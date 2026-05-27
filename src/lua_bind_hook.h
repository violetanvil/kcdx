#pragma once

// kcdx.hook Lua binding. See lua_bind_hook.cpp for the surface
// contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_hook {

// Register the Kind::Hook deferred-apply handler with kcdx::lua_registry.
// Engine state, not Lua-surface state: called at ENGINE INIT (dllmain.cpp,
// before DiscoverAndLoad) so the handler exists before any C++ plugin's
// kcdxPlugin_Load can queue a Kind::Hook entry via kcdxHookInterface. The
// Lua VM need not exist yet — registration only stores a function pointer.
// Split out of bind() because bind() runs too late (first-update-tick) for
// the C++ Load-time caller.
void RegisterHandlers();

// Register `kcdx.hook` on the table at the top of the Lua stack (the
// Lua-surface wiring only — the apply handler is registered separately by
// RegisterHandlers() at engine init).
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_hook
