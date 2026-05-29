#pragma once

// kcdx::declare_interface — the engine-side impl of kcdxDeclareInterface (the
// C++ mirror of the Lua kcdx.declare / kcdx.declared surface). Two methods:
// Declare(...) writes a per-version named target into the declared-targets
// store (the SAME store the Lua binder writes to — declared entries are
// indistinguishable by source); Get(name) reads a declared VALUE entry's
// payload (PATTERN entries surface through the hook / bytes verbs instead).
// See include/kcdx/Interfaces.h for the public ABI contract.
//
// Same shape as src/hook_interface.h + src/bytes_interface.h so
// interfaces.cpp's Thunk_QueryInterface case wires it identically. The
// declared-targets store + the address_library AuthorTarget integration are
// the data layer this interface fronts; this module is the C++ DLL-facing
// front door.

#include "kcdx/Interfaces.h"

namespace kcdx::declare_interface {

// Return the engine-owned static kcdxDeclareInterface instance. Stable for
// the process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Declare, ...).
const kcdxDeclareInterface* GetInterface();

}  // namespace kcdx::declare_interface
