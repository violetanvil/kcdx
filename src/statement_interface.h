#pragma once

// kcdx::statement_interface — the engine-side impl of kcdxStatementInterface
// (the C++ mirror of the Lua kcdx.statement.* surface). Three registration
// thunks (ReplaceWith / InsertBefore / InsertAfter) each validate the C-ABI
// arguments, classify the op through the SAME per-op table the Lua
// constructors use (lua_bind_op::BuildOpView), and queue through the SAME
// Kind::Statement seam the Lua verbs feed (lua_bind_statement::QueueStatement)
// — one queue, one apply pass, both surfaces. Four query thunks walk the same
// registry handle space the Lua handle userdata wraps. See
// include/kcdx/Interfaces.h for the public ABI contract.

#include "kcdx/Interfaces.h"

namespace kcdx::statement_interface {

// Return the engine-owned static kcdxStatementInterface instance. Stable for
// the process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Statement, ...).
const kcdxStatementInterface* GetInterface();

}  // namespace kcdx::statement_interface
