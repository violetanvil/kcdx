#pragma once

// kcdx::asset_interface — the engine-side impl of kcdxAssetInterface (the C++
// mirror of the Lua kcdx.assets.* surface). Five methods: GetByPath (pure read
// of the caller's own asset), GetByName (read a name the caller published),
// Declare (publish a name), Register (write a runtime overlay), Replace (write
// a runtime replacement keyed by a vanilla vpath or a packed cross-mod name).
// See include/kcdx/Interfaces.h for the public ABI contract.
//
// Same shape as src/declare_interface.h + src/hook_interface.h so
// interfaces.cpp's Thunk_QueryInterface case wires it identically. Each thunk
// resolves the calling plugin from the `self` handle, then calls the IDENTICAL
// shared helpers the Lua binder calls (lua_bind_assets::ResolveAssetPath +
// the asset_namespace runtime stores) — one shared resolution path, no
// parallel C++ copy (both languages call the shared helpers). The disk path is
// returned as a const char* (nullptr on failure); the teaching error is logged
// (the same LOG_*_KV lines the Lua binder emits), never handed back in code.

#include "kcdx/Interfaces.h"

namespace kcdx::asset_interface {

// Return the engine-owned static kcdxAssetInterface instance. Stable for the
// process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Assets, ...).
const kcdxAssetInterface* GetInterface();

}  // namespace kcdx::asset_interface
