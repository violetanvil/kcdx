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

#include <string>

#include "kcdx/Interfaces.h"
#include "refdb.h"  // refdb::StatementLocator (ConvertLocator's output type)

namespace kcdx::statement_interface {

// Return the engine-owned static kcdxStatementInterface instance. Stable for
// the process lifetime; consumed by interfaces.cpp's
// Thunk_QueryInterface(kcdxInterface_Statement, ...).
const kcdxStatementInterface* GetInterface();

// kcdxLocator → refdb::StatementLocator — the C-ABI locator's ONE conversion
// home, shared by the statement registration thunks here and by the hook
// interface's insert thunks (src/hook_interface.cpp). A pure field mapping:
// each kind copies its own operand(s). Returns false + a teaching reason on
// an unrecognized kind value or a missing required operand — fail loud,
// never a silently-defaulted locator.
bool ConvertLocator(const kcdxLocator& in, refdb::StatementLocator& out,
                    std::string& err);

// Collapse a target-by-reference kcdxFunctionRef to its carried name — the
// ONE home of the ref-as-target semantics every C-ABI verb family shares
// (the statement verbs' opts->targetRef AND the hook verbs' opts->targetRef).
// A found=false reference or a nameless (GameById) reference returns false +
// a teaching reason — a loud registration error at the caller, never a
// silent fallback to the positional string.
bool CollapseTargetRef(const kcdxFunctionRef& ref, std::string& nameOut,
                       std::string& err);

}  // namespace kcdx::statement_interface
