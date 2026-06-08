# Step 2 — refdb statement-resolution API, eager-load (engine lane)

**Status: DONE.** Ledger row: [`README.md`](README.md) → step 2.
**Lane: engine / consumer.**

## Sub-step ledger (commit-grain decomposition)

The step is multi-commit; each sub-step is independently verifiable when it lands
(`.claude/rules/incremental-delivery.md`). Verified in-game by an **engine-internal
statement-resolution self-test at init** (user decision 2026-06-08): the engine runs
the resolution self-checks at startup and reports each as a suite row to
`kcdx-dev.log`; the `cap-79-stmt-resolve` plugin is a thin suite-gated shell. No
Phase-9.3 author surface (`kcdx.locator.*`/`kcdx.statement.*`) is built early — the seam
is engine-owned test scaffolding (rejected: a dev-only Lua probe / `kcdx_dev_inspect`,
both pull a surface forward ahead of its design).

| Sub-step | Status | Commit |
|---|---|---|
| 2a — statement data model + eager-load in `Open()` (per-`address_version_id` statement vectors + the `referenced_vars` join); init-time curated-count assertion | DONE | f26c819 |
| 2b — the full §9.3 locator-resolution catalog + per-statement reads (`kind`/`callee`/`string_ref`/`byte_range_*`) | DONE | abdbee3 |
| 2c — captures-by-name join + the engine-internal self-test (the seam) + the `cap-83-stmt-resolve` thin shell plugin + matrix row (`cap-79` was taken; `cap-83` is next-free) | DONE | (landed) |

## What

Extend `refdb` (`src/refdb.{h,cpp}`) to eager-load the curated statement metadata at
engine startup — alongside the existing `kcdx.functions.*` / address-resolution
population (decision 2, mirroring the spec's eager model) — and expose a
statement-resolution surface the Phase 9.3 locator/op/statement binders read through.
This is the engine consumer of the data step 1 ships into `reference.sqlite`.

## Scope (`src/refdb.{h,cpp}`, extended)

- **Eager-load** the curated `statements` + `referenced_vars` rows at startup (the same
  startup pass that builds the address-resolution cache, `498934c`). Resident cost
  ~0.44 MB (the shipped curated subset incl. `pseudo_text`); the spec's stated eager
  model.
- **Locator resolution — the FULL §9.3 catalog** (00-original-plan.md:1733-1736).
  Resolve a locator descriptor to a statement index within a resolved function:
  - `function_entry` / `function_exit` → the function's first/last statement by `idx`.
  - `first_call_to(fn)` / `last_call_to` / `call_to` (error if multiple) → match
    `statements.callee`.
  - `first_return` / `last_return` → `statements.kind` (return).
  - **`return_value(v)`** → the return operand, matched from **`statements.pseudo_text`**
    (the operand is in `pseudo_text`, not a structured column — step 1 ships it).
  - `references_string(s)` / `first_read_of_cvar(name)` → `statements.string_ref`.
  - `matching{...}` — the general matcher over EVERY §9.3 key: `kind=` →
    `statements.kind`; `callee=` → `statements.callee`; `references_string=` /
    `reads_cvar=` → `statements.string_ref`; **`condition_contains=`** →
    `statements.pseudo_text` (the branch condition text lives in `pseudo_text`).
  - `matching_pattern("48 8B C1 …")` → the labeled expert raw-AOB hatch (not a
    statement-content form; resolved against bytes, not the statement metadata).

  All statement-content keys are backed by a shipped column: `kind`/`callee`/
  `string_ref`/`byte_range_len` (structured) + `pseudo_text` (the `return_value` operand
  and the `condition_contains` text). No §9.3 locator form is unresolvable.
- **Per-statement reads** — expose `kind`, `callee`, `string_ref`, `byte_range_start`,
  `byte_range_len` for a resolved statement (the `kcdx.op.*` fit decision reads
  `byte_range_len`).
- **Captures** — join `referenced_vars` by `(address_version_id, statement_idx)` to
  return a resolved statement's captured vars (`var_name` / `storage_kind` /
  `storage_detail` / `size_bytes` / `data_type`) for the captures-by-name thunk.
- **Hot-path discipline** — the per-call captures thunk is built ONCE at install from
  the resolved-at-startup data; no per-call SQLite query, no per-call allocation
  (`.claude/rules/memory.md`, `.claude/rules/logging.md`). Resolution itself is a
  startup/install-time concern, not a per-call one.
- Reads against the **pinned column contract** ([`../plan-spec.md`](../plan-spec.md)
  §"The column contract") — exactly the columns step 1 ships.

## Test bar (runs AT this step)

A `test-plugins/cap-NN-stmt-resolve/` regression plugin (next free cap-NN,
suite-gated): for a known curated function with statement coverage, the API resolves a
representative locator (e.g. `first_call_to(<known-callee>)`) to the EXPECTED statement
index and returns its `byte_range_len` + the expected captured-var set — a row per
checked locator family that FAILS if resolution returns the wrong statement or no
statement. Self-reports the canonical `ACCEPT-RESULT` / `ACCEPT-SUITE` lines into
`kcdx-dev.log` (`.claude/rules/acceptance-signal.md`). PROBE Q silent. Confirmed by the
user's launch + the agent's log read.

**Parallel-build note:** the resolution code is developed against the existing DEV-DB
schema (which already carries the statement tables) before step 1 lands; the *final
suite-green acceptance* reads the shipped `reference.sqlite` and so gates on step 1
being deployed (the column contract is the boundary — see
[`../plan-spec.md`](../plan-spec.md) §"Lane ownership").

## Dependencies

Step 1 (the curated statement subset present in the shipped `reference.sqlite` with the
pinned columns) — for the runtime acceptance. The resolution *code* depends only on the
column contract, which exists in the DEV DB today (parallel development). Phase 9.1's
refdb address-resolution cache (`498934c`, DONE) — this extends it.

## Design authority

[`../plan-spec.md`](../plan-spec.md) §"The column contract" + §"Settled design
decisions" (decision 2, eager-load). The Phase 9.3 design source:
[`../../restructure/00-original-plan.md`](../../restructure/00-original-plan.md)
§"Phase 9.3" → the `kcdx.locator.*` catalog (the locator forms this API resolves), the
`kcdx.op.*` paragraph (`byte_range_len` consumer), and the captures paragraph (the
`insert_before/after` captures-by-name thunk). `.claude/rules/hook-engine.md` +
`.claude/rules/memory.md` (hot-path). Build to those §s, not this summary.

## Disassembler-test / author-burden note

Author-invisible at this layer — the API backs the Phase 9.3 locator surface; the
author writes `first_call_to("IsInCombat")`, never a statement index or a byte offset
(`.claude/rules/cornerstones.md`, AP12). The engine carries the statement metadata and
resolves it. No author hex. No new DB rows (consumes step 1's projection).
