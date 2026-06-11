#pragma once

// kcdx.behavior.* — the named-behavior Lua domain. See lua_bind_behavior.cpp
// for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_behavior {

// Register the kcdx.behavior domain table (declare / get / list) on the kcdx
// table at the top of the Lua stack — a GROUPED capability domain, built like
// kcdx.cvar.* / kcdx.assets.*. Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_behavior
