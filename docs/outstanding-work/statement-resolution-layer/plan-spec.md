# Plan spec — statement-resolution layer (Phase 9.3 prerequisite)

## Goal

Ship the curated-function statement metadata into the production reference DB and
expose it through a refdb statement-resolution API, so the Phase 9.3
`kcdx.locator.*` / `kcdx.op.*` / `kcdx.statement.*` author surface can resolve a
locator to a statement at runtime. This is the prerequisite that unblocks Phase 9.3
steps 1 (locator), 2 (op), and 5 (statement).

## Why this exists (the drift it reconciles)

The Phase 9.3 design specifies that game-DLL functions are populated **eagerly at
engine startup from `data/reference.sqlite`**, carrying "names + signatures + **statement
metadata**" (`../restructure/00-original-plan.md` §"Phase 9.3", the
`kcdx.functions.WHGame.*` paragraph). A locator like
`kcdx.locator.first_call_to("IsInCombat")` resolves to a **statement within a
function**; `kcdx.op.*` reads the statement's `byte_range_len`; insert-callbacks read
the statement's captured variables.

The production `data/reference.sqlite` does **not** carry statement data today — the
`statements` / `referenced_vars` tables are DEV-only (`USER_TABLES` excludes them).
That exclusion was scoped to the **5.24M-row bulk** discovery corpus (correctly
DEV-only — Phase 9.4's `kcdx.find` job) and over-reached to the **curated subset** the
Phase 9.3 surface needs. The Phase 9.1 README records the exclusion as blanket
"DEV-only, not consumed by production" — true for the bulk, wrong for the curated
slice the Phase 9.3 design requires shipped.

**This plan reconciles the Phase 9.1 USER projection back to the Phase 9.3 design.**
It is not a new design decision: the bulk stays DEV-only; the curated 133-function
slice (~0.44 MB with the shipped columns) ships, exactly as the Phase 9.3 spec already
states.

## Settled design decisions (verbatim, with source)

1. **The curated-function statement subset ships to `reference.sqlite`; the 5.24M
   bulk stays DEV-only.** Source: user decision 2026-06-07, grounded in
   `../restructure/00-original-plan.md` §"Phase 9.3" (line ~1679, the
   eager-populate-from-`reference.sqlite` paragraph) + the measured ~0.44 MB curated
   subset (vs 1.3 GB bulk). The bulk's DEV-only status is unchanged
   (`../parallel-ghidra-research.md` §11.8 Track 3 / `kcdx.find`, Phase 9.4).

2. **refdb eager-loads the curated statement data at engine startup**, mirroring the
   `kcdx.functions.*` namespace population the Phase 9.3 design already specifies as
   eager. Source: user decision 2026-06-07. Rejected alternative: lazy on-first-use
   per-function SQLite query (leaner resident memory, but a first-use stall and a
   departure from the spec's stated eager model).

3. **Pinned runtime column contract** — the USER projection ships only the columns
   refdb reads (§"The column contract" below), dropping `content_hash` / the redundant
   `kcdx_id` on the two statement tables. `pseudo_text` IS shipped (curated subset
   only): it is the sole carrier of the data two §9.3 runtime locator forms need —
   `kcdx.locator.return_value(v)` (the return operand) and
   `matching{condition_contains=}` (the branch condition text). Source: user decision
   2026-06-07 (the initial "drop pseudo_text" framing was corrected by the gated
   coverage review, which found those two runtime locator forms strand without it —
   verified: 0 of 142 curated branch statements carry the condition outside
   `pseudo_text`, 0 of 171 return statements carry the value outside it). Curated
   subset is ~0.44 MB (vs the 1.3 GB bulk — negligible). The "`pseudo_text` is
   discovery-only" rationale is true for the Phase 9.4 `kcdx.find` BULK, NOT for the
   two curated runtime locator forms — the same bulk-vs-runtime over-reach this whole
   plan reconciles.

## The column contract (the cross-lane invariant)

The USER projection of `data/reference.sqlite` ships these tables, **curated rows
only** (rows whose `address_version_id` belongs to a curated entity), **these columns
only**. This is the contract the engine-side refdb API (step 2) reads against; the
DB-side projection (step 1) produces exactly this. A deviation is a contract FAIL on
step 1's acceptance test, caught before it reaches the engine read.

- `statements`: `id`, `address_version_id`, `idx`, `kind`, `callee`, `string_ref`,
  `byte_range_start`, `byte_range_len`, `pseudo_text`. **Dropped:** `content_hash`,
  `kcdx_id`. (`pseudo_text` ships because it is the sole backing of
  `return_value(v)` + `matching{condition_contains=}` — see decision 3.)
- `referenced_vars`: `id`, `address_version_id`, `statement_idx`, `var_name`,
  `storage_kind`, `storage_detail`, `size_bytes`, `data_type`. **Dropped:** `kcdx_id`.
- `call_edges`: **NOT shipped** (stays DEV-only — Phase 9.4).
- The `_dict_statements_kind` / `_dict_referenced_vars_storage_kind` /
  `_dict_referenced_vars_data_type` lookup tables are already present in
  `reference.sqlite`; no new dict work.

Full per-column rationale + the verified row counts live in the step-1 hand-off:
[`HANDOFF-db-curated-statements.md`](HANDOFF-db-curated-statements.md).

## Verified data facts (game version `release_1_5_1164953_841`, 2026-06-07)

Measured against `data/reference-dev.sqlite` (the populated DEV DB) and
`data/reference.sqlite` (the current shipped DB):

- Curated entities: 157 `address_names` → 157 curated `address_versions`.
- Curated functions WITH statement coverage: **133** (the ~24 without are non-function
  kinds — vtable/data slots — that carry no statements).
- Curated `statements` rows: **2,385**. Curated `referenced_vars` rows: **5,595**.
- Curated subset size in `reference.sqlite` with the pinned columns (incl.
  `pseudo_text`): **~0.44 MB**.
- The bulk `statements` table (DEV): 5,240,326 rows — stays DEV-only.

## Cross-step invariants

- **The bulk never leaks into USER.** Every step that touches the projection asserts
  `reference.sqlite`'s `statements` count is the curated count (~2,385), not the
  millions.
- **The column contract is verbatim.** Step 1 produces exactly the §"column contract"
  set; step 2 reads exactly it; step 3 documents exactly it. A column added/dropped is
  a contract change requiring both lanes' agreement.
- **No new seed rows / no AP18.** This re-projects existing dumped data; it adds no
  curated entity. Not an Address Library row addition.

## Lane ownership

- **Step 1 (DB ship)** — the **DB / extractor lane** (owner of `data/seeds/`,
  `data/refdata-extractor/`, `data/reference.sqlite`). Spec'd by
  [`HANDOFF-db-curated-statements.md`](HANDOFF-db-curated-statements.md).
- **Steps 2 + 3 (refdb API, doc reconcile)** — the **engine / consumer lane**.
- The two lanes meet at the column contract above; both build in parallel, the
  engine's runtime acceptance gating on step 1's deploy.

## Coverage map

| Design element (source) | Covered by | Notes |
|---|---|---|
| Curated statement metadata resident in shipped `reference.sqlite` (§9.3 eager-populate paragraph; decision 1) | Step 1 | The deliverable; pinned columns per the contract |
| `referenced_vars` captures data shipped curated (§9.3 captures paragraph) | Step 1 | Same curated row-filter |
| Bulk 5.24M `statements` + `call_edges` stay DEV-only (§9.4 / `kcdx.find`; decision 1) | Step 1 | Excluded by the curated row-filter; `call_edges` not in USER_TABLES |
| Pinned column contract enforced in the shipped DB (decision 3 — incl. `pseudo_text`) | Step 1 | Acceptance test asserts exact columns; a stray column OR a missing `pseudo_text` FAILs |
| Locator → statement-index resolution, FULL §9.3 catalog (§9.3 `kcdx.locator.*` catalog, lines 1733-1736) | Step 2 | refdb resolves `function_entry/exit`, `first/last_call_to`, `call_to`, `first/last_return`, and the `matching{}` matcher against `statements` |
| `return_value(v)` + `matching{condition_contains=}` locator forms (§9.3 lines 1734, 1736) | Step 2 (data: Step 1 ships `pseudo_text`) | Both resolve only from `statements.pseudo_text` — shipped per decision 3; refdb matches the return operand / condition text |
| `matching{reads_cvar=}` / `references_string` / `first_read_of_cvar` (§9.3 lines 1734, 1736) | Step 2 | Resolved via `statements.string_ref` (shipped) |
| `statements.byte_range_len` read for op fit (§9.3 `kcdx.op.*` paragraph) | Step 2 | refdb exposes per-statement `byte_range_len` |
| Captures-by-name join from `referenced_vars` (§9.3 captures paragraph) | Step 2 | refdb joins `referenced_vars` by `statement_idx` |
| Eager-load at startup, mirroring functions namespace (decision 2; §9.3 eager-populate) | Step 2 | Load at engine startup alongside `kcdx.functions.*` population |
| Phase 9.1 "DEV-only" drift reconciled vs §9.3 design | Step 3 | README line corrected: bulk DEV-only, curated subset ships |
| §9.3 step docs' `statements.captures` → `referenced_vars` correction | Step 3 | The column is `referenced_vars` (joined), not `statements.captures`; `applicable_ops` was dropped |

Every design element resolves to a step. Nothing deferred or out-of-scope.

## What this prerequisite unblocks

On completion, Phase 9.3 steps 1 (`kcdx.locator.*`), 2 (`kcdx.op.*`), and 5
(`kcdx.statement.*`) become buildable — they resolve against the now-shipped statement
metadata through the refdb statement-resolution API. Phase 9.3 step 3
(`kcdx.dll.declare` keystone) was never blocked (it rides `address_names`/`versions`,
no statement data). See `../restructure/phase-09.3-namespaces/`.
