# Step 2 — add explicit per-kind columns + collapse sprawl + validators

**What.** Implement the Phase-1 column plan: add the explicit authored per-kind
columns to the versions seed CSV + the `seeds_shared` schema, collapse the
overlapping `value`/`offset`/`vtable_slot` columns into the explicit per-kind
set, and make the validators enforce each. Columns are present but empty for
existing rows; no value authoring, no reader rewiring.

**Scope (commit-grain).**
- Add the new per-kind column(s) named in the Phase-1 plan to
  `data/seeds/address_versions_seed.csv`'s header (all existing rows get the new
  empty cell(s) — a pure header/width change, values stay blank).
- Add the same columns to `seeds_shared/schema.py`'s `address_versions` column
  list (+ `USER_COLUMNS` projection as the plan dictates); collapse the
  `value` / `offset` / `vtable_slot` sprawl into the explicit per-kind columns
  per decision 2 (remove the overloaded column(s) the plan retires; keep the
  schema's BLOB/INTEGER/TEXT typing law intact).
- `seeds_shared/validators.py`: a FORMAT validator per new column — shape check +
  which kinds may carry it (e.g. only `vtable_index` carries the slot column).
  These validate an authored column; they are NOT prose parsers (distinct from
  the out-of-scope legitimate validators in `../context.md`).
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
