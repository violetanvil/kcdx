# Phase 9.3 step 5 — `kcdx.statement.*` static-bytes namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 5.

## What

`kcdx.statement.*` — static-bytes modification (a kcdx-original concept,
descriptively named). Zero per-call cost: the game's bytes are modified and executed
natively. Use when behavior is static and you want native-speed execution. A
genuinely different namespace from `kcdx.hook.*`, NOT an alias (the confirmed design
decision). A new, ADDITIVE surface (new file; nothing replaced).

## Scope (`src/lua_bind_statement.cpp`, new file)

- `kcdx.statement.replace_with(module, target, [locator], op, [opts])` — accepts only
  static ops (`kcdx.op.*`, step 2); `target` accepts a `kcdx.functions.*` reference
  (step 3) or a `(module, target)` string pair.
- `kcdx.statement.insert_before(module, target, locator, callback, [opts])` —
  callback-only (no static-op form; insert-with-callback is the coherent case).
- `kcdx.statement.insert_after(...)` — callback-only (same).
- No `before/after/around` — those describe callback-ordering relative to an original
  call, which has no static-bytes analog.
- **Engine pads-and-trampolines always.** When an op's bytes don't fit the
  statement's byte range, the engine trampolines automatically (lifts to ±2GB-adjacent
  allocation, rel32 redirect). The author never sees a "doesn't fit" failure. The
  engine catches a kind/type mismatch at registration with a teaching error
  (`.claude/rules/cornerstones.md` errors-that-teach, AP14), but does NOT gate on
  semantic-purpose correctness (author's call).

## Test bar (runs AT this step)

A `test-plugins/cap-NN-statement-replace/` (suite-gated): a
`kcdx.statement.replace_with(kcdx.functions.WHGame.<fn>, kcdx.op.return_const(0))`
produces ZERO per-call Lua dispatch — verified by the ABSENCE of dispatch log lines
during a tight loop hitting the target (a falsifiable claim that FAILS if a per-call
dispatch line appears, proving the bytes execute natively, not via a callback). PROBE
Q silent. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

Step 1 (locators) + step 2 (ops — `replace_with` consumes them) + step 3 (the
`kcdx.functions.*` reference the target accepts). The auto-pad-and-trampoline relies
on step 6 for capacity AT SCALE — but this step's single-target test does NOT cross
the pool's threshold, so it runs against the current single-region pool; step 6 is a
TC-scale capacity dependency, not a single-test dependency.

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.statement.*` —
static-bytes modification" + "Engine pads-and-trampolines always" + the
`cap-XX-statement-replace` verification-gate row. `.claude/rules/hook-engine.md`
(byte-patch semantics). Build to those §s, not this summary.

## Disassembler-test / author-burden note

The author names a target + an `kcdx.op.*` value; the engine owns the byte emit, the
fit decision, and the trampoline. No author hex, no offset, no instruction length
(`.claude/rules/cornerstones.md`, AP12). No new DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The two distinct
namespaces" + "Engine pads-and-trampolines always".
