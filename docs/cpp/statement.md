# kcdxStatementInterface (↔ kcdx.statement)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.statement.*`](../lua/statement.md) — **static-bytes
modification** at a located statement. `ReplaceWith` writes an [op](op.md)'s
bytes in place so the modified bytes execute natively (zero per-call cost);
`InsertBefore` / `InsertAfter` run a callback at a statement.

**Not yet implemented (NYI).** There is no statement interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not link
against it. `kcdxStatementInterface` is the **planned** mirror name; it is
tracked parity debt — both docs map a capability even when only one is built,
discharged when the C++ parity phase ships it and it is verified callable. This
entry maps the planned shape so both surfaces describe the capability while the
engine catches up.

Like Lua's `kcdx.statement`, the author names a target statement and an
[op](op.md) — never an address, an offset, an instruction length, or a byte. The
engine resolves the [locator](locator.md), emits the op's bytes, and picks a
same-size rewrite vs a trampoline at apply time (you never see a "doesn't fit").

This is the static-bytes sibling of [`kcdxHookInterface`](hook.md): a hook runs a
callback every call (a per-call dispatch); `ReplaceWith` changes the bytes
themselves (zero per-call cost). Use `ReplaceWith` when the behaviour is static
and you want native-speed execution; use a [hook](hook.md) when you need per-call
logic.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options struct, doing → typed
params; `{named table}` → an options struct, positional Lua args → typed
params), the planned mirror is a `QueryInterface`-fetched interface that consumes
a [locator value](locator.md) and an [op value](op.md) (the C++ peers of the Lua
values).

```cpp
// PLANNED — not in Interfaces.h yet.

struct kcdxStatementOptions {
    const char* name;          // optional; a stable handle name
    const char* description;   // optional
};

struct kcdxStatementInterface {
    // Static-bytes modification: write the op's bytes at the located statement.
    // `locator` may be null → the function entry (the Lua function_entry()
    // default). `op` is a static op value (kcdxOpInterface) — NOT a callback.
    kcdxRegistrationHandle (*ReplaceWith)(
        const char* module, const char* target,
        kcdxLocator* locator, kcdxOp* op,
        const kcdxStatementOptions* opts);

    // Callback at a located statement. `locator` is REQUIRED ("insert before
    // what?" has no default). The callback receives the statement's captures.
    kcdxRegistrationHandle (*InsertBefore)(
        const char* module, const char* target,
        kcdxLocator* locator, kcdxStatementCallback callback, void* user,
        const kcdxStatementOptions* opts);
    kcdxRegistrationHandle (*InsertAfter)(
        const char* module, const char* target,
        kcdxLocator* locator, kcdxStatementCallback callback, void* user,
        const kcdxStatementOptions* opts);
};
```

The mirror is one-to-one with the Lua forms
([`kcdx.statement.*`](../lua/statement.md)): `replace_with` → `ReplaceWith`,
`insert_before` / `insert_after` → `InsertBefore` / `InsertAfter`. There are no
`Before` / `After` / `Around` / `Replace` methods — those describe callback
ordering relative to an original call, which has no static-bytes analogue (use
[`kcdxHookInterface`](hook.md) for those). The op kind check teaches on a mismatch
(the actual + required statement kind), exactly as the Lua side; the engine
checks the kind, not the author's purpose. As on the Lua side, `InsertBefore` /
`InsertAfter` against a statement locator are surface-built but the engine's
statement-capture apply path is not yet wired — they will register and fail loud
at apply until that path lands.
