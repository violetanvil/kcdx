# Phase 1 — data-core

**Intent.** Build the headless, Qt-free authoring core in
`data/refdata-extractor/python/seeds_shared/`: the DB→CSV exporter, the
bidirectional byte-identity round-trip oracle, and the validated atomic
audit-trio DB-edit. The entire authoring path is exercisable + proven with zero
Qt before any GUI exists (`.claude/rules/headless-testable.md`, design §5). Each
step ships a `data/refdata-extractor/tests/test_*.py` oracle joining the existing
oracle tree (mini-dump fixture at `tests/fixtures/mini-dump/`).

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 csv_exporter.py — DB → 3 seed CSVs, deterministic + diff-preserved | NOT STARTED | — |
| 2 round-trip oracle — bidirectional byte-identity (export/import) | NOT STARTED | — |
| 3 db_editor.py — validated atomic audit-trio UPDATE (Job-2 write) | NOT STARTED | — |

## Step docs

1. [step-1-csv-exporter.md](step-1-csv-exporter.md)
2. [step-2-round-trip-oracle.md](step-2-round-trip-oracle.md)
3. [step-3-db-editor.md](step-3-db-editor.md)

## Verification gate (phase end)

- `csv_exporter.py` and `db_editor.py` exist in `seeds_shared/`, headless +
  Qt-free, consuming the shared `validators.py` / `row_builder.py` / `schema.py`.
- The bidirectional byte-identity round-trip holds on the mini-dump fixture:
  `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs` (diff-preserved —
  row order, `#`-comments, `QUOTE_MINIMAL`, trailing newline).
- Every Phase-1 oracle is green: the new `test_csv_exporter.py`,
  `test_round_trip.py`, `test_db_editor_reverify.py`, PLUS the existing
  `test_rebuild_oracle.py` / `test_apply_*.py` still green (no regression).
- This phase ships NO UI and writes no Qt. Acceptance is the oracle suite — no
  game launch, no user gesture (the data-core is headless by construction).
