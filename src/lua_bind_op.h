#pragma once

// kcdx.op.* — Lua-facing static-bytes op value namespace. See
// lua_bind_op.cpp for the surface contract.

#include <cstdint>
#include <string>
#include <vector>

extern "C" {
#include "lua.h"
}

#include "kcdx/Interfaces.h"  // kcdxOpKind — the C-ABI op catalog BuildOpView maps

namespace kcdx::lua_bind_op {

// Register the `kcdx.op` sub-table (a grouped capability DOMAIN, like
// kcdx.locator / kcdx.cvar) on the kcdx table at the top of the Lua stack, and
// register the op-value userdata metatable in LUA_REGISTRYINDEX. Stack
// effect: 0.
void bind(lua_State* L);

// An op value's externally-readable facts. Filled by ReadOp when the value at a
// stack index IS a kcdx.op.value userdata. The statement verb
// (kcdx.statement.replace_with) reads this to drive the apply-time byte-emit
// WITHOUT reaching into the anonymous-namespace OpDescriptor type — the same
// cross-binder seam ReadLocator / ReadFunctionRef provide for their values.
//
// The view carries the op's identity + classification so the apply path can:
//   - check the op KIND against the resolved statement kind (the teaching error
//     on mismatch), and
//   - for a DETERMINATE op, emit the exact bytes for (stmt_kind, byte_range_len)
//     via EmitFor below; for a DEFERRED op, surface the not-yet-emittable reason
//     rather than fabricating a guessed branch/call/operand byte (AP14).
struct OpView {
    // A stable label for the op (the descriptive primary name —
    // "replace_with_return", "always_take_branch"). For diagnostics + the
    // statement verb's logged attribution.
    std::string op_label;

    // The statement KIND this op requires, as the decoded refdb kind string
    // ("return"/"call"/"branch"/"assign"/"compare"), or "" when the op applies
    // to ANY statement kind (replace_with_noop). The required-kind human label
    // (for the teaching error) is required_label.
    std::string required_kind;    // "" == any.
    std::string required_label;   // e.g. "return", "conditional jump (branch)".

    // True when the op's byte-emit is determinate from (kind, byte_range_len)
    // alone — i.e. EmitFor returns real bytes. False for the five
    // statement-bytes-dependent ops (always_take_branch / invert_branch_condition
    // / replace_call_target / replace_assignment_value / replace_compare_constant):
    // their final bytes need the apply-time statement's actual bytes (a rel32
    // displacement, a call-target address, an operand encoding) which the
    // statement-resolution layer does not expose — the apply path surfaces the
    // deferral, never a fabricated byte.
    bool        emit_determinate = false;

    // Operands (read only for the kinds that carry them — has_value gates value;
    // target_fn is non-empty only for replace_call_target). Carried so a future
    // deferred-emit path has the operand without re-reading the userdata.
    bool        has_value = false;
    int64_t     value = 0;
    std::string target_fn;
};

// If the value at `idx` is a kcdx.op.value userdata, fill `out` and return true;
// otherwise return false (leaves `out` untouched, raises nothing). The
// arg-type dispatch the statement verb runs on its required `op` positional.
bool ReadOp(lua_State* L, int idx, OpView& out);

// Build the OpView for an op identified by its C-ABI kind tag (a kcdxOp from
// the C++ kcdxStatementInterface) — the C++ surface's entry into the SAME
// per-op classification table the Lua constructors use (required statement
// kind, teaching label, determinate-vs-deferred emit class, which kinds carry
// which operand). ONE table, both surfaces — the C++ path never re-derives or
// duplicates the classification. `value` is read only by the value-carrying
// kinds; `targetFn` only by ReplaceCallTarget (REQUIRED there — its absence is
// the same author bug the Lua constructor raises on). Returns true and fills
// `out` on success; returns false and fills `err_out` with the teaching reason
// on an unrecognized kind value or a missing required operand — the caller
// fails loud at its own surface, never a silent default.
bool BuildOpView(kcdxOpKind kind, long long value, const char* targetFn,
                 OpView& out, std::string& err_out);

// True iff `stmtKind` (a decoded refdb statement kind string) satisfies the op's
// required kind. `view.required_kind == ""` (any) always satisfies. This is the
// kind-mismatch gate the statement verb runs at registration — a mismatch is a
// loud teaching error naming the actual + required kind (AP14).
bool OpKindSatisfies(const OpView& view, const std::string& stmtKind);

// Emit the DETERMINATE bytes for `view` over a statement of `byteRangeLen`
// bytes. Defined for an op whose emit_determinate is true; returns the exact
// byte sequence (NOT NOP-padded to the range — the caller pads/decides
// same-size vs trampoline). For a deferred op this returns an empty vector
// (the caller must check emit_determinate first and surface the deferral). The
// emit is the SAME logic :emit_for exposes to Lua, shared by the apply path so
// the on-disk byte sequence the test asserts and the byte sequence the engine
// writes are one source of truth.
std::vector<uint8_t> EmitDeterminate(const OpView& view, int64_t byteRangeLen);

}  // namespace kcdx::lua_bind_op
