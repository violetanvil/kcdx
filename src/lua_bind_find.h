#pragma once

// kcdx.find{...} — Lua-facing dev-time function discovery. See lua_bind_find.cpp
// for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_find {

// Register `kcdx.find` as a top-level verb on the kcdx table at the top of the
// Lua stack (a domain verb taking the at-least-one-of-N criteria table —
// registered directly on the kcdx table, like kcdx.scan). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_find
