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
| 2 author per-kind columns (keep value/offset/vtable_slot + add struct_offset) + engine read sync + validators | NOT STARTED | — |

## Step docs

2. [step-2-explicit-columns.md](step-2-explicit-columns.md)

## Verification gate (phase end)

- Every per-kind datum column the Phase-1 call-site audit confirmed exists as an
  AUTHORED column in `data/seeds/address_versions_seed.csv`'s header + in
  `seeds_shared/schema.py`'s `address_versions` list; `struct_offset` added;
  `value`/`offset`/`vtable_slot` KEPT (not deleted).
- The C++ engine read-side is in sync: `refdb.cpp`'s `kVersionSelectColumns` +
  column-bind + `NameResolution` carry the column set (append-only per AP11). A
  schema/SELECT mismatch is a DB-load failure — both move together.
- `seeds_shared/validators.py` validates each per-kind column (shape + which
  kinds carry it), a FORMAT validator on an authored column — never a prose parse.
- **FULL build green** (`pwsh ./build.ps1` exit 0 + artifacts) — this phase
  touches engine code, so the build gate is mandatory, not oracle-only. PLUS all
  mini-dump apply oracles + `test_rebuild_oracle` green; `test_rebuild_oracle`
  byte-identical after a DELIBERATE, documented `oracle_baseline.json` re-capture
  for the column-set change (existing rows' new columns empty).
- No value authored yet and no regex removed — that is Phase 3.
