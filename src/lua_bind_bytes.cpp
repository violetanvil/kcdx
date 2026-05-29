// kcdx.bytes — Lua-facing byte-rewrite registration.
//
// Part of the manifest-only restructure (deferred-apply model).
// Succeeds the v0.1 [[patch]] TOML schema:
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
#include "declared_targets.h"  // smart-resolver presence probe (author-declared store)
#include "log.h"
#include "lua_bind_helpers.h"  // FindUnknownKey (shared unknown-key gate)
#include "lua_registry.h"
#include "patch_engine.h"
#include "plugin_loader.h"     // g_runtimeGameVersionString (declared-store lookup arg)
#include "refdb.h"             // smart-resolver presence probe (engine seed)

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

// --- Unknown-key rejection (fail loud, never silent-drop) ---------------
//
// The recognized option-key set for kcdx.bytes. A typo'd `replacment=` /
// `targt=` would otherwise be silently ignored, the author's intent lost.
// The iteration is the shared kcdx::lua_bind_helpers::FindUnknownKey; this
// list stays local because the key set belongs to this binder.
static const char* kKnown[] = {
    "name", "description", "priority", "module", "offset", "idempotent",
    "address_id", "target_symbol", "pattern", "original", "replacement",
    "context", "anchor_string", "target",
};

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

    // Reject an unrecognized option key before reading anything — a typo'd
    // key would otherwise vanish silently (fail loud, never silent-drop).
    {
        std::string bad = kcdx::lua_bind_helpers::FindUnknownKey(
            L, 1, kKnown, sizeof(kKnown) / sizeof(kKnown[0]));
        if (!bad.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.bytes: unrecognized option key '%s' — not a recognized "
                "kcdx.bytes option (check for a typo).",
                bad.c_str());
            return 2;
        }
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

    // The owning plugin identity drives the self > engine > other
    // precedence in the name-resolution path below (ResolveByName /
    // FindResolvedAuthorTarget, self > engine > other precedence). Fetch BOTH
    // components ONCE here and reuse them for the registry Entry stamp +
    // the PatchEntry's own author/plugin fields — no second
    // OwningPluginForCurrentCall stack-walk. callSiteFile/Line come back
    // populated for the Entry too. Launch-time registration only; never
    // a hot path.
    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    // `target = "<name>"` — the COMMON-PATH locator (the disassembler test —
    // the name carries the address): the author names the site and the engine
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
    // kcdx.hook (the disassembler test): name the site, the engine resolves
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
        // The binder now threads the real (author, plugin) pair from the
        // OwningPluginForCurrentCall struct (step 4 of the 2-dot namespace
        // refactor). What remains transitional is plugin manifests whose
        // [plugin].author is still empty (step 6 populates them); when the
        // author is empty here the resolver walks the legacy 1-dot scope
        // by (plugin, name), preserving the existing observable behavior
        // for the current corpus.
        const uintptr_t va = kcdx::address_library::ResolveByName(
            targetName.c_str(), owner.author.c_str(), owner.plugin.c_str());
        if (va) {
            // Engine seed, Rva author-target, or AddressId author-target — the
            // name resolved straight to a VA. Carry it; Resolve uses it
            // directly and skips locator resolution.
            p->resolvedVa = va;
        } else {
            // ResolveByName returned 0 — either a Pattern / TargetSymbol
            // author-target (no VA in this leaf module) or a genuine miss.
            // FindResolvedAuthorTarget disambiguates with the same precedence
            // and the same real (author, plugin) the ResolveByName call
            // threads.
            const kcdx::address_library::AuthorTarget* at =
                kcdx::address_library::FindResolvedAuthorTarget(
                    targetName.c_str(), owner.author.c_str(),
                    owner.plugin.c_str());
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
    e.pluginName    = owner.plugin;
    e.callSiteFile  = callSiteFile;
    e.callSiteLine  = callSiteLine;
    // Anonymous (no owning plugin) entries copy the plugin name into
    // the PatchEntry so patch_engine log lines have meaningful
    // attribution — they get the script source filename as a
    // placeholder until the [entrypoints].lua landing lets us
    // attribute properly. The author always threads through verbatim
    // (empty for anonymous; the manifest's [plugin].author otherwise).
    p->pluginAuthor = owner.author;
    p->pluginName = e.pluginName.empty()
                        ? (e.callSiteFile.empty()
                            ? std::string("<lua>")
                            : e.callSiteFile)
                        : e.pluginName;

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

// =============================================================================
// Smart-resolver surface — kcdx.bytes.<name>{...}
// =============================================================================
//
// kcdx.bytes is registered as a TABLE with two metamethods:
//   __call  → forwards to the flat-table form (Lua_Bytes) so the legacy
//             kcdx.bytes{ target = "...", replacement = "..." } shape
//             keeps working unchanged.
//   __index → smart-resolver: kcdx.bytes.<name> probes the unified
//             named-target table (declared store + engine seed + cross-
//             plugin legacy author targets). Miss → returns nil (so
//             the next access raises "attempt to index a nil value"
//             naming the typoed slot). Hit → returns a verb-bound
//             userdata that carries (resolved name, owner identity)
//             baked in via a metatable on which __call IS the install
//             (single-mode verb — no per-mode access).
//
// The install closure synthesizes a flat-table form { target = "<name>",
// ...opts } and dispatches to the existing Lua_Bytes C function via
// lua_call. The install codepath is identical to the flat-table form's
// — same locator validation, same length-match check, same apply pass.
//
// Out of scope here (kept on the flat-table form): raw pattern= /
// address_id= / target_symbol= locators. Those paths have no name to
// drive the resolver and stay on the explicit table form.

// (We are still inside the file-wide anonymous namespace opened above —
// no second `namespace {` here; the helpers below share the same TU-
// local linkage as Lua_Bytes / ApplyBytesEntry / the LuaTable* helpers.)

// One-byte payload; the resolved facts live on the userdata's envtable
// (name, author, plugin), same shape as the kcdx.hook smart-resolver
// userdata.
struct ResolvedBytesUd {
    char unused;
};

constexpr const char* kBytesResolvedMt = "kcdx.bytes.resolved";

// Probe whether `name` resolves to ANY population source the install
// path would consult. Same shape as the kcdx.hook probe (the unified
// table backs both verbs).
bool ResolveProbe(const std::string& name,
                  const std::string& author,
                  const std::string& plugin) {
    if (kcdx::address_library::ResolveByName(
            name.c_str(), author.c_str(), plugin.c_str()) != 0) {
        return true;
    }
    if (kcdx::address_library::FindResolvedAuthorTarget(
            name.c_str(), author.c_str(), plugin.c_str()) != nullptr) {
        return true;
    }
    if (!plugin.empty()) {
        kcdx::declared_targets::ResolvedDeclared d =
            kcdx::declared_targets::LookupForCaller(
                author, plugin, name,
                ::kcdx::plugins::g_runtimeGameVersionString);
        if (d.kind != kcdx::declared_targets::ResolvedDeclared::Kind::NoEntry) {
            return true;
        }
    }
    return false;
}

int Lua_BytesCall(lua_State* L);
int Lua_BytesIndex(lua_State* L);
int Lua_BytesResolvedCall(lua_State* L);

// The kcdx.bytes table's __call metamethod — forwards args 2..N to
// Lua_Bytes (arg 1 is the kcdx.bytes table itself, supplied by the
// __call dispatcher).
int Lua_BytesCall(lua_State* L) {
    const int n = lua_gettop(L);
    lua_pushcfunction(L, Lua_Bytes);
    for (int i = 2; i <= n; ++i) lua_pushvalue(L, i);
    lua_call(L, n - 1, LUA_MULTRET);
    return lua_gettop(L) - n;
}

// The kcdx.bytes table's __index metamethod — the smart resolver.
int Lua_BytesIndex(lua_State* L) {
    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        return 1;
    }
    const char* nameCStr = lua_tostring(L, 2);
    std::string name = nameCStr ? nameCStr : "";
    if (name.empty()) { lua_pushnil(L); return 1; }

    std::string callSiteFile;
    int         callSiteLine = 0;
    kcdx::lua_registry::OwningPlugin owner =
        kcdx::lua_registry::OwningPluginForCurrentCall(
            L, callSiteFile, callSiteLine);

    if (!ResolveProbe(name, owner.author, owner.plugin)) {
        // Typo-fails-fast: the name doesn't resolve anywhere.
        lua_pushnil(L);
        return 1;
    }

    // Allocate the verb-bound userdata; stash (name, author, plugin)
    // on the envtable. Bytes is a single-mode verb — the userdata's
    // __call IS the install, no per-mode access required.
    auto* ud = static_cast<ResolvedBytesUd*>(
        lua_newuserdata(L, sizeof(ResolvedBytesUd)));
    ud->unused = 0;
    luaL_getmetatable(L, kBytesResolvedMt);
    lua_setmetatable(L, -2);
    lua_newtable(L);
    lua_pushstring(L, name.c_str());         lua_setfield(L, -2, "name");
    lua_pushstring(L, owner.author.c_str()); lua_setfield(L, -2, "author");
    lua_pushstring(L, owner.plugin.c_str()); lua_setfield(L, -2, "plugin");
    lua_setfenv(L, -2);
    return 1;
}

// The verb-bound userdata's __call metamethod — IS the install.
// Single-mode verb: the author writes kcdx.bytes.<name>{ replacement =
// "..." }, the closure synthesizes { target = name, ...opts } and
// dispatches to Lua_Bytes.
//
//   arg 1: the userdata.
//   arg 2: the options table (required — at minimum carries
//          replacement=, plus any of original=/idempotent=/offset=
//          /context=/anchor_string=).
//
// Forbidden in the opts: target= / pattern= / address_id= /
// target_symbol= (those are the locator-providing keys, fixed by the
// smart-resolver name).
int Lua_BytesResolvedCall(lua_State* L) {
    lua_getfenv(L, 1);
    lua_getfield(L, -1, "name");
    std::string name = lua_isstring(L, -1) ? lua_tostring(L, -1) : "?";
    lua_pop(L, 2);

    if (lua_gettop(L) < 2 || !lua_istable(L, 2)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.bytes.%s(...): expected a single table argument "
            "(at minimum { replacement = \"...\" }). The smart-resolver "
            "form fixes the locator (the name); the table carries the "
            "rewrite payload.",
            name.c_str());
        return 2;
    }

    // Reject locator keys the closure already supplies.
    static const char* const kForbidden[] = {
        "target", "pattern", "address_id", "target_symbol",
    };
    for (const char* fk : kForbidden) {
        lua_getfield(L, 2, fk);
        const bool present = !lua_isnil(L, -1);
        lua_pop(L, 1);
        if (present) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.bytes.%s(opts): the opts table cannot supply "
                "'%s' — the locator is fixed by the kcdx.bytes.<name> "
                "form. Drop the '%s' key, or switch to the flat-table "
                "form (kcdx.bytes{...}) if you need to override.",
                name.c_str(), fk, fk);
            return 2;
        }
    }

    // Synthesize { target = name, ...opts }. Build a fresh table at
    // the top of the stack; copy every author-supplied key onto it;
    // stamp target= last so the merge can't shadow it.
    lua_pushcfunction(L, Lua_Bytes);
    lua_newtable(L);
    const int synthIdx = lua_gettop(L);
    lua_pushnil(L);
    while (lua_next(L, 2) != 0) {
        // key at -2, value at -1.
        lua_pushvalue(L, -2);   // key copy
        lua_pushvalue(L, -2);   // value copy
        lua_settable(L, synthIdx);
        lua_pop(L, 1);          // pop value; keep key for next lua_next
    }
    lua_pushstring(L, name.c_str());
    lua_setfield(L, synthIdx, "target");

    lua_call(L, 1, LUA_MULTRET);
    return lua_gettop(L) - 2;  // discount the (self, opts) args
}

void EnsureSmartResolverMetatables(lua_State* L) {
    // kcdx.bytes table metatable — __call + __index.
    if (luaL_newmetatable(L, "kcdx.bytes.verb") != 0) {
        lua_pushcfunction(L, Lua_BytesCall);
        lua_setfield(L, -2, "__call");
        lua_pushcfunction(L, Lua_BytesIndex);
        lua_setfield(L, -2, "__index");
        lua_pushstring(L, "kcdx.bytes.verb");
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);

    // Resolved-userdata metatable — __call only (single-mode verb).
    if (luaL_newmetatable(L, kBytesResolvedMt) != 0) {
        lua_pushcfunction(L, Lua_BytesResolvedCall);
        lua_setfield(L, -2, "__call");
        lua_pushstring(L, kBytesResolvedMt);
        lua_setfield(L, -2, "__metatable");
    }
    lua_pop(L, 1);
}

}  // namespace

// Register the Kind::Bytes deferred-apply handler. ENGINE state, not
// Lua-surface state — it makes Kind::Bytes appliable regardless of which
// surface queued the entry. Called at engine init (dllmain.cpp, before
// DiscoverAndLoad), NOT from bind(): a future kcdxBytesInterface installing
// a byte-patch at C++ kcdxPlugin_Load time (DllMain-phase) would hit the
// same wall the Kind::Hook handler did (lua_registry::Append rejects any
// Kind with no handler; bind() runs too late at first-update-tick).
// ApplyBytesEntry is the TU-local static above; RegisterHandlers() sees it
// from the same TU. The call MOVED out of bind() — it is not duplicated.
void RegisterHandlers() {
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Bytes, &ApplyBytesEntry);
}

void bind(lua_State* L) {
    // Lua-surface wiring. The Kind::Bytes apply handler is registered
    // earlier, at engine init, by RegisterHandlers().

    // Make sure the registry handle metatable exists before any
    // kcdx.bytes call can produce a handle userdata.
    kcdx::lua_registry::EnsureHandleMetatable(L);
    EnsureSmartResolverMetatables(L);

    // kcdx.bytes is a TABLE with two metamethods:
    //   __call  → forwards to the flat-table form (kcdx.bytes{...}).
    //   __index → smart resolver (kcdx.bytes.<name>{...}).
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    luaL_getmetatable(L, "kcdx.bytes.verb");
    lua_setmetatable(L, -2);
    lua_setfield(L, kcdx_idx, "bytes");
}

}  // namespace kcdx::lua_bind_bytes
