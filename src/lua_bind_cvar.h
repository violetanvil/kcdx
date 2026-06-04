#pragma once

// kcdx.cvar.* — Lua-facing CVar-read surface (get_int / get_bool / get_float).
// See lua_bind_cvar.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_cvar {

// Register the `kcdx.cvar` sub-table (get_int / get_bool / get_float) onto the
// kcdx table at the top of the Lua stack (a grouped capability domain, not a
// top-level verb). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_cvar
