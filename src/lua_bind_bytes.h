#pragma once

// kcdx.bytes Lua binding. See lua_bind_bytes.cpp for the surface
// contract.

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_bind_bytes {

// Register the Kind::Bytes deferred-apply handler with kcdx::lua_registry.
// Engine state, not Lua-surface state: called at ENGINE INIT (dllmain.cpp,
// before DiscoverAndLoad) so the handler exists before any C++ plugin's
// kcdxPlugin_Load can queue a Kind::Bytes entry (the future
// kcdxBytesInterface). The Lua VM need not exist yet — registration only
// stores a function pointer. Split out of bind() for the same lifecycle
// reason as the Kind::Hook handler. See docs/known-issues/cap-36.
void RegisterHandlers();

// Register `kcdx.bytes` on the table at the top of the Lua stack (the
// Lua-surface wiring only — the apply handler is registered separately by
// RegisterHandlers() at engine init).
void bind(lua_State* L);

}  // namespace kcdx::lua_bind_bytes
