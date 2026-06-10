#pragma once

// kcdx.locator.* — Lua-facing locator value namespace. See
// lua_bind_locator.cpp for the surface contract.

#include <string>

extern "C" {
#include "lua.h"
}

// Forward-declare the locator descriptor the value carries (defined in refdb.h);
// the descriptor accessor below returns a pointer into the live userdata.
namespace kcdx::refdb { struct StatementLocator; }

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

// If the value at `idx` is a kcdx.locator.value userdata, return a pointer to
// the FULL refdb::StatementLocator descriptor it carries (the kind + every
// operand the statement-resolution layer needs); nullptr otherwise. ReadLocator
// surfaces only the kind label + is_function_entry flag (enough to disambiguate
// a positional + render a teaching error); the statement verb's APPLY path needs
// the whole descriptor to call refdb::ResolveStatementByName, so this returns it
// directly. The pointer is into the live userdata — valid only while the value
// stays on the stack at `idx`; the caller copies it (a value copy) before the
// stack changes. Reads nothing beyond the metatable identity.
const kcdx::refdb::StatementLocator* ReadLocatorDescriptor(lua_State* L, int idx);

}  // namespace kcdx::lua_bind_locator
