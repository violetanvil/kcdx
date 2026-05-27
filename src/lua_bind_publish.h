#pragma once

// kcdx.publish(event, payload) — Lua-facing cross-plugin pub/sub broadcast.
// See lua_bind_publish.cpp for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_publish {

// Register `kcdx.publish` as a top-level verb on the kcdx table at the top
// of the Lua stack (a core authoring verb — the broadcast counterpart to
// kcdx.on, registered directly, not in a sub-table). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_publish
