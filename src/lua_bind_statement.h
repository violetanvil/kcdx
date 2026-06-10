#pragma once

// kcdx.statement.* — Lua-facing static-bytes modification namespace. See
// lua_bind_statement.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_statement {

// Register the Kind::Statement deferred-apply handler. ENGINE state (not
// Lua-surface state), so it is appliable regardless of which surface queued the
// entry — registered at engine init (dllmain.cpp), BEFORE plugin discovery, the
// same lifecycle as lua_bind_hook / lua_bind_bytes RegisterHandlers().
void RegisterHandlers();

// Register the `kcdx.statement` sub-table (a grouped capability DOMAIN of
// sub-verbs: replace_with / insert_before / insert_after) on the kcdx table at
// the top of the Lua stack. Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_statement
