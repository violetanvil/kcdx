# Phase 2 — schema: explicit per-kind columns

**Intent.** Add the explicit authored per-kind columns the Phase-1 audit
specified to the versions seed CSV header + the `seeds_shared` schema, collapse
the overlapping `value`/`offset`/`vtable_slot` sprawl into the explicit columns
(decision 2), and have the validators enforce each new column. Columns EXIST
after this phase but are still NULL for the existing rows — no value authoring,
no reader rewiring yet. The phase ends buildable: rebuild + apply still produce
the same rows (the new columns are present-but-empty), oracles green.

Shared spec: [`../context.md`](../context.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 2 add explicit per-kind columns + collapse sprawl + validators | NOT STARTED | — |

## Step docs

2. [step-2-explicit-columns.md](step-2-explicit-columns.md)

## Verification gate (phase end)

- The new per-kind columns exist in `data/seeds/address_versions_seed.csv`'s
  header and in `seeds_shared/schema.py`'s `address_versions` column list, named
  per the Phase-1 plan; the `value`/`offset`/`vtable_slot` sprawl is collapsed
  per decision 2.
- `seeds_shared/validators.py` validates each new column (shape + which kinds may
  carry it), as a FORMAT validator on an authored column — never a prose parse.
- All oracles green; `test_rebuild_oracle` byte-identical after a DELIBERATE,
  documented `oracle_baseline.json` re-capture for the column-shape change (the
  existing rows' new columns are empty, so the only baseline diff is the schema
  column set).
- No value authored yet and no writer rewired — that is Phase 3.
