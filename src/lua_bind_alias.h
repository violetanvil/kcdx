#pragma once

// kcdx.alias(short, "plugin.name") — declare a per-plugin local name handle.
// See lua_bind_alias.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_alias {

// Register `kcdx.alias` as a top-level verb on the kcdx table at the top of
// the Lua stack (a "doing" positional call — registered directly, not in a
// sub-table). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_alias
