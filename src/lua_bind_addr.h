#pragma once

// kcdx.addr.* — Address Library names exposed as Lua-visible pointer
// userdata. See lua_bind_addr.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_addr {

// Populate the kcdx.addr table on top of the Lua stack with one
// pointer-userdata entry per resolvable Address Library row. Caller
// is expected to have created an empty `kcdx` table on top of the
// stack and to call this from within RegisterKcdxTable's sub-binder
// cascade.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_addr
