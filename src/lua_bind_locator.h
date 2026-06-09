#pragma once

// kcdx.locator.* — Lua-facing locator value namespace. See
// lua_bind_locator.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_locator {

// Register the `kcdx.locator` sub-table (a grouped capability DOMAIN, like
// kcdx.cvar / kcdx.test) on the kcdx table at the top of the Lua stack, and
// register the locator-value userdata metatable in LUA_REGISTRYINDEX. Stack
// effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_locator
