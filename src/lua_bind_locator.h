#pragma once

// kcdx.locator.* — Lua-facing locator value namespace. See
// lua_bind_locator.cpp for the surface contract.

#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_locator {

// Register the `kcdx.locator` sub-table (a grouped capability DOMAIN, like
// kcdx.cvar / kcdx.test) on the kcdx table at the top of the Lua stack, and
// register the locator-value userdata metatable in LUA_REGISTRYINDEX. Stack
// effect: 0.
void bind(lua_State* L);

// A locator value's externally-readable shape, for the hook / statement verbs
// that accept a `kcdx.locator.*` value as their optional `[locator]` positional.
// `is_function_entry` is true for kcdx.locator.function_entry() — the ONE
// locator the function-entry hook path already honors (it means "the function
// entry", the existing default). Every other kind is a STATEMENT-level locator
// the function-entry apply path cannot consume yet; `kind_label` names it for a
// teaching diagnostic.
struct LocatorView {
    bool        is_function_entry = false;
    std::string kind_label;       // e.g. "first_call_to", "function_exit".
};

// If the value at `idx` is a kcdx.locator.value userdata, fill `out` and return
// true; otherwise return false (leaves `out` untouched, raises nothing). The
// arg-type dispatch a hook/statement verb runs on its optional locator slot.
bool ReadLocator(lua_State* L, int idx, LocatorView& out);

}  // namespace kcdx::lua_bind_locator
