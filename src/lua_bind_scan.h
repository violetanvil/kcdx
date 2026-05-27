#pragma once

// kcdx.scan{...} — Lua-facing diagnostic AOB scan. See lua_bind_scan.cpp
// for the surface contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_scan {

// Register `kcdx.scan` as a top-level verb on the kcdx table at the top
// of the Lua stack (a core authoring verb — registered directly, not in a
// sub-table, like kcdx.code / kcdx.hook). Stack effect: 0.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_scan
