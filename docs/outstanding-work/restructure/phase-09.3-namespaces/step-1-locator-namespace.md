# Phase 9.3 step 1 — `kcdx.locator.*` value namespace

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

The `kcdx.locator.*` value namespace — locator values that say WHERE in a function
a hook/statement applies. A new, ADDITIVE namespace (no existing surface replaced):
the hook (step 4) and statement (step 5) verbs consume these values, but the
namespace stands alone and self-verifies at this step (each locator resolves to the
expected statement against a reference-DB function).

## Scope (`src/lua_bind_locator.cpp`, new file)

- Function-level: `kcdx.locator.function_entry()`, `function_exit()`.
- Statement-content shortcuts (the documented common path): `first_call_to(fn)`,
  `last_call_to(fn)`, `call_to(fn)` (errors if multiple), `first_return()`,
  `last_return()`, `return_value(v)`, `first_read_of_cvar(name)`.
- General matcher: `matching{kind=, callee=, condition_contains=, reads_cvar=,
  references_string=}`.
- Labeled expert hatch: `matching_pattern("48 8B C1 ...")` — raw-AOB cases,
  explicitly labeled expert (`.claude/rules/cornerstones.md` — the common-path
  locators are the default; the pattern form is the labeled escape hatch).
- Default: `kcdx.locator.function_entry()` when omitted on a verb that accepts a
  default. (Module is NOT defaulted — required first positional per the phase rule.)
- A locator value resolves against the reference-DB `statements` metadata
  (`statements.kind` / `callee` / `string_ref` / `byte_range_len`), via `refdb`.
  Captures (the per-statement variables, for insert-callbacks) are the joined
  `referenced_vars` rows, matched by `(address_version_id, statement_idx)` — a
  separate table, not a column on `statements`.

## Test bar (runs AT this step)

A `test-plugins/cap-NN-locator-resolve/` (next free cap-NN, suite-gated): for a
known reference-DB function, EACH locator form resolves to the EXPECTED statement
index (a row per locator family — `function_entry`, `first_call_to`, `matching{}`,
`matching_pattern` — that FAILS if the locator resolves to the wrong statement or
fails to resolve). NOT deferred to steps 4/5 — the namespace self-verifies here:
the locator → statement mapping is checkable against the reference DB without a hook
or statement verb consuming it. PROBE Q silent.

## Dependencies

The statement-resolution-layer prerequisite
([`../../statement-resolution-layer/`](../../statement-resolution-layer/), steps
1+2 landed) — it ships the curated-function `statements` metadata into
`reference.sqlite` and exposes the `refdb` statement-resolution API a locator
resolves against. This extends the Phase 9.1 foundation (the reference DB +
`refdb` ADDRESS resolution — DONE), which shipped address resolution but not the
statement data.

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.locator.*` —
locator values" (the locator catalog + the default + the expert-hatch labeling).
Build to that §, not this doc's summary.

## Disassembler-test / author-burden note

The common-path locators (`first_call_to`, `first_return`, …) name what the author
already understands — no hex. `matching_pattern` is the LABELED expert hatch for the
rare raw-AOB case (`.claude/rules/cornerstones.md`, AP12). No new DB rows — locators
resolve against existing reference-DB statement metadata.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "`kcdx.locator.*`".
