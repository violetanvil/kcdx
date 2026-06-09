#pragma once

// kcdx.functions.* + kcdx.dll.declare — the §9.3 function-reference value
// namespace + the author-declaration verb. See lua_bind_functions.cpp for the
// surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_functions {

// Register the `kcdx.functions` reference namespace (a lazy-resolving table)
// and the `kcdx.dll` domain (carrying `declare`) on the kcdx table at the top
// of the Lua stack, plus the function-reference-value userdata metatable in
// LUA_REGISTRYINDEX. Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_functions
