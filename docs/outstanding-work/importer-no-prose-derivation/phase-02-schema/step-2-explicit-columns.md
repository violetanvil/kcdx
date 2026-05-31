# Step 2 — add explicit per-kind columns + collapse sprawl + validators

**What.** Implement the Phase-1 column plan: add the explicit authored per-kind
columns to the versions seed CSV + the `seeds_shared` schema, collapse the
overlapping `value`/`offset`/`vtable_slot` columns into the explicit per-kind
set, and make the validators enforce each. Columns are present but empty for
existing rows; no value authoring, no reader rewiring.

**Scope (commit-grain).** Per the CORRECTED comprehensive column plan in
`../context.md` (keep + author the engine-read columns; the delete-lean is
superseded — these fields are load-bearing for the downstream hardcoded-address
migration). The EXACT column set is whatever Phase 1's call-site-data audit
finalized; the below is the expected shape:
- For EVERY per-kind datum column Phase 1 confirmed, ensure
  `data/seeds/address_versions_seed.csv` has an explicit AUTHORED column:
  `vtable_slot`, `offset`, `value` (these already exist in the DB schema but were
  not authored from the seed — add/confirm the seed-CSV header column for each),
  and ADD `struct_offset` (new column — the audit's vtable-byte-offset kind has
  no home today). All existing rows get empty cells (values authored in Step 3 /
  by the migration).
- In `seeds_shared/schema.py`: keep `value`/`offset`/`vtable_slot`; ADD
  `struct_offset` (INTEGER) to the `address_versions` list + `USER_COLUMNS`.
  Keep the BLOB/INTEGER/TEXT typing law.
- In `src/refdb.cpp`: extend `kVersionSelectColumns` + the column-bind block +
  `NameResolution` (append-only, AP11) to carry `struct_offset` — the engine
  read-side stays in sync with the schema (a column the DB has but the SELECT
  omits, or vice versa, is a load failure). This makes Step 2 a coordinated
  schema + engine-read change → the FULL build gate (`pwsh ./build.ps1`), not
  oracle-only.
- In `seeds_shared/row_builder.py`: `build_curated_row` reads each authored
  per-kind column straight from the seed cell (stop the `value = vtable_slot`
  mirror — `value` becomes its own authored datum); `build_bulk_row` carries the
  columns as NULL for bulk.
- `seeds_shared/validators.py`: a FORMAT validator per per-kind column (shape +
  which kinds may carry it). Authored-column validators, NOT prose parsers.
- Keep `row_builder.build_curated_row` / `build_bulk_row` writing the new columns
  as NULL for now (no value source yet) so rebuild + apply still emit the same
  rows; the rewire to READ authored values is Step 3.
- Re-capture `oracle_baseline.json` for the column-set change (DELIBERATE; note
  the reason in the capture docstring/provenance per the existing baseline
  convention).

**Test bar.** The four mini-dump apply oracles (`test_apply_add_entity`,
`test_apply_reverify`, `test_survival_table`, `test_apply_deprecate_supersede`)
+ the full-dump `test_rebuild_oracle` all PASS; `test_rebuild_oracle`
byte-identical to the re-captured baseline. The smallest layer exercising this is
those oracle suites (the importer has no unit harness below them); a column added
but unreadable must not change any emitted row value (only the column set).

**Dependencies.** Step 1 (the audit names the exact columns to add + what the
sprawl collapses into).

**Reference.** [`../context.md`](../context.md) decision 2 + finding F4 (the
sprawl). Schema: `seeds_shared/schema.py`; validators:
`seeds_shared/validators.py`; the shared builder: `seeds_shared/row_builder.py`.
