#pragma once

// kcdx::functions_interface — the engine-side impl of kcdxFunctionsInterface
// (the C++ mirror of the Lua kcdx.functions.* surface). Three mint thunks
// (GameByName / GameById / PluginByName) each build a by-value kcdxFunctionRef
// by resolving the requested reference through the SAME refdb + declared-store
// path the Lua :resolve() accessor runs (lua_bind_functions::ResolveFunctionRef)
// — one store, both surfaces. See include/kcdx/Interfaces.h for the public ABI
// contract.

#include "kcdx/Interfaces.h"

namespace kcdx::functions_interface {

// Return the engine-owned static kcdxFunctionsInterface instance. Stable for
// the process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Functions, ...).
const kcdxFunctionsInterface* GetInterface();

}  // namespace kcdx::functions_interface
