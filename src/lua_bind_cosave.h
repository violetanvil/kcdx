#pragma once

// kcdx.cosave.* — Lua-facing save/load persistence. See
// lua_bind_cosave.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_cosave {

// Register the `kcdx.cosave` capability DOMAIN (a sub-table, like
// kcdx.log.* / kcdx.console.*, per .claude/rules/lua-api-surface.md —
// NOT a top-level verb) on the kcdx table at the top of the Lua stack.
// Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_cosave
