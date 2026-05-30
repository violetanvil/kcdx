# Phase 9.3 step 4 — `kcdx.statement.*` static-bytes namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 4.

## What

`kcdx.statement.*` — static-bytes modification (kcdx-original concept,
descriptively named). Zero per-call cost: the game's bytes are modified and
executed natively. Use when behavior is static and you want native-speed
execution. Genuinely different namespace from `kcdx.hook.*`, NOT an alias (per the
confirmed design decisions §9).

## Scope (`src/lua_bind_statement.cpp`)

- `kcdx.statement.replace_with(module, target, [locator], op, [opts])` — accepts
  only static ops (`kcdx.op.*`, step 2).
- `kcdx.statement.insert_before(module, target, locator, callback, [opts])` —
  callback-only (no static-op form; insert-with-callback is the coherent case).
- `kcdx.statement.insert_after(...)` — callback-only (same).
- No before/after/around — those describe callback-ordering relative to an
  original call, which has no static-bytes analog.
- **Engine pads-and-trampolines always.** When an op's bytes don't fit the
  statement's byte range, the engine trampolines automatically (lifts to
  ±2GB-adjacent allocation, rel32 redirect). Author never sees a "doesn't fit"
  failure. Engine catches kind/type mismatch at registration with teaching errors,
  but does NOT gate on semantic-purpose correctness (author's call).

## Dependencies

Steps 1 (locators) + 2 (ops). Trampoline auto-pad relies on step 6 for capacity at
scale.

## Test bar

`cap-XX-statement-replace`: `kcdx.statement.replace_with(kcdx.functions.WHGame.fn,
kcdx.op.return_const(0))` produces zero per-call Lua dispatch (verified by absence
of dispatch log lines during a tight loop hitting the target).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The two distinct
namespaces" + "Engine pads-and-trampolines always".
