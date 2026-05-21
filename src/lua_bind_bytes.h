#pragma once

// kcdx.bytes Lua binding. See lua_bind_bytes.cpp for the surface
// contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_bytes {

// Register `kcdx.bytes` on the table at the top of the Lua stack
// (which lua_bind.cpp places there before calling sub-binders).
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_bytes
