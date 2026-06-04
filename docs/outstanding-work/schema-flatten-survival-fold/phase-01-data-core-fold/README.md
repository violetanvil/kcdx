# Phase 1 — the data-core fold

**Intent.** Land the survival fold in the data-core, additively and provably: add the folded
columns to `address_versions`, populate them on the av row from the per-kind dispatch WHILE
dual-writing the `survival` table (so the equivalence is provable), and carry them through the
exporter + round-trip. End state: the av row carries every survival fact, PROVEN equal to the
still-present `survival` table — the deletion (Phase 3) is then safe.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §11.1 / §11.2.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 schema + row-builder — add aob/anchor_string/rule/slot_count/expect_unique + derives_from (self-FK) to address_versions (SCHEMA + USER_COLUMNS + DICT_COLS); additive, no reader yet | NOT STARTED | — |
| 2 importer populate-on-av + dual-write — _apply_one_db + rebuild write the per-kind survival cells onto the av row (reusing _KIND_TO_FORM); survival table still written in parallel; assert av-columns == survival-row (the 157/157 equivalence) | NOT STARTED | — |
| 3 exporter + round-trip — csv_exporter emits/reads the folded columns; round_trip asserts byte-identity with them present | NOT STARTED | — |

## Step docs

1. [step-1-schema-add-folded-columns.md](step-1-schema-add-folded-columns.md)
2. [step-2-importer-populate-on-av-dual-write.md](step-2-importer-populate-on-av-dual-write.md)
3. [step-3-exporter-roundtrip-folded-columns.md](step-3-exporter-roundtrip-folded-columns.md)

## Verification gate (phase end)

- `address_versions` carries the folded columns (`aob`/`anchor_string`/`rule`/`slot_count`/
  `expect_unique`/`derives_from`); the rebuild oracle is green (re-captured to reflect the
  additive columns, deliberate + inspected per the oracle's BASELINE PROVENANCE rule).
- The populate step's equivalence assertion is GREEN: each curated av row's folded cells
  match its `survival` row, row-for-row (the proof the deletion is safe).
- Convergence holds: direct-write == seed-rebuild byte-identity for the av columns
  (`_db_fingerprint` whole-table oracle).
- Export + round-trip carry the folded columns (`test_round_trip` green with them present).
- This phase ships NO engine change and does NOT delete the survival table — the sibling is
  still written in parallel (dual-write); deletion is Phase 3 after consumers migrate.
