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

#include "dev.h"
#include "log.h"

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
    uintptr_t  fn_addr   = (uintptr_t)fn;
    int        arg_type  = lua_type(L, 1);
    const void* arg_objp = lua_topointer(L, 1);

    KCDX_DEV("LUA", "CFUNCTION_ADDR/enter",
        kcdx::dev::KV("L",          (const void*)L),
        kcdx::dev::KV("arg_type",   arg_type),
        kcdx::dev::KV("arg_topointer", arg_objp),
        kcdx::dev::KV("tocfunction",   (const void*)fn));

    lua_pushinteger(L, (lua_Integer)fn_addr);

    // Readback what we just pushed via both lua_tointeger and lua_tonumber
    // so we can see if the value is the same as what we put in.
    lua_Integer back_i = lua_tointeger(L, -1);
    lua_Number  back_n = lua_tonumber(L, -1);
    KCDX_DEV("LUA", "CFUNCTION_ADDR/readback",
        kcdx::dev::KV("pushed_hex",     (uintptr_t)fn_addr),
        kcdx::dev::KV("readback_int_hex", (uintptr_t)back_i),
        kcdx::dev::KV("readback_num",   (double)back_n),
        kcdx::dev::KV("readback_num_as_hex", (uintptr_t)(uintptr_t)back_n));
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
