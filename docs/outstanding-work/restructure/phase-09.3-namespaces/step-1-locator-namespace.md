# Phase 9.3 step 1 — `kcdx.locator.*` value namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

The `kcdx.locator.*` value namespace — locator values that say WHERE in a function
a hook/statement applies. Consumed by the hook (step 3) and statement (step 4)
verbs.

## Scope (`src/lua_bind_locator.cpp`)

- Function-level: `function_entry()`, `function_exit()`.
- Statement-content shortcuts (documented common path): `first_call_to(fn)`,
  `last_call_to(fn)`, `call_to(fn)` (errors if multiple), `first_return()`,
  `last_return()`, `return_value(v)`, `first_read_of_cvar(name)`.
- General matcher: `matching{kind=, callee=, condition_contains=, reads_cvar=,
  references_string=}`.
- Labeled expert hatch: `matching_pattern("48 8B C1 ...")` — raw-AOB cases,
  explicitly labeled expert.
- Default: `kcdx.locator.function_entry()` when omitted on verbs that accept a
  default. (Module is NOT defaulted — required first positional per the phase
  rule.)

## Disassembler test

The common-path locators (`first_call_to`, `first_return`, …) name what the author
already understands. `matching_pattern` is the labeled escape hatch. Clean.

## Test bar

Exercised by steps 3/4's tests; this step self-checks each locator resolves to the
expected statement against a reference-DB function.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.locator.*`".
