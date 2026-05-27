#pragma once

// kcdx.plugin.* — Lua-facing plugin introspection. See
// lua_bind_plugin.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_plugin {

// Register the `kcdx.plugin` capability DOMAIN (a sub-table, like
// kcdx.cosave.* / kcdx.dev.* — NOT a top-level verb) on the kcdx table
// at the top of the Lua stack. Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_plugin
