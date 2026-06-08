# Phase 9.3 step 2 — `kcdx.op.*` static-op value namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

The `kcdx.op.*` value namespace — static-bytes op values consumed by
`kcdx.statement.replace_with` (step 5). A new, ADDITIVE namespace: a curated catalog
with descriptive primary names + friendly aliases; each op carries a byte-emit
function the engine invokes at apply time. Stands alone and self-verifies here (each
op emits the expected bytes for a known statement kind; a kind-mismatch is rejected
with a teaching error).

## Scope (`src/lua_bind_op.cpp`, new file)

Catalog (descriptive primary names + aliases):
`replace_with_return(value)` (alias `return_const`), `replace_with_noop` (alias
`noop`), `skip_call_void`, `skip_call_return_value(value)`,
`replace_call_target(new_fn_name)`, `always_take_branch`, `never_take_branch`,
`invert_branch_condition`, `replace_assignment_value(value)`,
`replace_compare_constant(value)`, `replace_return_value(value)`.

- Each op carries a byte-emit function the engine invokes at apply time.
- The engine picks same-size byte rewrite vs trampoline based on
  op-bytes-vs-statement-range (the reference-DB `statements.byte_range_len`).
- The engine catches a kind/type mismatch at registration with a TEACHING error
  (`always_take_branch` requires a conditional jump statement; on a `call` it errors
  naming the actual kind — `.claude/rules/cornerstones.md` errors-that-teach, AP14
  never a silent drop). It does NOT gate on semantic-purpose correctness (author's
  call).

## Test bar (runs AT this step)

A `test-plugins/cap-NN-op-emit/` (suite-gated): EACH op emits the EXPECTED bytes for
a known statement kind (a row per op family that FAILS if the emitted bytes differ
from the verified expectation); the engine REJECTS a kind-mismatch
(`always_take_branch` on a `call`) with the teaching error (a row that reads the
actual reject + its message, not a tautology — AP15). NOT deferred to step 5 — the
op → bytes mapping + the kind-mismatch reject are checkable here without a statement
verb applying them. PROBE Q silent.

## Dependencies

Phase 9.1 (the reference DB `statements.kind` / `byte_range_len` an op's bytes are
checked against — DONE). Independent of step 1.

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.op.*` —
static-bytes op values" (the catalog + the same-size-vs-trampoline pick + the
kind-mismatch teaching error). Build to that §, not this summary.

## Disassembler-test / author-burden note

The op names ARE the abstraction — the author picks `kcdx.op.never_take_branch`, not
a byte sequence. The disassembler test passes by construction
(`.claude/rules/cornerstones.md`). No author hex. No new DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.op.*`".
