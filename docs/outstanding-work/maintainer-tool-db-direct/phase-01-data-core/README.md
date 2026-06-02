# Phase 1 — data-core

**Intent.** Build the headless, Qt-free authoring core in
`data/refdata-extractor/python/seeds_shared/`: the DB→CSV exporter, the
bidirectional byte-identity round-trip oracle, the validated atomic `db_editor`
write shapes for the whole six-job catalog (version-row UPDATE, INSERT for new
version/entity, lifecycle UPDATE for supersede/deprecate), and the field-delta
computation the confirm surface reads. The entire authoring path is exercisable +
proven with zero Qt before any GUI exists (`.claude/rules/headless-testable.md`,
design §5). Each step ships a `data/refdata-extractor/tests/test_*.py` oracle joining
the existing oracle tree (mini-dump fixture at `tests/fixtures/mini-dump/`).

Shared spec: [`../plan-spec.md`](../plan-spec.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1b importer: NULL signature on curated function-kind rows with a blank seed cell | DONE | (landed) |
| 1 csv_exporter.py — DB → 3 seed CSVs, deterministic + diff-preserved | NOT STARTED | — |
| 2 round-trip oracle — bidirectional byte-identity (export/import) | NOT STARTED | — |
| 3 db_editor.py — validated atomic version-row UPDATE (audit-trio + full-column) | NOT STARTED | — |
| 4 db_editor.py — validated atomic INSERT (new version Job 6 + new entity Job 1) | NOT STARTED | — |
| 5 db_editor.py — validated atomic lifecycle UPDATE (supersede/deprecate Jobs 4/5) | NOT STARTED | — |
| 6 field_delta.py — saved-vs-prospective field-delta computation (D8) | NOT STARTED | — |

## Step docs

1b. [step-1b-importer-null-blank-signature.md](step-1b-importer-null-blank-signature.md)
1. [step-1-csv-exporter.md](step-1-csv-exporter.md)
2. [step-2-round-trip-oracle.md](step-2-round-trip-oracle.md)
3. [step-3-db-editor-update.md](step-3-db-editor-update.md)
4. [step-4-db-editor-insert.md](step-4-db-editor-insert.md)
5. [step-5-db-editor-lifecycle.md](step-5-db-editor-lifecycle.md)
6. [step-6-field-delta.md](step-6-field-delta.md)

## Verification gate (phase end)

- `csv_exporter.py`, `db_editor.py`, `field_delta.py` exist in `seeds_shared/`,
  headless + Qt-free, consuming the shared `validators.py` / `row_builder.py` /
  `schema.py` (no rule reimplemented).
- The bidirectional byte-identity round-trip holds on the mini-dump fixture:
  `import(export(DB)) == DB` AND `export(import(CSVs)) == CSVs` (diff-preserved —
  row order, `#`-comments, `QUOTE_MINIMAL`, trailing newline).
- Every `db_editor` write shape is oracle-proven on the mini-dump fixture:
  version-row UPDATE (audit-trio + full column), INSERT (new version + new entity,
  with id-assignment + tuple-uniqueness + AP18-surfacing), lifecycle UPDATE
  (supersede/deprecate pair-integrity + acyclicity). An invalid edit of any shape
  aborts with NO write (the DB is byte-identical to pre-action).
- The field-delta computation produces the correct `field: old → new` set for each
  edit shape (oracle-tested).
- Every Phase-1 oracle is green: the new `test_csv_exporter.py`,
  `test_round_trip.py`, `test_db_editor_*.py`, `test_field_delta.py`, PLUS the
  existing `test_rebuild_oracle.py` / `test_apply_*.py` still green (no regression).
- This phase ships NO UI and writes no Qt. Acceptance is the oracle suite — no
  game launch, no user gesture (the data-core is headless by construction).
