// kcdx.op.* — the §9.3 static-bytes op value namespace.
//
// An op value says WHAT static change to make at a located statement — by NAME
// (never_take_branch, replace_with_noop), never a byte sequence the author
// hand-writes (cornerstones.md — the disassembler test, by construction). This
// is an ADDITIVE namespace: kcdx.statement.replace_with (step 5) consumes these
// values, but the namespace stands alone and SELF-VERIFIES here via the
// `:emit_for(kind, byte_range_len)` introspection accessor (the settled fork,
// mirroring kcdx.locator's `:resolve` — see DESIGN NOTES). Each kcdx.op.* call
// returns an OP VALUE (a userdata carrying an OpDescriptor) the statement verb
// accepts.
//
//   -- return / function-level
//   kcdx.op.replace_with_return(value)   -- alias return_const(value)
//   kcdx.op.replace_return_value(value)
//   -- whole-statement neutralize
//   kcdx.op.replace_with_noop            -- alias noop
//   -- call statements
//   kcdx.op.skip_call_void
//   kcdx.op.skip_call_return_value(value)
//   kcdx.op.replace_call_target(new_fn_name)
//   -- branch (conditional-jump) statements
//   kcdx.op.always_take_branch
//   kcdx.op.never_take_branch
//   kcdx.op.invert_branch_condition
//   -- assignment / compare statements
//   kcdx.op.replace_assignment_value(value)
//   kcdx.op.replace_compare_constant(value)
//
// THE ENGINE PICKS same-size byte rewrite vs trampoline at APPLY TIME (step 5's
// kcdx.statement verb), from op-bytes-vs-statement-range — the reference-DB
// statements.byte_range_len of the resolved statement (00-original-plan.md §9.3:
// "the engine picks same-size byte rewrite vs trampoline based on
// op-bytes-vs-statement-range from the SQLite statements.byte_range_len.
// Authors never see a 'doesn't fit' failure"). An op value at THIS step carries
// the op KIND + its operand(s) + a byte-emit DESCRIPTOR; it does NOT resolve a
// statement or compute the same-size/trampoline pick itself. The pick is the
// statement verb's job at apply time. This step builds the op VALUES + their
// byte-emit + the registration-time/emit-time kind check.
//
// THE KIND CHECK + THE TEACHING ERROR. Each op declares the statement KIND(s)
// it applies to (a branch op requires a conditional-jump statement; a call op
// requires a call statement; a return op requires a return statement). Handed a
// statement of the wrong kind, the op fails LOUD with a teaching error NAMING
// the actual kind — 00-original-plan.md §9.3's example verbatim:
// "always_take_branch requires a conditional jump statement; this statement is
// a `call`" (cornerstones.md errors-that-teach, AP14 — never a silent drop). It
// does NOT gate on semantic-purpose correctness ("you replaced the damage calc
// with always-zero, did you mean that?" — author's call, not the engine's).
//
// THE :emit_for(kind, byte_range_len) ACCESSOR (the settled fork — the seam the
// test asserts against, mirroring kcdx.locator's `:resolve`). Every op value
// carries a Lua method that, given a target statement's KIND + byte span,
// returns a result table. It is the self-check seam (the op self-verifies with
// NO statement verb applying it) AND a useful author introspection surface:
//
//   local r = kcdx.op.replace_with_noop:emit_for("call", 5)
//   -- r.kind_ok       : bool   — true when the op applies to this stmt kind
//   -- r.reason        : string — on kind_ok==false, the teaching error
//   --                            (names the actual kind + the required kind)
//   -- r.deferred      : bool   — true when final bytes need the apply-time
//   --                            statement bytes (a branch displacement, a
//   --                            call target, an assignment/compare encoding);
//   --                            false when the emit is determinate from
//   --                            kind+range alone
//   -- r.bytes         : {int,...} | nil — the emitted byte sequence, present
//   --                            ONLY when deferred==false (a determinate emit);
//   --                            nil when deferred==true (NOT fabricated —
//   --                            emitting a guessed branch/call/operand byte
//   --                            would be an AP14-class silent defect)
//   -- r.fit           : string | nil — "same_size" | "trampoline" — the pick
//   --                            the statement verb WILL make for this op at
//   --                            this byte_range_len (present when bytes is)
//
// WHY SOME OPS DEFER. Five ops cannot emit final bytes from kind+range alone:
//   * always_take_branch / invert_branch_condition — need the original Jcc's
//     displacement/opcode (to preserve the target / flip the condition);
//   * replace_call_target — needs the original call site AND the resolved
//     address of new_fn_name (a rel32 to a target not known at this step);
//   * replace_assignment_value / replace_compare_constant — need the original
//     instruction's destination/operand encoding (where the constant sits).
// These carry the descriptor; the byte-emit runs at apply time (step 5) on the
// resolved statement's actual bytes. `:emit_for` reports deferred=true + bytes=
// nil for them — the test asserts the DEFERRAL + the kind check, never a
// fabricated byte (results-driven.md / AP14: a wrong byte emit is a silent
// defect; do not guess what the apply-time path computes).
//
// The determinate ops (replace_with_noop / skip_call_void /
// skip_call_return_value / replace_with_return / replace_return_value) emit a
// statement-bytes-INDEPENDENT sequence the test asserts EXACTLY:
//   * noop over an N-byte range          -> N × 0x90 (one-byte NOP each).
//   * skip_call_void over N bytes        -> N × 0x90 (the call is NOP'd out).
//   * replace_with_return(v) / replace_return_value(v) / skip_call_return_value(v)
//     -> set the return register to v (mov eax, imm32 = B8 <imm32-le>) and, for
//        the return-producing ops, a near ret (C3); the remainder of the range
//        is NOP-padded. v==0 uses the canonical xor eax,eax (31 C0) over
//        mov eax,0, then pads. (32-bit eax write zero-extends rax — the SysV/MS
//        x64 int-return convention; a non-int return ABI is out of scope for the
//        catalog's integer `value`.)
//
// DESIGN NOTES:
//   * The op NAMES the behavior (never_take_branch, not a byte sequence). No
//     author hex on the surface — the disassembler test passes by construction
//     (cornerstones.md / AP12). There is no expert raw-bytes op in this catalog;
//     a raw byte rewrite is kcdx.bytes, a different surface.
//   * Lua bridge (lua-bridge.md): raw Lua C API only. The op value is a raw
//     lua_newuserdata + a metatable registered with luaL_newmetatable. NO
//     kcdx-side static-const sentinel in any GCObject; the frealloc canary
//     (PROBE Q) stays zero.
//   * Lua precision (lua-precision.md): the op's constant `value`,
//     byte_range_len, and emitted bytes are small integers (a constant operand,
//     a byte span, byte values 0..255), NOT pointers — pushed via
//     lua_pushinteger. No VA crosses this surface, so the pointer-precision
//     hazard does not apply. (replace_call_target's target is a NAME string,
//     resolved to an address at apply time, never a VA on this surface.)
//   * Fail loud (AP14): a bad-arg constructor raises at the call site; a
//     kind-mismatch on :emit_for returns kind_ok=false + the teaching reason,
//     never a silent empty.

#include "lua_bind_op.h"

#include <string>
#include <vector>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

namespace kcdx::lua_bind_op {

namespace {

// The op-value userdata metatable name (LUA_REGISTRYINDEX key). Stable
// identifier, same convention as kcdx.locator.value.
constexpr const char* kOpMetatable = "kcdx.op.value";

// The op KIND — one per catalog entry (the descriptive primary name; aliases
// map to the same kind).
enum class OpKind {
    ReplaceWithReturn,      // replace_with_return(value) / return_const(value)
    ReplaceReturnValue,     // replace_return_value(value)
    ReplaceWithNoop,        // replace_with_noop / noop
    SkipCallVoid,           // skip_call_void
    SkipCallReturnValue,    // skip_call_return_value(value)
    ReplaceCallTarget,      // replace_call_target(new_fn_name)
    AlwaysTakeBranch,       // always_take_branch
    NeverTakeBranch,        // never_take_branch
    InvertBranchCondition,  // invert_branch_condition
    ReplaceAssignmentValue, // replace_assignment_value(value)
    ReplaceCompareConstant, // replace_compare_constant(value)
};

// The op-value payload. A descriptor: the kind + its operand(s). value_ is read
// only by the kinds that take a constant (has_value_ gates it); target_fn_ only
// by ReplaceCallTarget. Carries a std::string member → placement-new on push +
// explicit dtor at __gc (same lifecycle as kcdx.locator.value).
struct OpDescriptor {
    OpKind      kind = OpKind::ReplaceWithNoop;
    bool        has_value = false;
    int64_t     value = 0;       // the constant operand, when has_value.
    std::string target_fn;       // ReplaceCallTarget's new_fn_name.
};

OpDescriptor* CheckOp(lua_State* L, int idx) {
    return static_cast<OpDescriptor*>(luaL_checkudata(L, idx, kOpMetatable));
}

int Lua_OpGc(lua_State* L) {
    auto* op = CheckOp(L, 1);
    op->~OpDescriptor();
    return 0;
}

// ---- the kind contract --------------------------------------------------

// The statement KIND an op requires (matched against a resolved statement's
// decoded statements.kind — "call"/"return"/"branch"/"assign"/… — refdb.h
// StatementResolution.kind). FunctionLevel ops (noop) apply to ANY statement.
enum class RequiredKind {
    Any,       // applies to any statement (replace_with_noop).
    Return,    // a return statement.
    Call,      // a call statement.
    Branch,    // a conditional-jump statement.
    Assign,    // an assignment statement.
    Compare,   // a compare statement.
};

// Static per-op facts: the required statement kind, a human label for the
// required kind (for the teaching error), and whether the byte-emit is
// determinate from kind+range alone (false → the final bytes need the
// apply-time statement bytes; :emit_for reports deferred + bytes=nil).
struct OpContract {
    RequiredKind required;
    const char*  required_label;   // names the kind in the teaching error.
    bool         emit_determinate; // true → bytes computable from kind+range.
};

OpContract ContractFor(OpKind k) {
    switch (k) {
        case OpKind::ReplaceWithReturn:
            return { RequiredKind::Return, "return", true };
        case OpKind::ReplaceReturnValue:
            return { RequiredKind::Return, "return", true };
        case OpKind::ReplaceWithNoop:
            return { RequiredKind::Any, "any statement", true };
        case OpKind::SkipCallVoid:
            return { RequiredKind::Call, "call", true };
        case OpKind::SkipCallReturnValue:
            return { RequiredKind::Call, "call", true };
        case OpKind::ReplaceCallTarget:
            // Needs the original call site + the resolved address of the new
            // target name (a rel32 to an address not known at this step).
            return { RequiredKind::Call, "call", false };
        case OpKind::AlwaysTakeBranch:
            // Needs the original Jcc displacement (preserve the branch target).
            return { RequiredKind::Branch, "conditional jump (branch)", false };
        case OpKind::NeverTakeBranch:
            // Fall through = neutralize the jump = NOP the range (determinate).
            return { RequiredKind::Branch, "conditional jump (branch)", true };
        case OpKind::InvertBranchCondition:
            // Needs the original Jcc opcode (flip the condition code).
            return { RequiredKind::Branch, "conditional jump (branch)", false };
        case OpKind::ReplaceAssignmentValue:
            // Needs the assignment's destination/operand encoding.
            return { RequiredKind::Assign, "assignment", false };
        case OpKind::ReplaceCompareConstant:
            // Needs the compare's operand encoding (where the constant sits).
            return { RequiredKind::Compare, "compare", false };
    }
    return { RequiredKind::Any, "any statement", true };  // unreachable.
}

// Does a resolved statement's decoded kind string satisfy the op's required
// kind? The decoded strings come from refdb (statements.kind): "return",
// "call", "branch", "assign", "compare"/"cmp". Any matches everything.
bool KindSatisfies(RequiredKind req, const std::string& stmt_kind) {
    switch (req) {
        case RequiredKind::Any:     return true;
        case RequiredKind::Return:  return stmt_kind == "return";
        case RequiredKind::Call:    return stmt_kind == "call";
        case RequiredKind::Branch:  return stmt_kind == "branch";
        case RequiredKind::Assign:  return stmt_kind == "assign";
        case RequiredKind::Compare:
            return stmt_kind == "compare" || stmt_kind == "cmp";
    }
    return false;
}

// ---- the determinate byte-emit ------------------------------------------

// Append a NOP-pad of `n` one-byte NOPs (0x90). The §9.3 same-size path fills
// the statement's byte range; the trampoline path (chosen at apply time when
// the op bytes exceed the range) is the statement verb's concern, not here.
void EmitNopPad(std::vector<uint8_t>& out, int64_t n) {
    for (int64_t i = 0; i < n && i < 0x10000; ++i) out.push_back(0x90);
}

// Emit `mov eax, imm32` (B8 imm32-le) for a nonzero value, or `xor eax,eax`
// (31 C0) for zero (the canonical zero-idiom; a 32-bit eax write zero-extends
// rax, the x64 int-return convention). Appends to `out`.
void EmitSetEaxConst(std::vector<uint8_t>& out, int64_t value) {
    if (value == 0) {
        out.push_back(0x31);  // xor eax, eax
        out.push_back(0xC0);
        return;
    }
    uint32_t v = static_cast<uint32_t>(value);
    out.push_back(0xB8);  // mov eax, imm32
    out.push_back(static_cast<uint8_t>(v & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<uint8_t>((v >> 24) & 0xFF));
}

// Build the determinate byte sequence for an op over `byte_range_len`. Called
// ONLY for ops whose contract.emit_determinate is true. Returns the bytes; the
// caller decides the same-size vs trampoline fit by comparing size to the
// range. For the return/value ops the instruction is emitted first, then the
// range is NOP-padded to length when it fits; when the instruction is LONGER
// than the range, the bytes are returned at their natural length (apply-time
// trampolines).
std::vector<uint8_t> EmitDeterminate(const OpDescriptor& op, int64_t range) {
    std::vector<uint8_t> out;
    switch (op.kind) {
        case OpKind::ReplaceWithNoop:
        case OpKind::SkipCallVoid:
            // Neutralize the statement: NOP the whole range.
            EmitNopPad(out, range);
            break;
        case OpKind::NeverTakeBranch:
            // Fall through: neutralize the conditional jump = NOP the range.
            EmitNopPad(out, range);
            break;
        case OpKind::ReplaceWithReturn:
            // mov eax, v ; ret ; pad.
            EmitSetEaxConst(out, op.value);
            out.push_back(0xC3);  // ret
            if (static_cast<int64_t>(out.size()) < range)
                EmitNopPad(out, range - static_cast<int64_t>(out.size()));
            break;
        case OpKind::ReplaceReturnValue:
            // Set the return register WITHOUT a ret (the statement already
            // returns — we overwrite only the value computation). mov eax, v ;
            // pad to the range.
            EmitSetEaxConst(out, op.value);
            if (static_cast<int64_t>(out.size()) < range)
                EmitNopPad(out, range - static_cast<int64_t>(out.size()));
            break;
        case OpKind::SkipCallReturnValue:
            // Skip the call but leave its result register set: mov eax, v ;
            // NOP the rest of the call's range.
            EmitSetEaxConst(out, op.value);
            if (static_cast<int64_t>(out.size()) < range)
                EmitNopPad(out, range - static_cast<int64_t>(out.size()));
            break;
        default:
            break;  // unreachable for determinate ops.
    }
    return out;
}

// :emit_for(kind, byte_range_len) -> table
//
// The settled-fork self-check seam (mirrors kcdx.locator's :resolve). Given a
// target statement's KIND (string) + byte span (int), returns:
//   kind mismatch → { kind_ok=false, reason=<teaching error naming kinds> }
//   determinate op → { kind_ok=true, deferred=false, bytes={...},
//                      fit="same_size"|"trampoline" }
//   deferred op    → { kind_ok=true, deferred=true } (bytes nil — needs the
//                      apply-time statement bytes; NOT fabricated)
// A bad arg → (nil, teaching error).
int Lua_OpEmitFor(lua_State* L) {
    auto* op = CheckOp(L, 1);

    if (lua_type(L, 2) != LUA_TSTRING) {
        lua_pushnil(L);
        lua_pushstring(L,
            "op:emit_for(kind, byte_range_len): `kind` (string) is required — "
            "the resolved statement's kind (e.g. \"call\", \"return\", "
            "\"branch\"). Call shape: "
            "kcdx.op.replace_with_noop:emit_for(\"call\", 5).");
        return 2;
    }
    if (lua_type(L, 3) != LUA_TNUMBER) {
        lua_pushnil(L);
        lua_pushstring(L,
            "op:emit_for(kind, byte_range_len): `byte_range_len` (number) is "
            "required — the resolved statement's byte span (statements."
            "byte_range_len). Call shape: "
            "kcdx.op.replace_with_noop:emit_for(\"call\", 5).");
        return 2;
    }
    std::string stmt_kind = lua_tostring(L, 2);
    int64_t range = static_cast<int64_t>(lua_tointeger(L, 3));

    OpContract c = ContractFor(op->kind);

    lua_newtable(L);  // result table
    int t = lua_gettop(L);

    // The kind check (AP14 — fail loud, teaching error naming the actual kind).
    if (!KindSatisfies(c.required, stmt_kind)) {
        lua_pushboolean(L, 0);
        lua_setfield(L, t, "kind_ok");
        // The §9.3 teaching-error shape: "<op> requires a <required>
        // statement; this statement is a `<actual>`."
        lua_pushfstring(L,
            "this op requires a %s statement; this statement is a `%s`. "
            "Use an op that applies to a %s statement, or pick a locator that "
            "resolves to a %s statement.",
            c.required_label, stmt_kind.c_str(),
            c.required_label, c.required_label);
        lua_setfield(L, t, "reason");
        return 1;
    }

    lua_pushboolean(L, 1);
    lua_setfield(L, t, "kind_ok");

    if (!c.emit_determinate) {
        // The final bytes need the apply-time statement bytes (a branch
        // displacement, a call target address, an assignment/compare operand
        // encoding). The op carries the descriptor; the byte-emit runs at apply
        // time (step 5) on the resolved statement's actual bytes. Report the
        // DEFERRAL, never a fabricated byte (AP14).
        lua_pushboolean(L, 1);
        lua_setfield(L, t, "deferred");
        return 1;
    }

    lua_pushboolean(L, 0);
    lua_setfield(L, t, "deferred");

    std::vector<uint8_t> bytes = EmitDeterminate(*op, range);

    lua_newtable(L);
    int b = lua_gettop(L);
    for (size_t i = 0; i < bytes.size(); ++i) {
        lua_pushinteger(L, static_cast<lua_Integer>(bytes[i]));
        lua_rawseti(L, b, static_cast<int>(i + 1));
    }
    lua_setfield(L, t, "bytes");

    // The fit the statement verb WILL pick at apply time for this byte_range_len:
    // bytes fit within the range → same_size rewrite; bytes exceed it → the
    // engine trampolines (00-original-plan.md §9.3 — "Author never sees a
    // doesn't-fit failure"). The pick itself is the statement verb's at apply
    // time; reporting it here makes the value introspectable.
    lua_pushstring(L,
        static_cast<int64_t>(bytes.size()) <= range ? "same_size" : "trampoline");
    lua_setfield(L, t, "fit");

    return 1;
}

// Install the op-value metatable. Idempotent (luaL_newmetatable returns 0 +
// leaves the existing table on the stack when already registered). Stack
// effect: 0.
void SetupMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kOpMetatable) == 0) {
        lua_pop(L, 1);  // already registered
        return;
    }
    // Stack: [..., mt]
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");  // mt.__index = mt (methods live on mt)
    lua_pushcfunction(L, Lua_OpGc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "kcdx.op.value");
    lua_setfield(L, -2, "__metatable");  // hide the metatable from pak Lua
    lua_pushcfunction(L, Lua_OpEmitFor);
    lua_setfield(L, -2, "emit_for");
    lua_pop(L, 1);  // pop mt; restore stack
}

// Push a fresh op-value userdata carrying `op`. Stack effect: +1.
void PushOp(lua_State* L, const OpDescriptor& op) {
    auto* mem = static_cast<OpDescriptor*>(
        lua_newuserdata(L, sizeof(OpDescriptor)));
    new (mem) OpDescriptor(op);
    luaL_getmetatable(L, kOpMetatable);
    lua_setmetatable(L, -2);
}

// Read a single required integer constant operand (arg `argn`) for an op
// constructor; raises a Lua error naming `verb` if missing/wrong. Raises (does
// not return (nil,err)) because these are constructors whose missing required
// arg is an author bug to surface at the call site.
int64_t CheckIntArg(lua_State* L, int argn, const char* verb) {
    if (lua_type(L, argn) != LUA_TNUMBER) {
        luaL_error(L,
            "kcdx.op.%s(value): `value` (number) is required — the constant "
            "the op writes (e.g. kcdx.op.%s(0)). See the kcdx.op docs.",
            verb, verb);
    }
    return static_cast<int64_t>(lua_tointeger(L, argn));
}

// Read a single required string operand (arg `argn`) for an op constructor;
// raises a Lua error naming `verb` + `param` if missing/wrong.
std::string CheckStringArg(lua_State* L, int argn, const char* verb,
                           const char* param) {
    if (lua_type(L, argn) != LUA_TSTRING) {
        luaL_error(L,
            "kcdx.op.%s(%s): `%s` (string) is required — see the kcdx.op docs "
            "for the call shape.",
            verb, param, param);
    }
    return lua_tostring(L, argn);
}

// ---- the op constructors -------------------------------------------------

int Lua_ReplaceWithReturn(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::ReplaceWithReturn;
    op.value = CheckIntArg(L, 1, "replace_with_return");
    op.has_value = true;
    PushOp(L, op);
    return 1;
}

int Lua_ReplaceReturnValue(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::ReplaceReturnValue;
    op.value = CheckIntArg(L, 1, "replace_return_value");
    op.has_value = true;
    PushOp(L, op);
    return 1;
}

int Lua_SkipCallReturnValue(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::SkipCallReturnValue;
    op.value = CheckIntArg(L, 1, "skip_call_return_value");
    op.has_value = true;
    PushOp(L, op);
    return 1;
}

int Lua_ReplaceAssignmentValue(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::ReplaceAssignmentValue;
    op.value = CheckIntArg(L, 1, "replace_assignment_value");
    op.has_value = true;
    PushOp(L, op);
    return 1;
}

int Lua_ReplaceCompareConstant(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::ReplaceCompareConstant;
    op.value = CheckIntArg(L, 1, "replace_compare_constant");
    op.has_value = true;
    PushOp(L, op);
    return 1;
}

int Lua_ReplaceCallTarget(lua_State* L) {
    OpDescriptor op;
    op.kind = OpKind::ReplaceCallTarget;
    op.target_fn = CheckStringArg(L, 1, "replace_call_target", "new_fn_name");
    PushOp(L, op);
    return 1;
}

// The no-operand ops. Each is a value-producing constructor: kcdx.op.noop is a
// FUNCTION the author calls (kcdx.op.noop()) OR — to match the §9.3 catalog's
// no-paren spelling for the no-arg ops (replace_with_noop, always_take_branch,
// …) — usable bare via a metatable. The catalog lists them without parens
// (`replace_with_noop`, not `replace_with_noop()`); the surface keeps them as
// zero-arg FUNCTIONS for one consistent call shape (always call to MINT a
// value), and the docs show the call form. (A bare-table-as-value form would
// make these the only ops that are values-not-calls — an inconsistency the
// learnable-surface cornerstone rejects; every op is minted by a call.)
int Lua_ReplaceWithNoop(lua_State* L) {
    OpDescriptor op; op.kind = OpKind::ReplaceWithNoop; PushOp(L, op); return 1;
}
int Lua_SkipCallVoid(lua_State* L) {
    OpDescriptor op; op.kind = OpKind::SkipCallVoid; PushOp(L, op); return 1;
}
int Lua_AlwaysTakeBranch(lua_State* L) {
    OpDescriptor op; op.kind = OpKind::AlwaysTakeBranch; PushOp(L, op); return 1;
}
int Lua_NeverTakeBranch(lua_State* L) {
    OpDescriptor op; op.kind = OpKind::NeverTakeBranch; PushOp(L, op); return 1;
}
int Lua_InvertBranchCondition(lua_State* L) {
    OpDescriptor op; op.kind = OpKind::InvertBranchCondition; PushOp(L, op);
    return 1;
}

const luaL_Reg kFunctions[] = {
    {"replace_with_return", Lua_ReplaceWithReturn},
    {"return_const", Lua_ReplaceWithReturn},          // alias
    {"replace_return_value", Lua_ReplaceReturnValue},
    {"replace_with_noop", Lua_ReplaceWithNoop},
    {"noop", Lua_ReplaceWithNoop},                    // alias
    {"skip_call_void", Lua_SkipCallVoid},
    {"skip_call_return_value", Lua_SkipCallReturnValue},
    {"replace_call_target", Lua_ReplaceCallTarget},
    {"always_take_branch", Lua_AlwaysTakeBranch},
    {"never_take_branch", Lua_NeverTakeBranch},
    {"invert_branch_condition", Lua_InvertBranchCondition},
    {"replace_assignment_value", Lua_ReplaceAssignmentValue},
    {"replace_compare_constant", Lua_ReplaceCompareConstant},
    {nullptr, nullptr},
};

}  // namespace

// Called from lua_bind.cpp::RegisterKcdxTable with the kcdx global table on top
// of the stack. Registers the op-value metatable, then creates the `op`
// sub-table inside kcdx. Stack effect: 0.
void bind(lua_State* L) {
    SetupMetatable(L);

    int kcdx_idx = lua_gettop(L);
    lua_newtable(L);
    for (const luaL_Reg* f = kFunctions; f->name; ++f) {
        lua_pushcfunction(L, f->func);
        lua_setfield(L, -2, f->name);
    }
    lua_setfield(L, kcdx_idx, "op");
}

}  // namespace kcdx::lua_bind_op
