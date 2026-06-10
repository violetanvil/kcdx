#pragma once

// kcdx.statement.* — Lua-facing static-bytes modification namespace. See
// lua_bind_statement.cpp for the surface contract.

#include <cstdint>
#include <string>

extern "C" {
#include "lua.h"
}

#include "lua_bind_op.h"  // OpView — the op identity+classification the payload carries
#include "refdb.h"        // refdb::StatementLocator — the statement selector

namespace kcdx::lua_bind_statement {

// Register the Kind::Statement deferred-apply handler. ENGINE state (not
// Lua-surface state), so it is appliable regardless of which surface queued the
// entry — registered at engine init (dllmain.cpp), BEFORE plugin discovery, the
// same lifecycle as lua_bind_hook / lua_bind_bytes RegisterHandlers().
void RegisterHandlers();

// Register the `kcdx.statement` sub-table (a grouped capability DOMAIN of
// sub-verbs: replace_with / insert_before / insert_after) on the kcdx table at
// the top of the Lua stack. Stack effect: 0.
void bind(lua_State* L);

// A statement-modification registration — the surface-independent intent BOTH
// authoring surfaces queue: the Lua kcdx.statement.* verbs and the C++
// kcdxStatementInterface thunks each validate their own arguments, fill one of
// these, and hand it to QueueStatement below. It is also the queued payload
// the Kind::Statement apply handler consumes — ONE shape from registration to
// apply, no per-surface payload type.
//
// A replace_with intent carries hasOp=true + the op's OpView (identity + kind
// contract + determinate/deferred classification + operands — read via
// lua_bind_op::ReadOp on the Lua path, lua_bind_op::BuildOpView on the C++
// path, both projecting the SAME classification table). An insert intent
// carries insertPending=true and fails loud at apply (the statement-locator
// capture-thunk apply path is unwired on both surfaces — an honest deferral,
// never faked green).
struct StatementRegistration {
    std::string name;          // REQUIRED non-empty (each surface synthesizes
                               // its own default before queueing).
    std::string description;
    std::string module;

    std::string owningAuthor;  // 2-dot identity (attribution; may be empty).
    std::string owningPlugin;

    // The statement selector — resolved at apply time against the curated DB.
    std::string targetName;               // the curated function name.
    refdb::StatementLocator locator;      // defaults to FunctionEntry.

    // The op (replace_with only), carried as the cross-binder OpView so the
    // apply path emits without re-reading the originating surface's value.
    kcdx::lua_bind_op::OpView op;
    bool        hasOp = false;

    // insert_before / insert_after: the capture-thunk apply path is unwired;
    // the entry fails loud at apply.
    bool        insertPending = false;

    // Registration call site, for the queue log line ("" / 0 on the C++
    // surface — a compiled DLL has no script call site to report).
    std::string callSiteFile;
    int         callSiteLine = 0;
};

// Queue a statement registration as a Kind::Statement deferred-apply entry and
// return the registry handle id (0 + err_out filled on failure). THE single
// queue seam both surfaces share: payload construction + registry append live
// here only — the Lua verbs and the C++ thunks never build a registry entry
// themselves, so the two surfaces cannot drift. The returned id is the value
// the Lua handle userdata wraps and the C++ kcdxStatementHandle carries (one
// handle space, one registry).
uint64_t QueueStatement(const StatementRegistration& reg, std::string* err_out);

}  // namespace kcdx::lua_bind_statement
