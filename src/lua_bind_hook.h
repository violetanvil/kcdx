#pragma once

// kcdx.hook Lua binding. See lua_bind_hook.cpp for the surface
// contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_hook {

// Register `kcdx.hook` on the table at the top of the Lua stack.
// Also registers the per-Kind apply handler with kcdx::lua_registry.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_hook
