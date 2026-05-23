// kcdx.bytes — Lua-facing byte-rewrite registration.
//
// Phase 2a of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md §"Confirmed design
// decisions" #2 — deferred-apply model). Succeeds the v0.1 [[patch]]
// TOML schema:
//
//   local h, err = kcdx.bytes{
//       name        = "outfit_swap_in_combat",
//       pattern     = "48 81 C1 60 0B 00 00 ...",   -- OR address_id, target_symbol
//       module      = "WHGame.dll",
//       offset      = 13,
//       original    = "44 8A F0",                   -- optional verify
//       replacement = "45 31 F6",
//       idempotent  = true,
//       priority    = 100,
//       context     = "...",
//       anchor_string = "...",
//   }
//   -- h:applied() -> nil (Pending), true (Applied), false (Failed)
//   -- h:reason()  -> string (when Failed)
//   -- h:name()    -> string
//
// Returns (nil, err) on argument-parse failure (invalid table, missing
// required fields, mutually-exclusive locator violation, etc.). On
// successful registration returns a handle whose :applied() flips
// from nil to true|false during the engine's end-of-zone apply pass.
//
// Per the plan §"Confirmed design decisions" #2:
//   - Validation runs IMMEDIATELY (locator format, length match,
//     exclusivity). Parse failures return (nil, err) so the caller
//     can react in straight-line code.
//   - The actual VirtualProtect + memcpy is DEFERRED to the apply
//     pass, which runs after every plugin in the current zone has
//     finished registering. This lets conflict_engine see all intent
//     across all plugins before any byte is written.

#include "lua_bind_bytes.h"

#include <memory>
#include <stdexcept>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "address_library.h"
#include "log.h"
#include "lua_registry.h"
#include "patch_engine.h"

namespace kcdx::lua_bind_bytes {

namespace {

// --- Lua-table helpers (kept TU-local) ---

std::string LuaTableString(lua_State* L, int tableIdx, const char* key,
                           const char* fallback = "") {
    lua_getfield(L, tableIdx, key);
    std::string out = fallback;
    if (lua_isstring(L, -1)) out = lua_tostring(L, -1);
    lua_pop(L, 1);
    return out;
}

int LuaTableInt(lua_State* L, int tableIdx, const char* key, int fallback) {
    lua_getfield(L, tableIdx, key);
    int out = fallback;
    if (lua_isnumber(L, -1)) out = static_cast<int>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

uint64_t LuaTableU64(lua_State* L, int tableIdx, const char* key,
                     uint64_t fallback) {
    lua_getfield(L, tableIdx, key);
    uint64_t out = fallback;
    if (lua_isnumber(L, -1)) {
        out = static_cast<uint64_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    return out;
}

bool LuaTableBool(lua_State* L, int tableIdx, const char* key, bool fallback) {
    lua_getfield(L, tableIdx, key);
    bool out = fallback;
    if (lua_isboolean(L, -1)) out = lua_toboolean(L, -1) != 0;
    lua_pop(L, 1);
    return out;
}

// Apply handler for Kind::Bytes. Invoked once per queued entry during
// kcdx::lua_registry::ApplyZone, in unified-load-order. Returns true
// on successful patch apply (or idempotent-skip), false on rejection
// with `reason_out` populated.
bool ApplyBytesEntry(kcdx::lua_registry::Entry& entry,
                     std::string& reason_out) {
    auto* p = std::static_pointer_cast<kcdx::patch::PatchEntry>(
        entry.payload).get();
    if (!p) {
        reason_out = "internal error: bytes entry payload is null";
        return false;
    }
    // patch::ApplyPatch logs its own diagnostic + handles
    // VirtualProtect / memcpy / idempotent-skip.
    bool ok = false;
    try {
        ok = kcdx::patch::ApplyPatch(*p);
    } catch (const std::exception& ex) {
        reason_out = ex.what();
        return false;
    }
    if (!ok) {
        reason_out = "patch apply rejected (see engine log for "
                     "locator + byte-mismatch diagnostic)";
    }
    return ok;
}

int Lua_Bytes(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "kcdx.bytes: expected a single table argument");
        return 2;
    }

    // Build the patch entry from the table.
    auto p = std::make_shared<kcdx::patch::PatchEntry>();
    p->sourceFile  = "<lua>";
    p->name        = LuaTableString(L, 1, "name", "lua_bytes");
    p->description = LuaTableString(L, 1, "description");
    // priority on individual entries is no longer honored — plugin-
    // level [load_order].priority is the single source of truth for
    // cross-plugin ordering, and intra-plugin order is determined by
    // the order entries are registered in plugin.lua. Silently accept
    // the field for forward-compat with old TOML conversions, but
    // INFO-log once-per-session so authors notice when they're
    // reaching for a knob that doesn't do anything.
    lua_getfield(L, 1, "priority");
    if (!lua_isnil(L, -1)) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            warnedOnce = true;
            log::Info("kcdx.bytes: entry-level 'priority' field is no "
                      "longer honored. Cross-plugin ordering comes from "
                      "the plugin's [load_order].priority (set in "
                      "kcdx.toml). Intra-plugin ordering is the "
                      "registration order in your plugin.lua. This "
                      "warning fires once per session.");
        }
    }
    lua_pop(L, 1);
    p->priority    = 50;   // engine-internal default; ignored everywhere
    p->module      = LuaTableString(L, 1, "module", "WHGame.dll");
    p->offset      = LuaTableInt(L, 1, "offset", 0);
    p->idempotent  = LuaTableBool(L, 1, "idempotent", true);
    p->addressId   = LuaTableU64(L, 1, "address_id", 0);
    p->targetSymbol = LuaTableString(L, 1, "target_symbol");

    const std::string patternStr     = LuaTableString(L, 1, "pattern");
    const std::string originalStr    = LuaTableString(L, 1, "original");
    const std::string replacementStr = LuaTableString(L, 1, "replacement");
    const std::string contextStr     = LuaTableString(L, 1, "context");
    const std::string anchorStr      = LuaTableString(L, 1, "anchor_string");

    // The owning plugin drives the self > engine > other precedence in the
    // name-resolution path below (ResolveByName / FindResolvedAuthorTarget,
    // naming-namespaces.md). Fetch it ONCE here and reuse it for the registry
    // Entry stamp at the bottom — no second OwningPluginForCurrentCall
    // stack-walk. callSiteFile/Line come back populated for the Entry too.
    // Launch-time registration only; never a hot path.
    std::string owningPlugin;
    std::string callSiteFile;
    int         callSiteLine = 0;
    owningPlugin = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, callSiteFile, callSiteLine);

    // `target = "<name>"` — the COMMON-PATH locator (the disassembler test,
    // cornerstones.md / AP12): the author names the site and the engine
    // resolves WHERE, exactly as kcdx.hook's `target` does. Distinct from
    // `target_symbol` (the cross-plugin published-symbol table) — `target`
    // names an Address Library seed entry OR an author-declared target. The
    // pattern / address_id / target_symbol locators remain the labeled
    // expert/advanced hatch for sites the name table can't yet name.
    const std::string targetName = LuaTableString(L, 1, "target");

    // Exactly-one-locator rule. Same invariant as the TOML loader + the
    // hook target/address_id-share rule. `target` is one of the mutually-
    // exclusive locators and joins the count.
    const int locatorCount =
        (!patternStr.empty()       ? 1 : 0) +
        (p->addressId != 0         ? 1 : 0) +
        (!p->targetSymbol.empty()  ? 1 : 0) +
        (!targetName.empty()       ? 1 : 0);
    if (locatorCount == 0) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': must specify exactly one locator "
            "(target, pattern, address_id, or target_symbol). The common "
            "path is target = \"<name>\" — a name the engine resolves to an "
            "address; pattern/address_id/target_symbol are the expert hatch.",
            p->name.c_str());
        return 2;
    }
    if (locatorCount > 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': locators are mutually exclusive "
            "(set exactly one of target, pattern, address_id, target_symbol)",
            p->name.c_str());
        return 2;
    }
    if (replacementStr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': missing required field 'replacement'",
            p->name.c_str());
        return 2;
    }

    // --- target = "<name>" → resolve the NAME into a locator field --------
    //
    // kcdx.bytes is a byte rewrite, not a typed hook, so `target` resolves
    // ONLY an ADDRESS (no signature) — the disassembler-test parity with
    // kcdx.hook (cornerstones.md / AP12): name the site, the engine resolves
    // WHERE. Resolution is launch-time (registration pass), never a hot path.
    //
    // The bytes apply pass (ApplyBytesEntry → patch::ApplyPatch → Resolve)
    // re-resolves from the PatchEntry, so `target` is resolved here into the
    // PatchEntry field that carries the named target's WHERE — mirroring
    // hook_chain::ResolveLocator's name→locator routing. With the opt-in
    // resolvedVa carrier on PatchEntry, this now reaches parity with kcdx.hook
    // for ALL locator kinds:
    //
    //   - VA-bearing names (engine seed, Rva author-target, AddressId
    //     author-target) → p->resolvedVa (the carrier; Resolve uses it directly)
    //   - Pattern author-target      → p->pattern       (parse the registry AOB)
    //   - TargetSymbol author-target → p->targetSymbol  (cross-plugin symbol)
    //
    // RESOLUTION ORDER (self > engine > other is enforced INSIDE both calls):
    //   1. ResolveByName(name, owner) → nonzero VA  → p->resolvedVa.
    //      address_library resolves an engine seed, an Rva author-target, AND
    //      an AddressId author-target (via Resolve(id)) to a VA here, so all
    //      three flow through the single resolvedVa carrier. Pattern /
    //      TargetSymbol author-targets return 0 from ResolveByName (the leaf
    //      module can't turn them into a VA without depending on the patch
    //      engine / symbol table) and fall through.
    //   2. Else FindResolvedAuthorTarget(name, owner) → Pattern / TargetSymbol
    //      author-target  → route through p->pattern / p->targetSymbol.
    //   3. Else genuine miss → teaching error.
    //
    // AddressId author-targets resolve via path 1 (resolvedVa) rather than the
    // p->addressId field — both produce the SAME final patchAddr (resolvedVa =
    // ResolveByName → ResolveAuthorTargetAddr → Resolve(id); the addressId
    // field path computes Resolve(id) too), so this is simpler and correct.
    if (!targetName.empty()) {
        const uintptr_t va = kcdx::address_library::ResolveByName(
            targetName.c_str(), owningPlugin.c_str());
        if (va) {
            // Engine seed, Rva author-target, or AddressId author-target — the
            // name resolved straight to a VA. Carry it; Resolve uses it
            // directly and skips locator resolution.
            p->resolvedVa = va;
        } else {
            // ResolveByName returned 0 — either a Pattern / TargetSymbol
            // author-target (no VA in this leaf module) or a genuine miss.
            // FindResolvedAuthorTarget disambiguates with the same precedence.
            const kcdx::address_library::AuthorTarget* at =
                kcdx::address_library::FindResolvedAuthorTarget(
                    targetName.c_str(), owningPlugin.c_str());
            if (at) {
                using Kind = kcdx::address_library::AuthorLocatorKind;
                switch (at->kind) {
                    case Kind::Pattern:
                        // The registry stores the AOB un-parsed; parse it here
                        // so the apply pass resolves it through the SAME
                        // pattern path a directly-set pattern= uses. A
                        // malformed AOB is an author error in the target's row
                        // — teach it, don't throw.
                        try {
                            p->pattern =
                                kcdx::patch::ParsePattern(at->locatorStr);
                        } catch (const std::exception& ex) {
                            lua_pushnil(L);
                            lua_pushfstring(L,
                                "kcdx.bytes '%s': target '%s' (author-declared "
                                "pattern) has a malformed AOB: %s",
                                p->name.c_str(), targetName.c_str(), ex.what());
                            return 2;
                        }
                        break;
                    case Kind::TargetSymbol:
                        p->targetSymbol = at->locatorStr;
                        break;
                    case Kind::Rva:
                    case Kind::AddressId:
                        // VA-bearing kinds should have resolved via
                        // ResolveByName above. Reaching here means the name
                        // table disagreed with itself; surface it rather than
                        // silently mis-resolving.
                        lua_pushnil(L);
                        lua_pushfstring(L,
                            "kcdx.bytes '%s': target '%s' is a VA-bearing "
                            "author-target but did not resolve to an address "
                            "(unverified row or game-version mismatch). Check "
                            "the target's row.",
                            p->name.c_str(), targetName.c_str());
                        return 2;
                    default:
                        lua_pushnil(L);
                        lua_pushfstring(L,
                            "kcdx.bytes '%s': target '%s' has an unsupported "
                            "author-target kind.",
                            p->name.c_str(), targetName.c_str());
                        return 2;
                }
            } else {
                // Genuine miss — no seed, no author target won the precedence.
                lua_pushnil(L);
                lua_pushfstring(L,
                    "kcdx.bytes '%s': target '%s' did not resolve (unknown "
                    "name, wrong game version, unverified row, or a typo). "
                    "Check the name against kcdx.addr.* or your declared "
                    "[[target]] rows.",
                    p->name.c_str(), targetName.c_str());
                return 2;
            }
        }
    }

    try {
        if (!patternStr.empty()) p->pattern = kcdx::patch::ParsePattern(patternStr);
        p->replacement = kcdx::patch::ParseBytes(replacementStr);
        if (!originalStr.empty()) p->original = kcdx::patch::ParseBytes(originalStr);
        if (!contextStr.empty()) {
            p->context = kcdx::patch::ParsePattern(contextStr);
        }
        if (!anchorStr.empty()) {
            p->anchor = kcdx::patch::AnchorString{anchorStr};
        }
    } catch (const std::exception& ex) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.bytes '%s': %s", p->name.c_str(), ex.what());
        return 2;
    }

    if (!p->original.empty() && p->original.size() != p->replacement.size()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes '%s': original length (%d) != replacement length (%d)",
            p->name.c_str(),
            static_cast<int>(p->original.size()),
            static_cast<int>(p->replacement.size()));
        return 2;
    }

    // Stamp owning plugin + call site for the registry. Reuse the owner +
    // call site already fetched above for the target= resolution — one
    // OwningPluginForCurrentCall stack-walk per call, not two.
    kcdx::lua_registry::Entry e;
    e.kind     = kcdx::lua_registry::Kind::Bytes;
    e.name     = p->name;
    e.priority = p->priority;
    e.payload  = p;  // shared_ptr<PatchEntry> stored as shared_ptr<void>
    e.pluginName    = owningPlugin;
    e.callSiteFile  = callSiteFile;
    e.callSiteLine  = callSiteLine;
    // Anonymous (no owning plugin) entries copy the plugin name into
    // the PatchEntry so patch_engine log lines have meaningful
    // attribution — they get the script source filename as a
    // placeholder until Phase 2h's [entrypoints].lua landing lets us
    // attribute properly.
    p->pluginName = e.pluginName.empty()
                        ? (e.callSiteFile.empty()
                            ? std::string("<lua>")
                            : e.callSiteFile)
                        : e.pluginName;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

}  // namespace

void bind(lua_State* L) {
    // Register the apply handler ONCE on first bind. Subsequent calls
    // (e.g. a re-bind during a Lua-state reset) overwrite — same
    // function pointer, no harm.
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Bytes, &ApplyBytesEntry);

    // Make sure the registry handle metatable exists before any
    // kcdx.bytes call can produce a handle userdata.
    kcdx::lua_registry::EnsureHandleMetatable(L);

    // kcdx.bytes is a plain C function on the kcdx table.
    lua_pushcfunction(L, Lua_Bytes);
    lua_setfield(L, -2, "bytes");
}

}  // namespace kcdx::lua_bind_bytes
