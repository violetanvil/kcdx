# kcdxOpInterface (↔ kcdx.op)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.op.*`](../lua/op.md) — op values that say *what
static change* to make at a code site (return a constant, no-op a statement,
skip a call, flip a branch), plus an `EmitFor(kind, byteRangeLen)` introspection
accessor that reports what the op produces for a statement of a given kind and
span.

**Not yet implemented (NYI).** There is no op interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. `kcdxOpInterface` is the **planned** mirror name; it is tracked
parity debt — both docs map a capability even when only one is built, discharged
when the C++ parity phase ships it and it is verified callable. This entry maps
the planned shape so both surfaces describe the capability while the engine
catches up.

Like Lua's `kcdx.op`, every op NAMES a behaviour — the author never supplies an
opcode, displacement, or instruction length. The engine produces the bytes and
picks a same-size rewrite vs a trampoline at apply time.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options/factory, doing → typed
params), the planned mirror is a `QueryInterface`-fetched interface that mints an
op value and exposes an `EmitFor` accessor returning the same fields the Lua
`:emit_for` table carries.

```cpp
// PLANNED — not in Interfaces.h yet.

// An op descriptor (the C++ peer of a kcdx.op.* value).
struct kcdxOp;  // opaque handle minted by the factory calls below

struct kcdxOpEmit {
    bool        kindOk;        // op applies to this statement kind
    const char* reason;        // set when !kindOk (teaching error: actual + required kind)
    bool        deferred;      // final bytes need the live statement's bytes
    bool        hasBytes;      // bytes valid (i.e. !deferred)
    const unsigned char* bytes;
    int                  byteCount;
    const char* fit;           // "same_size" / "trampoline" (when hasBytes)
};

struct kcdxOpInterface {
    // Return / function-level
    kcdxOp* (*ReplaceWithReturn)(long long value);   // alias ReturnConst
    kcdxOp* (*ReturnConst)(long long value);
    kcdxOp* (*ReplaceReturnValue)(long long value);
    // Whole-statement neutralize
    kcdxOp* (*ReplaceWithNoop)();                     // alias Noop
    kcdxOp* (*Noop)();
    // Call statements
    kcdxOp* (*SkipCallVoid)();
    kcdxOp* (*SkipCallReturnValue)(long long value);
    kcdxOp* (*ReplaceCallTarget)(const char* newFnName);
    // Branch (conditional-jump) statements
    kcdxOp* (*AlwaysTakeBranch)();
    kcdxOp* (*NeverTakeBranch)();
    kcdxOp* (*InvertBranchCondition)();
    // Assignment / compare statements
    kcdxOp* (*ReplaceAssignmentValue)(long long value);
    kcdxOp* (*ReplaceCompareConstant)(long long value);

    // Inspect what an op emits for a statement of a given kind + byte span.
    kcdxOpEmit (*EmitFor)(kcdxOp* op, const char* kind, long long byteRangeLen);
};
```

The mirror is one-to-one with the Lua forms ([`kcdx.op.*`](../lua/op.md)): each
`kcdx.op.<form>(...)` maps to the same-named factory call, and the Lua
`value:emit_for(kind, byteRangeLen)` table maps to `kcdxOpEmit`. The five
statement-bytes-dependent ops (`AlwaysTakeBranch`, `InvertBranchCondition`,
`ReplaceCallTarget`, `ReplaceAssignmentValue`, `ReplaceCompareConstant`) report
`deferred = true` / `hasBytes = false` from `EmitFor` — their final bytes are
produced when the op is applied to a resolved statement, the same contract as
the Lua side. A wrong-kind statement reports `kindOk = false` with the teaching
`reason`, never a silent accept.
