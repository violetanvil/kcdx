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
//
// The registered `kcdx.plugin` table carries BOTH:
//   - the function members (is_rejected) — the query surface, callable
//     as kcdx.plugin.is_rejected("author.plugin").
//   - an __index metamethod — the navigable cross-plugin namespace
//     resolver (kcdx.plugin.<author>.<plugin> resolves each dotted
//     segment to an engine-side handle). The function members shadow the
//     __index (a raw-table hit on `is_rejected` never reaches the
//     metamethod), so the two surfaces coexist.
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_plugin
