// kcdx.dev.* — Lua-side accessors for engine dev mode.
//
// Exposes:
//   kcdx.dev.is_enabled() -> bool
//   kcdx.dev.on_ready(fn) -> bool
//       Invokes `fn` immediately when kcdx.* is fully populated. By
//       construction this is always "now" if the script can call this
//       function (you need kcdx.dev to call it), so on_ready is sugar
//       for "wrap setup that requires kcdx in a clear intent marker."
//       Returns true if fn was invoked.

#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "dev.h"
#include "lua_bind.h"

namespace kcdx::lua_bind_dev {

namespace {

// kcdx.dev.is_enabled() -> bool
int Lua_IsEnabled(lua_State* L) {
    lua_pushboolean(L, kcdx::dev::IsEnabled() ? 1 : 0);
    return 1;
}

// kcdx.dev.on_ready(fn) -> bool
//
// Invokes fn() with no args if kcdx.* is fully populated. Returns true
// on invocation, false if not yet ready (caller should retry later
// e.g. on the next UIAction.RegisterEventSystemListener fire).
//
// Errors raised inside fn propagate up to the caller — caller can wrap
// in pcall if they want to swallow.
int Lua_OnReady(lua_State* L) {
    luaL_checktype(L, 1, LUA_TFUNCTION);
    if (!kcdx::lua_bind::IsKcdxGlobalReady()) {
        lua_pushboolean(L, 0);
        return 1;
    }
    // Call fn() — leaves any return values on the stack but we ignore.
    int top_before = lua_gettop(L) - 1;  // -1 to discount the fn itself
    lua_pushvalue(L, 1);
    lua_call(L, 0, 0);
    lua_settop(L, top_before);
    lua_pushboolean(L, 1);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"is_enabled", Lua_IsEnabled},
    {"on_ready",   Lua_OnReady},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global table
// on top of the stack. Creates the `dev` sub-table inside it. Stack
// effect: 0.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "dev");
}

}  // namespace kcdx::lua_bind_dev
