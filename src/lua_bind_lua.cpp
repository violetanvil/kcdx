// kcdx.lua.* — Lua-VM introspection helpers.
//
// Phase 5c.7d ships a single function: kcdx.lua.cfunction_address.
// More may follow (kcdx.lua.cfunction_for_address, _registry_dump, etc.)
// but this is the load-bearing one.
//
// What this enables: combined with kcdx.memory.dynamic_hook, pak Lua
// can now hook the C function backing a registered Lua callable.
//
//   local addr = kcdx.lua.cfunction_address(System.LogAlways)
//   kcdx.memory.dynamic_hook({
//       name   = "log_intercept",
//       target = addr,
//       ...
//   })
//
// Why it has to live on the C side: lua_tocfunction is a C-API-only
// function (the Lua-side `tostring(fn)` returns "function: 0x..."
// but that address is the lua_State-internal callable representation,
// not the underlying C function pointer). kcdx, sitting at the C
// side, can call lua_tocfunction; pak Lua cannot.

#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace kcdx::lua_bind_lua {

namespace {

// kcdx.lua.cfunction_address(fn) -> integer VA, or (nil, errmsg).
int Lua_CFunctionAddress(lua_State* L) {
    if (!lua_iscfunction(L, 1)) {
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.lua.cfunction_address: argument 1 is "
                           "not a C function (lua_iscfunction returned false)");
        return 2;
    }
    lua_CFunction fn = lua_tocfunction(L, 1);
    if (!fn) {
        // Shouldn't reach here given the iscfunction check, but defensive.
        lua_pushnil(L);
        lua_pushliteral(L, "kcdx.lua.cfunction_address: lua_tocfunction "
                           "returned null");
        return 2;
    }
    lua_pushinteger(L, (lua_Integer)(uintptr_t)fn);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"cfunction_address", Lua_CFunctionAddress},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable, with the kcdx global
// table on top of the stack. Creates the `lua` sub-table inside it.
// Stack effect: 0.
void bind(lua_State* L) {
    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "lua");
}

}  // namespace kcdx::lua_bind_lua
