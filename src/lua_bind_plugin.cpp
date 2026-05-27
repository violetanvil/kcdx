// kcdx.plugin.* — Lua-side plugin introspection.
//
// A GROUPED capability DOMAIN (kcdx.plugin.*, like kcdx.cosave.* /
// kcdx.dev.*, NOT a top-level verb — plugin introspection is a query
// domain, not one of the closed-set core registration verbs).
//
// Today exposes ONE accessor:
//
//   kcdx.plugin.is_rejected(name) -> (bool, reason_or_nil)
//       Was the named plugin rejected by zone_gate this session?
//       `name` is the full prefixed plugin name "<author>.<plugin>"
//       (every plugin's identity is the two-component pair). Returns:
//         (true,  reason_string) -- rejected; reason teaches why
//         (false, nil)           -- not rejected (loaded normally,
//                                   or user-disabled, or unknown)
//         (nil,   teaching_err)  -- bad input (the kcdx-binder error
//                                   idiom; arg missing / wrong type /
//                                   empty)
//
// Useful for a plugin to degrade gracefully when a dependency it
// expected was rejected at load time (rather than crashing on a missing
// hook or silently doing nothing). The boolean is the predicate; the
// reason is the same teaching string the gate logged.
//
// Threading: pure read of zone_gate's in-process map; no callback
// is fired. Called from plugin.lua / require() in the main thread.
//
// Lua bridge: raw Lua C API, no static-const sentinel, no userdata, no
// registry refs. Returns are string + bool (no pointers, so the
// lua_Number=float precision caveat does not apply).

#include "lua_bind_plugin.h"

#include <string>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "zone_gate.h"

namespace kcdx::lua_bind_plugin {

namespace {

// kcdx.plugin.is_rejected(name) -> (bool, reason_or_nil)
//
// On a bad call returns (nil, teaching_error) — the kcdx-binder error
// idiom. The error names the field and the call shape so the author
// can fix it without consulting docs.
int Lua_IsRejected(lua_State* L) {
    // Arg 1 must be a string. luaL_checkstring would raise — we want
    // the soft (nil, err) return instead so authors can pcall this in
    // a guard without an unwind.
    if (lua_type(L, 1) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.plugin.is_rejected(name): `name` (string) is required "
            "— the full prefixed plugin name '<author>.<plugin>' "
            "(e.g. \"redmoon.outfit\"). Call shape: "
            "kcdx.plugin.is_rejected(\"author.plugin\")");
        return 2;
    }
    size_t len = 0;
    const char* s = lua_tolstring(L, 1, &len);
    if (!s || len == 0) {
        lua_pushnil(L);
        lua_pushstring(L,
            "kcdx.plugin.is_rejected(name): `name` must be non-empty — "
            "the full prefixed plugin name '<author>.<plugin>'. Call "
            "shape: kcdx.plugin.is_rejected(\"author.plugin\")");
        return 2;
    }

    const std::string name(s, len);
    const bool rejected = kcdx::zone_gate::IsRejected(name);
    lua_pushboolean(L, rejected ? 1 : 0);
    if (rejected) {
        // RejectReason returns a stable std::string (the gate's owned
        // reason text, recorded at evaluation time). Push it by
        // .c_str() — Lua copies the bytes into its own string.
        const std::string& reason = kcdx::zone_gate::RejectReason(name);
        lua_pushlstring(L, reason.data(), reason.size());
    } else {
        // Not rejected — second return is nil. Covers both "plugin
        // loaded normally" and "plugin is unknown / user-disabled":
        // either way zone_gate has no rejection on file. The predicate
        // is the source of truth; nil reason just means "no reason".
        lua_pushnil(L);
    }
    return 2;
}

const luaL_Reg kFunctions[] = {
    {"is_rejected", Lua_IsRejected},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global
// table on top of the stack. Creates the `plugin` sub-table inside
// it. Stack effect: 0. Built exactly like kcdx.dev.* / kcdx.test.*:
// lua_newtable + per-fn lua_pushcfunction/lua_setfield, then
// lua_setfield onto the kcdx table.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "plugin");
}

}  // namespace kcdx::lua_bind_plugin
