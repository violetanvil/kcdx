# 2.3 [TEST] Repoint the broken rebuild tests + the paused 6.2 batch tests → db-export

## What

Repoint every rebuild-based pytest off the retired `data/seeds/` to the db-export genesis: the
pre-existing rebuild tests that currently ERROR on the deleted path (`test_deferred_commit.py`,
`test_db_editor_update.py`, `test_confirm_endpoint.py`, `test_rebuild_oracle`/`test_round_trip`,
`test_csv_exporter`, …) AND the PAUSED Phase-6 step-6.2 batch tests (`test_db_editor_batch.py`,
`test_confirm_batch_endpoint.py`) that were written against `data/seeds/`. Each test's
`REAL_SEED_DIR` / seed-baseline source moves to the db-export CSVs (curated + bulk as the rebuild
needs). After this, the full rebuild-based pytest suite is green — unblocking Phase 6 / step 6.2 +
clearing the pre-existing breakage the half-done deprecation caused.

## Scope

One commit across the affected test files in `data/refdata-extractor/tests/` +
`data/maintainer-tool/backend/tests/`: the `REAL_SEED_DIR` constant / the `_copy_seeds` /
seed-baseline-rebuild fixtures repoint from `data/seeds/` to the db-export genesis. The TEST LOGIC
(what each asserts) is unchanged — only the baseline SOURCE moves. The paused 6.2 batch tests
(uncommitted in the Phase-6 work) are repointed here so they run against the migrated baseline. Does
NOT change production code (2.1/2.2 did that) — this is the test-fixture repoint.

## Test bar

The full data-core + backend pytest suite green against the db-export baseline: every previously-
`data/seeds/`-reading test now resolves db-export and passes; the 6.2 batch tests
(`test_db_editor_batch` all-or-nothing + `test_confirm_batch_endpoint`) pass against the migrated
genesis. Emits the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE`. FALSIFIABLE: any rebuild-based test
still pointing at `data/seeds/` (erroring on the deleted path), or a 6.2 batch test failing against
db-export, fails the row. Per `.claude/rules/test-discipline.md` (the same-change test bar — the
tests ARE the deliverable here) + `.claude/rules/incremental-delivery.md` (runnable only after
2.1/2.2 land the production repoint).

## Dependencies

- **2.1** — `import_to_sqlite` constants point at db-export (the rebuild the tests drive).
- **2.2** — the backend resolves db-export (the backend tests' subject).
- **1.1–1.4** — the db-export genesis + the bulk CSVs + the widened oracle exist (the baseline the
  tests rebuild from).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the test-repoint element; the 6.2-unblock
note).

## Design authority

`data/maintainer-tool/design.md` **D38** — the tests' rebuild baseline is the db-export genesis D38
established. The test logic is unchanged; only the baseline source repoints. Build to D38.

## Disassembler-test / author-burden

None — test-fixture path repoint; no author-facing input, no game-function target, no AP18 addition.
