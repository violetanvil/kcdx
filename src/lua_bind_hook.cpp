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
//       name      = "bugsplat_filename_fix",
//       function_name = "WHGame.dll!Symbol",   -- one locator (see below)
//       mode      = "before",                  -- before|after|around|replace|mid|callsite
//       signature = "void (ptr, wstr szApp, u32)",
//       callback  = function(args, call_original) ... end,
//   }
//   -- h:applied() -> nil (Pending) | true (Applied) | false (Failed)
//   -- h:reason()  -> string (when Failed)
//   -- h:name()    -> string
//
// SCOPE OF THIS COMMIT (sub-3): build the queued payload, parse the
// signature DSL, validate the locator + mode + exclusivity rules, take
// a GC-safe reference to the callback closure, and enqueue the
// registration. The actual interception install is DEFERRED to the
// per-mode apply commits (sub-4..9): this file registers an apply
// handler that, until those land, marks the entry Failed with a clear
// "mode not yet implemented" reason. That keeps handle:applied()
// honest — a plugin sees false + a diagnostic rather than a silent
// no-op.
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

#include "hook_payload.h"
#include "hook_signature.h"
#include "log.h"
#include "lua_registry.h"
#include "patch_engine.h"

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

// --- Apply handler (sub-3 stub) ----------------------------------------
//
// Registered for Kind::Hook. The per-mode install routines (sub-4..9)
// replace this with real dispatch. Until then, every queued hook
// resolves to Failed with an explicit reason — the registration,
// parsing, and validation all ran (and would have errored loudly if
// wrong), but no interception is installed yet. This is intentional:
// sub-3's contract is "the surface exists, validates, and queues"; the
// behavior arrives mode-by-mode.
bool ApplyHookEntry(kcdx::lua_registry::Entry& entry,
                    std::string& reason_out) {
    auto* p = std::static_pointer_cast<kcdx::hook_payload::HookPayload>(
                  entry.payload)
                  .get();
    if (!p) {
        reason_out = "internal error: hook entry payload is null";
        return false;
    }
    reason_out =
        std::string("kcdx.hook mode='") +
        kcdx::hook_payload::ModeToken(p->mode) +
        "' install is not yet implemented (Phase 2b sub-3 ships the "
        "registration + validation surface; per-mode apply lands in "
        "sub-4..9). The registration parsed and validated cleanly.";
    return false;
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
        (!p.targetSymbol.empty()       ? 1 : 0) +
        (!p.targetLuaCfunction.empty() ? 1 : 0);

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
        return "must specify exactly one locator (function_name, "
               "pattern, address_id, target_symbol, or "
               "target_lua_cfunction)";
    }
    if (entryLocatorCount > 1) {
        return "locators are mutually exclusive (set exactly one of "
               "function_name, pattern, address_id, target_symbol, "
               "target_lua_cfunction)";
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

    // --- Mode ---
    const std::string modeStr = LuaTableString(L, 1, "mode", "before");
    if (!kcdx::hook_payload::ParseMode(modeStr, p->mode)) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': unknown mode '%s' (expected one of: "
            "before, after, around, replace, mid, callsite)",
            p->name.c_str(), modeStr.c_str());
        return 2;
    }

    // --- Function-entry locator fields ---
    p->functionName       = LuaTableString(L, 1, "function_name");
    p->addressId          = LuaTableU64(L, 1, "address_id", 0);
    p->targetSymbol       = LuaTableString(L, 1, "target_symbol");
    p->targetLuaCfunction = LuaTableString(L, 1, "target_lua_cfunction");
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
    // Required for every mode except `mid` (which can capture raw
    // register/memory state without a typed entry signature). When
    // present, parse it now so the apply pass never re-parses.
    const std::string sigStr = LuaTableString(L, 1, "signature");
    if (sigStr.empty()) {
        if (p->mode != kcdx::hook_payload::Mode::Mid) {
            lua_pushnil(L);
            lua_pushfstring(L,
                "kcdx.hook '%s': mode='%s' requires a 'signature' "
                "(e.g. \"void (ptr, wstr szApp)\")",
                p->name.c_str(),
                kcdx::hook_payload::ModeToken(p->mode));
            return 2;
        }
    } else {
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

    // --- mode == mid: capture descriptors ---
    if (p->mode == kcdx::hook_payload::Mode::Mid) {
        lua_getfield(L, 1, "captures");
        if (lua_istable(L, -1)) {
            const int capIdx = lua_gettop(L);
            const int n = static_cast<int>(lua_objlen(L, capIdx));
            for (int i = 1; i <= n; ++i) {
                lua_rawgeti(L, capIdx, i);
                if (lua_isstring(L, -1)) {
                    p->captures.emplace_back(lua_tostring(L, -1));
                }
                lua_pop(L, 1);
            }
        }
        lua_pop(L, 1);
    }

    // --- Callback closure ---
    // Required for every mode (the interception runs Lua). Take a
    // GC-safe registry ref so the closure survives until the apply pass.
    p->callbackRef = TakeCallbackRef(L, 1, "callback");
    if (p->callbackRef == LUA_NOREF) {
        lua_pushnil(L);
        lua_pushfstring(L,
            "kcdx.hook '%s': missing required 'callback' function",
            p->name.c_str());
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
