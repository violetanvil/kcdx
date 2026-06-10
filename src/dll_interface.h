#pragma once

// kcdx::dll_interface — the engine-side impl of kcdxDllInterface (the C++
// mirror of the Lua kcdx.dll.declare surface). One Declare thunk writes each
// (name, signature) entry into the SAME declared-function store the Lua
// kcdx.dll.declare binder populates (lua_bind_functions::DeclareFunction) — one
// store, both surfaces. See include/kcdx/Interfaces.h for the public ABI
// contract.

#include "kcdx/Interfaces.h"

namespace kcdx::dll_interface {

// Return the engine-owned static kcdxDllInterface instance. Stable for the
// process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Dll, ...).
const kcdxDllInterface* GetInterface();

}  // namespace kcdx::dll_interface
