// kcdx.hook — Lua-facing function-interception registration.
//
// Phase 2b sub-3 of the manifest-only restructure (see
// docs/outstanding-work/restructure-plan.md). This is a game-mod
// authoring surface: a plugin's plugin.lua declares "when the game
// calls this function, run my Lua callback" by calling kcdx.hook{...}.
// It succeeds the v0.1 [[hook]] + [[mid_hook]] TOML schemas and the
// runtime kcdx.memory.dynamic_hook helper, collapsing all three into
// one function call.
//
//   local h = kcdx.hook{
//       name      = "outfit_gate",
//       address_id = "lua_pcall",   -- one locator: Address Library NAME
//                                   -- (or numeric id), or `address` (raw
//                                   -- VA/pointer), `target_symbol`,
//                                   -- `pattern`. See below.
//       signature = "void (ptr self, wstr szApp, u32 flags)",
//       -- attach the callback under the MODE NAME itself. Exactly one of
//       -- before / after / around / replace per call:
//       before = function(self, szApp, flags)
//           if szApp:find(":") then szApp = (szApp:gsub(":", " -")) end
//           return self, szApp, flags          -- returned values flow into the original
//       end,
//   }
//   -- h:applied() -> nil (Pending) | true (Applied) | false (Failed)
//   -- h:reason()  -> string (when Failed)
//   -- h:name()    -> string
//
// Callback surface (the author writes the target function, in Lua):
//   - Params arrive as POSITIONAL callback arguments, named by the
//     author's own function(...) list. The signature string supplies
//     the TYPES; the parameter names are the author's choice.
//   - Mutation is by RETURN. "What you return is what flows forward."
//       before  : return nothing -> original runs with unchanged args;
//                 return N values -> they replace the args. before
//                 ALWAYS runs the original (it massages inputs only).
//       after   : receives the return value; returns the (possibly
//                 changed) return value.
//       replace : original never runs; the return is the result. An
//                 empty replace = function() end suppresses the call.
//       around  : receives the original as a callable first parameter;
//                 calls it 0/1/N times, returns the result. The full
//                 wrap; the only mode that can conditionally skip.
//
// SCOPE: this file builds the queued payload, parses the signature,
// validates the locator + the mode-as-key rule (exactly one of
// before/after/around/replace), takes a GC-safe ref to the callback,
// and enqueues. The deferred apply pass (Kind::Hook handler ->
// hook_chain::Add) installs the interception in unified load order.
// mid / callsite modes are NOT accepted here yet (their own sub-commits;
// the payload carries the enum values so those subs are additive).
//
// Validation runs IMMEDIATELY (table shape, locator exclusivity, mode
// name, signature parse) so the caller gets (nil, err) in straight-line
// code. Per the plan §"Confirmed design decisions" #2, the install
// itself waits for the end-of-zone apply pass so conflict resolution
// sees every plugin's intent before any byte changes.

#include "lua_bind_hook.h"

#include <memory>
#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "load_order.h"
#include "log.h"
#include "lua_memory.h"
#include "lua_registry.h"
#include "patch_engine.h"
#include "scripting.h"

namespace kcdx::lua_bind_hook {

namespace {

// --- Lua-table helpers (kept TU-local; same shape as lua_bind_bytes) ---

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

// Read a `key` that may be a kcdx.memory.pointer userdata, a raw
// lightuserdata (exact VA — preferred for pointer-magnitude values; see
// .claude/rules/lua-precision.md), or an integer VA. Returns 0 if absent
// / wrong type. Same target shape as kcdx.memory.dynamic_hook's `target`.
uintptr_t LuaTableAddress(lua_State* L, int tableIdx, const char* key) {
    lua_getfield(L, tableIdx, key);
    uintptr_t out = 0;
    if (lua_islightuserdata(L, -1)) {
        out = reinterpret_cast<uintptr_t>(lua_touserdata(L, -1));
    } else if (lua_isnumber(L, -1)) {
        out = static_cast<uintptr_t>(lua_tointeger(L, -1));
    } else if (lua_isuserdata(L, -1)) {
        if (lua_getmetatable(L, -1)) {
            luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
            if (lua_rawequal(L, -1, -2)) {
                lua_pop(L, 2);
                auto* p = static_cast<kcdx::lua_memory::pointer*>(
                    lua_touserdata(L, -1));
                if (p) out = static_cast<uintptr_t>(p->get_address());
            } else {
                lua_pop(L, 2);
            }
        }
    }
    lua_pop(L, 1);
    return out;
}

uint64_t LuaTableU64(lua_State* L, int tableIdx, const char* key,
                     uint64_t fallback) {
    lua_getfield(L, tableIdx, key);
    uint64_t out = fallback;
    if (lua_isnumber(L, -1)) out = static_cast<uint64_t>(lua_tointeger(L, -1));
    lua_pop(L, 1);
    return out;
}

// Pull `key` from the table as a Lua function and stash it in the Lua
// registry so the GC won't collect the closure between registration
// and the deferred apply pass. Returns the luaL_ref handle, or
// LUA_NOREF if the field is absent / not a function.
int TakeCallbackRef(lua_State* L, int tableIdx, const char* key) {
    lua_getfield(L, tableIdx, key);
    if (!lua_isfunction(L, -1)) {
        lua_pop(L, 1);
        return LUA_NOREF;
    }
    // luaL_ref pops the value off the stack and returns a stable int key
    // into LUA_REGISTRYINDEX. Released by the apply pass / teardown via
    // luaL_unref when the hook is uninstalled or fails to apply.
    return luaL_ref(L, LUA_REGISTRYINDEX);
}

// --- Apply handler ------------------------------------------------------
//
// Registered for Kind::Hook. Runs in the end-of-zone apply pass
// (lua_registry::ApplyZone), in unified load order. Installs the hook by
// resolving its locator + appending to the per-target chain via
// hook_chain::Add. On success the handle goes Applied; on conflict /
// resolution failure it goes Failed with a clear reason. On failure the
// callback registry ref is released (no live hook will use it).
bool ApplyHookEntry(kcdx::lua_registry::Entry& entry,
                    std::string& reason_out) {
    auto sp = std::static_pointer_cast<kcdx::hook_payload::HookPayload>(
                  entry.payload);
    kcdx::hook_payload::HookPayload* p = sp.get();
    if (!p) {
        reason_out = "internal error: hook entry payload is null";
        return false;
    }

    // Effective load-order priority for chain ordering. Anonymous
    // entries (no owning plugin yet) fall back to the entry's own
    // priority field (default 50).
    int priority = entry.priority;
    if (!entry.pluginName.empty()) {
        priority = kcdx::load_order::Of(entry.pluginName).priority;
    }

    // L is captured by hook_chain at first-tick (SetLuaState) and again
    // on first Add; pass nullptr here — Add uses the already-bound state.
    kcdx::hook_chain::AddResult r = kcdx::hook_chain::Add(
        /*L=*/nullptr, *p, p->callbackRef, entry.pluginName, priority,
        entry.name);

    if (!r.ok) {
        reason_out = r.reason;
        // Release the callback ref — this hook will never fire.
        if (p->callbackRef != LUA_NOREF) {
            lua_State* gs = kcdx::scripting::lua_state();
            if (gs) luaL_unref(gs, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
        return false;
    }
    return true;
}

// --- Locator validation -------------------------------------------------
//
// Exactly one function-entry locator must be set for non-callsite
// modes. For callsite mode, the callsite sub-locator carries the patch
// target and function_name (if present) is signature-info only, so the
// function-entry-locator count must be zero. Returns an empty string on
// success or a ready-to-return diagnostic on failure.
std::string ValidateLocator(const kcdx::hook_payload::HookPayload& p) {
    const int entryLocatorCount =
        (!p.functionName.empty()       ? 1 : 0) +
        (!p.pattern.bytes.empty()      ? 1 : 0) +
        (p.addressId != 0              ? 1 : 0) +
        (!p.addressName.empty()        ? 1 : 0) +
        (!p.targetSymbol.empty()       ? 1 : 0) +
        (!p.targetLuaCfunction.empty() ? 1 : 0) +
        (p.address != 0                ? 1 : 0);

    if (p.mode == kcdx::hook_payload::Mode::Callsite) {
        if (!p.callsite.has_value()) {
            return "mode='callsite' requires a target_callsite table "
                   "(pattern, address_id, or rva)";
        }
        const auto& cs = *p.callsite;
        const int csLocatorCount =
            (!cs.pattern.bytes.empty() ? 1 : 0) +
            (cs.addressId != 0         ? 1 : 0) +
            (!cs.rva.empty()           ? 1 : 0);
        if (csLocatorCount != 1) {
            return "target_callsite must set exactly one of "
                   "pattern, address_id, or rva";
        }
        // function_name is allowed here (it supplies the called
        // function's signature); the other entry locators are not.
        const int disallowed = entryLocatorCount -
                               (!p.functionName.empty() ? 1 : 0);
        if (disallowed > 0) {
            return "mode='callsite' uses target_callsite for the patch "
                   "target; do not also set pattern/address_id/"
                   "target_symbol/target_lua_cfunction (function_name is "
                   "allowed, for signature info only)";
        }
        return "";
    }

    // Non-callsite modes: exactly one function-entry locator.
    if (entryLocatorCount == 0) {
        return "must specify exactly one locator (address, function_name, "
               "pattern, address_id, target_symbol, or "
               "target_lua_cfunction)";
    }
    if (entryLocatorCount > 1) {
        return "locators are mutually exclusive (set exactly one of "
               "address, function_name, pattern, address_id, "
               "target_symbol, target_lua_cfunction)";
    }
    if (p.callsite.has_value()) {
        return "target_callsite is only valid with mode='callsite'";
    }
    return "";
}

// Build the optional callsite sub-locator from a nested
// `target_callsite = {...}` table, if present. Leaves `out` as
// nullopt when the field is absent. Returns "" on success, a
// diagnostic otherwise. Pattern parse errors surface here.
std::string ReadCallsite(lua_State* L, int tableIdx,
                         std::optional<kcdx::hook_payload::CallsiteLocator>& out) {
    lua_getfield(L, tableIdx, "target_callsite");
    if (lua_isnil(L, -1)) {
        lua_pop(L, 1);
        return "";
    }
    if (!lua_istable(L, -1)) {
        lua_pop(L, 1);
        return "target_callsite must be a table";
    }
    const int csIdx = lua_gettop(L);
    kcdx::hook_payload::CallsiteLocator cs;
    cs.offset    = LuaTableInt(L, csIdx, "offset", 0);
    cs.addressId = LuaTableU64(L, csIdx, "address_id", 0);
    cs.rva       = LuaTableString(L, csIdx, "rva");
    const std::string csPattern = LuaTableString(L, csIdx, "pattern");
    lua_pop(L, 1);  // pop the target_callsite table

    if (!csPattern.empty()) {
        try {
            cs.pattern = kcdx::patch::ParsePattern(csPattern);
        } catch (const std::exception& ex) {
            return std::string("target_callsite.pattern: ") + ex.what();
        }
    }
    out = std::move(cs);
    return "";
}

int Lua_Hook(lua_State* L) {
    if (!lua_istable(L, 1)) {
        lua_pushnil(L);
        lua_pushstring(L, "kcdx.hook: expected a single table argument");
        return 2;
    }

    auto p = std::make_shared<kcdx::hook_payload::HookPayload>();
    p->name        = LuaTableString(L, 1, "name", "lua_hook");
    p->description = LuaTableString(L, 1, "description");
    p->module      = LuaTableString(L, 1, "module", "WHGame.dll");
    p->offset      = LuaTableInt(L, 1, "offset", 0);

    // Entry-level 'priority' is no longer honored — cross-plugin order
    // comes from the plugin's [load_order].priority (kcdx.toml);
    // intra-plugin order is registration order in plugin.lua. Mirror
    // the kcdx.bytes once-per-session INFO so authors notice the dead
    // knob. (See restructure-plan §"Confirmed design decisions".)
    lua_getfield(L, 1, "priority");
    if (!lua_isnil(L, -1)) {
        static bool warnedOnce = false;
        if (!warnedOnce) {
            warnedOnce = true;
            log::Info("kcdx.hook: entry-level 'priority' field is no "
                      "longer honored. Cross-plugin ordering comes from "
                      "the plugin's [load_order].priority (set in "
                      "kcdx.toml). Intra-plugin ordering is the "
                      "registration order in your plugin.lua. This "
                      "warning fires once per session.");
        }
    }
    lua_pop(L, 1);

    // --- Mode-as-key + callback ---
    // The author attaches the callback under the mode name itself:
    //   kcdx.hook{ ..., before = function(...) ... end }
    // Exactly ONE of before / after / around / replace must hold a
    // function (one mode per call — chain multiple modes on a target by
    // making multiple kcdx.hook calls). mid / callsite are not accepted
    // in this sub (they land in their own sub-commits); the payload
    // still carries Mode::Mid/Callsite so those subs are additive.
    struct ModeKey { const char* key; kcdx::hook_payload::Mode mode; };
    static const ModeKey kModeKeys[] = {
        {"before",  kcdx::hook_payload::Mode::Before},
        {"after",   kcdx::hook_payload::Mode::After},
        {"around",  kcdx::hook_payload::Mode::Around},
        {"replace", kcdx::hook_payload::Mode::Replace},
    };
    int    modeCount = 0;
    bool   haveMode  = false;
    for (const auto& mk : kModeKeys) {
        lua_getfield(L, 1, mk.key);
        const bool isFn = lua_isfunction(L, -1);
        lua_pop(L, 1);
        if (isFn) {
            ++modeCount;
            if (!haveMode) { p->mode = mk.mode; haveMode = true; }
        }
    }
    if (modeCount == 0) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': must attach exactly one callback under a "
            "mode key (before, after, around, or replace), e.g. "
            "before = function(...) ... end",
            p->name.c_str());
        return 2;
    }
    if (modeCount > 1) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': attach exactly ONE mode per call; found "
            "multiple of before/after/around/replace. Use separate "
            "kcdx.hook calls to put more than one mode on a target.",
            p->name.c_str());
        return 2;
    }

    // --- Function-entry locator fields ---
    p->functionName       = LuaTableString(L, 1, "function_name");
    // address_id accepts EITHER a human-readable Address Library name
    // (string → addressName) OR a numeric id (number → addressId). One
    // field, both forms — authors can write the readable name they see
    // in kcdx.addr.* instead of memorizing the opaque number. Dispatch on
    // the actual Lua type (LUA_TSTRING vs LUA_TNUMBER), not lua_isstring/
    // lua_isnumber which cross-coerce in Lua 5.1.
    lua_getfield(L, 1, "address_id");
    if (lua_type(L, -1) == LUA_TSTRING) {
        p->addressName = lua_tostring(L, -1);
    } else if (lua_type(L, -1) == LUA_TNUMBER) {
        p->addressId = static_cast<uint64_t>(lua_tointeger(L, -1));
    }
    lua_pop(L, 1);
    p->targetSymbol       = LuaTableString(L, 1, "target_symbol");
    p->targetLuaCfunction = LuaTableString(L, 1, "target_lua_cfunction");
    p->address            = LuaTableAddress(L, 1, "address");
    const std::string patternStr = LuaTableString(L, 1, "pattern");
    const std::string contextStr = LuaTableString(L, 1, "context");
    const std::string anchorStr  = LuaTableString(L, 1, "anchor_string");

    try {
        if (!patternStr.empty()) {
            p->pattern = kcdx::patch::ParsePattern(patternStr);
        }
        if (!contextStr.empty()) {
            p->context = kcdx::patch::ParsePattern(contextStr);
        }
        if (!anchorStr.empty()) {
            p->anchor = kcdx::patch::AnchorString{anchorStr};
        }
    } catch (const std::exception& ex) {
        lua_pushnil(L);
        lua_pushfstring(L, "kcdx.hook '%s': %s", p->name.c_str(), ex.what());
        return 2;
    }

    // --- Callsite sub-locator (mode == callsite) ---
    {
        std::string csErr = ReadCallsite(L, 1, p->callsite);
        if (!csErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook '%s': %s",
                            p->name.c_str(), csErr.c_str());
            return 2;
        }
    }

    // --- Locator exclusivity / completeness ---
    {
        std::string locErr = ValidateLocator(*p);
        if (!locErr.empty()) {
            lua_pushnil(L);
            lua_pushfstring(L, "kcdx.hook '%s': %s",
                            p->name.c_str(), locErr.c_str());
            return 2;
        }
    }

    // --- Signature DSL ---
    // Required for before/after/around/replace — the engine needs the
    // ABI to marshal arguments + return value to/from the callback.
    // Parse now so the apply pass never re-parses.
    const std::string sigStr = LuaTableString(L, 1, "signature");
    if (sigStr.empty()) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': a 'signature' is required (e.g. "
            "\"void (ptr self, wstr szApp)\") so the engine knows the "
            "function's argument + return types",
            p->name.c_str());
        return 2;
    }
    {
        auto sr = kcdx::hook_signature::Parse(sigStr);
        if (!sr.ok) {
            lua_pushnil(L);
            if (sr.errorColumn > 0) {
                lua_pushfstring(L,
                    "kcdx.hook '%s': signature parse error at column %d: %s",
                    p->name.c_str(), sr.errorColumn, sr.error.c_str());
            } else {
                lua_pushfstring(L,
                    "kcdx.hook '%s': signature parse error: %s",
                    p->name.c_str(), sr.error.c_str());
            }
            return 2;
        }
        p->signature    = std::move(sr.sig);
        p->hasSignature = true;
    }

    // --- Callback closure ---
    // Take a GC-safe registry ref to the function attached under the
    // chosen mode key, so the closure survives until the apply pass.
    p->callbackRef = TakeCallbackRef(
        L, 1, kcdx::hook_payload::ModeToken(p->mode));
    if (p->callbackRef == LUA_NOREF) {
        // Shouldn't happen — mode detection above already confirmed a
        // function under this key — but guard defensively.
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': internal error: mode '%s' callback vanished "
            "between detection and ref",
            p->name.c_str(), kcdx::hook_payload::ModeToken(p->mode));
        return 2;
    }

    // --- Queue the registration ---
    kcdx::lua_registry::Entry e;
    e.kind    = kcdx::lua_registry::Kind::Hook;
    e.name    = p->name;
    e.payload = p;  // shared_ptr<HookPayload> stored as shared_ptr<void>
    e.pluginName = kcdx::lua_registry::OwningPluginForCurrentCall(
        L, e.callSiteFile, e.callSiteLine);

    std::string err;
    uint64_t handleId = kcdx::lua_registry::Append(std::move(e), &err);
    if (handleId == 0) {
        // Append failed; the entry was not enqueued, so release the
        // callback ref we took above (no apply pass will free it).
        if (p->callbackRef != LUA_NOREF) {
            luaL_unref(L, LUA_REGISTRYINDEX, p->callbackRef);
            p->callbackRef = LUA_NOREF;
        }
    }
    return kcdx::lua_registry::PushHandleOrError(L, handleId, err);
}

}  // namespace

void bind(lua_State* L) {
    kcdx::lua_registry::RegisterApplyHandler(
        kcdx::lua_registry::Kind::Hook, &ApplyHookEntry);

    kcdx::lua_registry::EnsureHandleMetatable(L);

    lua_pushcfunction(L, Lua_Hook);
    lua_setfield(L, -2, "hook");
}

}  // namespace kcdx::lua_bind_hook
