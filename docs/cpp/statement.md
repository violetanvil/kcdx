# kcdxStatementInterface (↔ kcdx.statement)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.statement.*`](../lua/statement.md) — **static-bytes
modification** at a located statement. `ReplaceWith` writes a static op's bytes
in place so the modified bytes execute natively (zero per-call cost);
`InsertBefore` / `InsertAfter` are the callback form (see their
register-and-defer contract below).

Fetched via `api->QueryInterface(kcdxInterface_Statement,
kcdxStatementInterface_Version)` (or as `K.statement` after `K.Init(...)`).

Like Lua's `kcdx.statement`, the author names a target function, an op, and
optionally a locator — never an address, an offset, an instruction length, or a
byte. The engine resolves the statement from the curated reference database,
emits the op's bytes, and picks a same-size rewrite vs a trampoline at apply
time (you never see a "doesn't fit").

This is the static-bytes sibling of [`kcdxHookInterface`](hook.md): a hook runs
a callback every call (a per-call dispatch); `ReplaceWith` changes the bytes
themselves (zero per-call cost). Use `ReplaceWith` when the behaviour is static
and you want native-speed execution; use a [hook](hook.md) when you need
per-call logic. Both surfaces feed ONE engine path — a C++ registration lands
as the same deferred-apply statement entry the Lua verbs queue, kind-checked
and applied by the same engine pass, in unified load order with the conflict
engine seeing every plugin's intent first.

## The op value — `kcdxOp` (↔ a `kcdx.op.*` value)

A plain tagged struct you fill (typically a brace literal) — the C++ peer of a
[`kcdx.op.*`](op.md) value. The `kcdxOpKind` catalog mirrors the Lua
constructors one-to-one:

```cpp
kcdxOp noop = { kcdxOp_ReplaceWithNoop };          // kcdx.op.replace_with_noop
kcdxOp ret0 = { kcdxOp_ReplaceWithReturn, 0 };     // kcdx.op.replace_with_return(0)

struct kcdxOp {
    kcdxOpKind  kind;       // which op — kcdxOp_ReplaceWithReturn / _ReplaceReturnValue /
                            // _ReplaceWithNoop / _SkipCallVoid / _SkipCallReturnValue /
                            // _ReplaceCallTarget / _AlwaysTakeBranch / _NeverTakeBranch /
                            // _InvertBranchCondition / _ReplaceAssignmentValue /
                            // _ReplaceCompareConstant
    long long   value;      // the constant operand (the value-carrying kinds only)
    const char* targetFn;   // kcdxOp_ReplaceCallTarget only — the new callee's NAME
};
```

The engine classifies the kind — which statement kind it requires (a branch op
needs a branch statement, a call op a call, …) and whether its byte emit is
determinate — from the **same per-op table the Lua constructors use**, so the
two surfaces agree exactly. The five statement-bytes-dependent ops
(`AlwaysTakeBranch`, `InvertBranchCondition`, `ReplaceCallTarget`,
`ReplaceAssignmentValue`, `ReplaceCompareConstant`) register and then surface a
clear not-yet-emittable deferral at apply (`IsApplied` false + a `GetReason`
teaching string) — their final bytes need the live statement's own bytes, which
the statement-resolution layer does not yet expose; the engine never fabricates
a guessed byte. The determinate ops apply today.

## The locator value — `kcdxLocator` (↔ a `kcdx.locator.*` value)

A plain tagged struct — the C++ peer of a [`kcdx.locator.*`](locator.md) value,
mirroring the constructor catalog one-to-one. Each kind reads only its own
operand field(s); null means unset:

```cpp
kcdxLocator firstCall = {};
firstCall.kind       = kcdxLocator_FirstCallTo;   // kcdx.locator.first_call_to(fn)
firstCall.calleeOrFn = "IsInCombat";
```

| Kind | Lua form | Operand field(s) |
|---|---|---|
| `kcdxLocator_FunctionEntry` | `function_entry()` | — |
| `kcdxLocator_FunctionExit` | `function_exit()` | — |
| `kcdxLocator_FirstCallTo` / `_LastCallTo` / `_CallTo` | `first_call_to(fn)` / `last_call_to(fn)` / `call_to(fn)` | `calleeOrFn` |
| `kcdxLocator_FirstReturn` / `_LastReturn` | `first_return()` / `last_return()` | — |
| `kcdxLocator_ReturnValue` | `return_value(v)` | `returnValueOperand` |
| `kcdxLocator_ReferencesString` | `references_string(s)` | `stringArg` |
| `kcdxLocator_FirstReadOfCvar` | `first_read_of_cvar(n)` | `stringArg` |
| `kcdxLocator_Matching` | `matching{...}` | any subset of `matchKind` / `matchCallee` / `matchConditionContains` / `matchReadsCvar` / `matchReferencesString` (non-null keys are ANDed) |
| `kcdxLocator_MatchingPattern` | `matching_pattern("…")` | `aobPattern` — **expert/advanced escape hatch** (raw AOB; not a statement-metadata locator). The common path is the named forms above. |

A kind's missing required operand is a loud registration error (a zero handle +
a logged teaching reason naming the field), never a silently-defaulted locator.

## Call shape

```cpp
struct kcdxStatementInterface {
    // Static-bytes replacement (a STATIC kcdxOp — never a callback).
    kcdxStatementHandle (*ReplaceWith)(const char* target, const kcdxOp* op,
                                       const kcdxStatementOptions* opts /* nullable */);
    // Callback at a located statement; locator REQUIRED (register-and-defer — below).
    kcdxStatementHandle (*InsertBefore)(const char* target, const kcdxLocator* locator,
                                        void* callback, const kcdxStatementOptions* opts);
    kcdxStatementHandle (*InsertAfter) (const char* target, const kcdxLocator* locator,
                                        void* callback, const kcdxStatementOptions* opts);
    // Handle queries (same contract as the hook handle's query set).
    bool        (*IsApplied)(kcdxStatementHandle h);
    const char* (*GetReason)(kcdxStatementHandle h);
    const char* (*GetName)  (kcdxStatementHandle h);
    bool        (*Uninstall)(kcdxStatementHandle h);   // not yet supported — see below
};
```

The verbs are one-to-one with the Lua forms
([`kcdx.statement.*`](../lua/statement.md)): `replace_with` → `ReplaceWith`,
`insert_before` / `insert_after` → `InsertBefore` / `InsertAfter`. There are no
`Before` / `After` / `Around` / `Replace` methods — those describe callback
ordering relative to an original call, which has no static-bytes analogue (use
[`kcdxHookInterface`](hook.md) for those).

- **Arguments.** `target` is the curated function NAME the statement lives in
  (the common path — the engine carries the address, the statement metadata,
  and the fit decision). `op` is a `kcdxOp` value (ReplaceWith only — the
  static-op-only contract; a function pointer in the op slot does not exist by
  construction). `locator` on the insert verbs is REQUIRED ("insert before
  what?" has no default); ReplaceWith's optional locator rides in `opts`
  (null = the function's first statement, the `function_entry()` default).
  `callback` is a function pointer cast to `void*`.
- **Options (`kcdxStatementOptions`, nullable).** `locator` (ReplaceWith only),
  `name` / `description` (identity; engine synthesizes a default from the
  target when absent), `module` (default `"WHGame.dll"` — the same default the
  hook options carry; the Lua surface spells this as its required first
  positional), `owningPlugin` (your handle, for attribution), and `targetRef`
  (below).
- **Return.** A `kcdxStatementHandle`. `0` = registration FAILED — the teaching
  reason is auto-logged at Error level to the engine log and your plugin's log
  (category `STATEMENT_INTERFACE`). A NON-ZERO handle does **not** mean
  applied: the resolve + kind-check + emit + write run in the engine's
  deferred apply pass (so the conflict engine sees every plugin's intent before
  any byte changes). Query `IsApplied(h)` after the apply pass; `GetReason(h)`
  returns the teaching string when it did not apply (null when applied).
- **Error behaviour.** Registration fails loud (zero handle + the logged
  teaching reason) on a null op, a missing target, a not-found `targetRef`, an
  unrecognized op/locator kind value, or a missing required operand. An
  apply-time failure is loud on the handle: a **kind mismatch** (e.g. a branch
  op at a non-branch statement) is rejected with a teaching reason naming the
  actual AND required kinds — never a silent wrong-kind write; an unresolved
  statement, a deferred-op emit, and the insert deferral each carry their own
  reason. The engine checks the statement KIND, not your purpose — what you
  replace is your call.

### Target by reference — `opts.targetRef`

To resolve a function once and pass it to N verbs, set `opts.targetRef` to a
[`kcdxFunctionRef`](functions.md) (minted by `K.functions->GameByName(...)` /
`PluginByName(...)`). When set, the reference **wins** over the positional
`target` string (which may then be null). The reference collapses to its
carried name for statement resolution, so it must be a named reference with
`found = true` — a not-found or `GameById` (nameless) reference is a loud
registration error, never a silent fallback.

### `InsertBefore` / `InsertAfter` — register-and-defer (not yet firing)

The engine's statement-locator capture-thunk apply path is **not wired yet — on
both surfaces** (the Lua `kcdx.statement.insert_*` has the same contract). An
insert REGISTERS (a non-zero handle on a valid call) and fails LOUD at the
apply pass: `IsApplied(h)` stays false and `GetReason(h)` carries the teaching
reason. The callback does not fire until that engine path ships; the deferral
is explicit on the handle, never a silently-claimed install. Use `ReplaceWith`
for a static-bytes modification at a resolved statement until then.

### `Uninstall` — not yet supported

An applied statement rewrite has no byte-revert path yet (the engine stores no
original-bytes snapshot), so `Uninstall(h)` returns `false` and logs the
teaching reason; the registration's status is unchanged — never a
silently-flipped "removed" over bytes that are still live. This mirrors the Lua
statement handle's `:uninstall()` teaching error. A per-kind uninstall
(snapshot + revert) ships as its own later feature.

## Minimal snippet

```cpp
#include "kcdx/Kcdx.h"
static Kcdx K;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "redmoon", "outfit")) return true;  // logs why

    // Make IsInCombat always report "no": replace its first return statement
    // with `return 0`. Name + locator + op — no hex, no offsets.
    kcdxOp ret0 = { kcdxOp_ReplaceWithReturn, 0 };
    kcdxLocator firstReturn = {};
    firstReturn.kind = kcdxLocator_FirstReturn;
    kcdxStatementOptions opts = {};
    opts.locator      = &firstReturn;
    opts.owningPlugin = K.self;
    kcdxStatementHandle h = K.statement->ReplaceWith("IsInCombat", &ret0, &opts);
    if (h == 0) return true;  // the teaching reason is in your plugin log

    // Later (after the engine's apply pass): confirm it went live.
    // if (!K.statement->IsApplied(h)) K.log.Warn("MOD", "%s", K.statement->GetReason(h));
    return true;
}
```

## Glossary

- **op value (`kcdxOp`)** — a tagged struct naming WHAT static change to make
  (return a constant, no-op a statement, skip a call, flip a branch); the C++
  peer of a `kcdx.op.*` value. The author never supplies an opcode or a byte.
- **locator value (`kcdxLocator`)** — a tagged struct naming WHERE in a curated
  function the op applies (the function entry, the first call to X, the first
  return, …); the C++ peer of a `kcdx.locator.*` value.
- **determinate vs deferred op** — a determinate op's bytes are computable from
  the statement's kind + byte span alone and apply today; a deferred op's final
  bytes need the live statement's own bytes and currently surface a loud
  not-yet-emittable deferral instead of a guessed byte.
- **register-and-defer** — the insert verbs' current contract: the registration
  is accepted (a handle), the apply pass declines loudly (`IsApplied` false +
  a readable reason) because the callback-install path is not built yet.
- **statement handle (`kcdxStatementHandle`)** — the opaque id every statement
  verb returns; query `IsApplied` / `GetReason` / `GetName` on it. Non-zero ≠
  applied (deferred-apply model).

---

See also: [op.md](op.md) + [locator.md](locator.md) (the planned C++
introspection mirrors of `kcdx.op.*:emit_for` / `kcdx.locator.*:resolve` —
still NYI; the statement verbs consume the `kcdxOp` / `kcdxLocator` value
structs above directly), [hook.md](hook.md) (the per-call callback sibling),
[functions.md](functions.md) (the `kcdxFunctionRef` that `opts.targetRef`
accepts), [`../lua/statement.md`](../lua/statement.md) (the Lua surface).
