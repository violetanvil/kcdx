// kcdx.dev.* — Lua-side accessors for engine dev mode.
//
// Currently only exposes is_enabled(). Pak Lua scripts that ship as test-
// suite plugins use this to early-out when dev mode is off, mirroring the
// engine-side test_suite_only TOML gate (which doesn't apply to pak
// scripts because they don't have a TOML).

#include <cstdint>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "dev.h"

namespace kcdx::lua_bind_dev {

namespace {

// kcdx.dev.is_enabled() -> bool
int Lua_IsEnabled(lua_State* L) {
    lua_pushboolean(L, kcdx::dev::IsEnabled() ? 1 : 0);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"is_enabled", Lua_IsEnabled},
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
