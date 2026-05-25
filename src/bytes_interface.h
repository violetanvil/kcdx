#pragma once

// kcdx::bytes_interface — the engine-side impl of kcdxBytesInterface (the C++
// mirror of the Lua kcdx.bytes surface). A single Register method builds a
// patch::PatchEntry from the caller's kcdxBytesOptions — same payload shape,
// same exactly-one-locator rule, same target="<name>" name-resolution, same
// Kind::Bytes lua_registry::Entry + deferred apply pass — as the Lua binder
// (src/lua_bind_bytes.cpp), but taking raw C inputs from a C++ DLL plugin via
// the kcdxBytesInterface vtable instead of a Lua table. The four query thunks
// (IsApplied/GetReason/GetName/Uninstall) walk the registry by handleId — same
// shape as Lua's handle:applied()/:reason()/:name()/:uninstall(). Uninstall is
// a no-op-with-teaching-log: a byte rewrite has no revert path (the original
// bytes are not retained for restore). See include/kcdx/Interfaces.h:1648-1816
// for the public ABI contract.
//
// Phase 3 sub-2 step 2. Mirrors src/hook_interface.{h,cpp} exactly so
// interfaces.cpp wires it identically (Thunk_QueryInterface case).

#include "kcdx/Interfaces.h"

namespace kcdx::bytes_interface {

// Return the engine-owned static kcdxBytesInterface instance. Stable for the
// process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Bytes, ...).
const kcdxBytesInterface* GetInterface();

}  // namespace kcdx::bytes_interface
