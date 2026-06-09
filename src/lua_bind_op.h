#pragma once

// kcdx.op.* — Lua-facing static-bytes op value namespace. See
// lua_bind_op.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_op {

// Register the `kcdx.op` sub-table (a grouped capability DOMAIN, like
// kcdx.locator / kcdx.cvar) on the kcdx table at the top of the Lua stack, and
// register the op-value userdata metatable in LUA_REGISTRYINDEX. Stack
// effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_op
