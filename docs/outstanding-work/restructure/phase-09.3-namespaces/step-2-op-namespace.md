# Phase 9.3 step 2 — `kcdx.op.*` static-op value namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

The `kcdx.op.*` value namespace — static-bytes op values consumed by
`kcdx.statement.replace_with` (step 4). A curated catalog with descriptive
primary names + friendly aliases; each op carries a byte-emit function the engine
invokes at apply time.

## Scope (`src/lua_bind_op.cpp`)

Catalog: `replace_with_return(value)` (alias `return_const`),
`replace_with_noop` (alias `noop`), `skip_call_void`,
`skip_call_return_value(value)`, `replace_call_target(new_fn_name)`,
`always_take_branch`, `never_take_branch`, `invert_branch_condition`,
`replace_assignment_value(value)`, `replace_compare_constant(value)`,
`replace_return_value(value)`.

The engine picks same-size byte rewrite vs trampoline based on
op-bytes-vs-statement-range (from the reference-DB `byte_range_len`). Authors
never see assembly — the op names describe the behavior the bytes produce.

## Disassembler test

The op names ARE the abstraction — the author picks `kcdx.op.never_take_branch`,
not a byte sequence. This is the disassembler test passing by construction. Clean.

## Test bar

Exercised by step 4's `cap-XX-statement-replace`; this step self-checks each op
emits the expected bytes for a known statement kind + that the engine rejects a
kind-mismatch (`always_take_branch` on a `call`) with a teaching error.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.op.*`".
