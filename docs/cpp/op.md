# kcdx op values (↔ kcdx.op)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.op.*`](../lua/op.md) — op values that say *what
static change* to make at a code site (return a constant, no-op a statement,
skip a call, flip a branch).

**The op VALUE is built.** `kcdxOp` is a small author-filled **value struct** in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) — a `kcdxOpKind`
tag plus the operand fields — consumed by
[`kcdxStatementInterface::ReplaceWith`](statement.md). There is no factory
interface and no opaque handle: the author fills the struct directly, the C++
spelling of calling a `kcdx.op.<form>(...)` constructor. The kind tags map
one-to-one to the Lua constructors:

| Lua constructor | `kcdxOp.kind` | operand |
|---|---|---|
| `kcdx.op.replace_with_return(v)` | `kcdxOp_ReplaceWithReturn` | `value` |
| `kcdx.op.replace_return_value(v)` | `kcdxOp_ReplaceReturnValue` | `value` |
| `kcdx.op.replace_with_noop()` | `kcdxOp_ReplaceWithNoop` | — |
| `kcdx.op.skip_call_void()` | `kcdxOp_SkipCallVoid` | — |
| `kcdx.op.skip_call_return_value(v)` | `kcdxOp_SkipCallReturnValue` | `value` |
| `kcdx.op.replace_call_target(fn)` | `kcdxOp_ReplaceCallTarget` | `targetFn` |
| `kcdx.op.always_take_branch()` | `kcdxOp_AlwaysTakeBranch` | — |
| `kcdx.op.never_take_branch()` | `kcdxOp_NeverTakeBranch` | — |
| `kcdx.op.invert_branch_condition()` | `kcdxOp_InvertBranchCondition` | — |
| `kcdx.op.replace_assignment_value(v)` | `kcdxOp_ReplaceAssignmentValue` | `value` |
| `kcdx.op.replace_compare_constant(v)` | `kcdxOp_ReplaceCompareConstant` | `value` |

The full as-built usage (kind checks, deferred ops, the apply contract) is
documented with the consuming surface: [`statement.md`](statement.md). Like
Lua's `kcdx.op`, every op NAMES a behaviour — the author never supplies an
opcode, displacement, or instruction length. The engine produces the bytes and
picks a same-size rewrite vs a trampoline at apply time.

## The introspection accessor — not yet implemented (NYI)

The Lua `value:emit_for(kind, byteRangeLen)` introspection accessor has no C++
peer yet — tracked parity debt, discharged when the introspection mirror ships
and is verified callable. Its result will carry the same fields the Lua
`:emit_for` table does:

```cpp
// PLANNED result shape — the accessor itself is not in Interfaces.h yet, and
// where it lives is settled when it is built (it will take the BUILT kcdxOp
// value struct above, not an opaque handle).
struct kcdxOpEmit {
    bool        kindOk;        // op applies to this statement kind
    const char* reason;        // set when !kindOk (teaching error: actual + required kind)
    bool        deferred;      // final bytes need the live statement's bytes
    bool        hasBytes;      // bytes valid (i.e. !deferred)
    const unsigned char* bytes;
    int                  byteCount;
    const char* fit;           // "same_size" / "trampoline" (when hasBytes)
};
```

The five statement-bytes-dependent ops (`AlwaysTakeBranch`,
`InvertBranchCondition`, `ReplaceCallTarget`, `ReplaceAssignmentValue`,
`ReplaceCompareConstant`) will report `deferred = true` / `hasBytes = false` —
their final bytes are produced when the op is applied to a resolved statement,
the same contract as the Lua side. A wrong-kind statement reports
`kindOk = false` with the teaching `reason`, never a silent accept.
