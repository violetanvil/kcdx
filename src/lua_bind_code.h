#pragma once

// kcdx.code{...} — Lua-facing code/trampoline allocation. See
// lua_bind_code.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_code {

// Register `kcdx.code` as a top-level verb on the kcdx table at the top
// of the Lua stack (a core authoring verb per
// .claude/rules/lua-api-surface.md — registered directly, not in a
// sub-table). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_code
