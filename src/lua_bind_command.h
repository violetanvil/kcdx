#pragma once

// kcdx.command{...} — Lua-facing console-command registration. See
// lua_bind_command.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_command {

// Register `kcdx.command` as a top-level verb on the kcdx table at the top
// of the Lua stack (a core authoring verb — registered directly, not in a
// sub-table). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_command
