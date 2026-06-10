# Phase 2 — The backend + toolchain migration off data/seeds

**Intent:** with the tracked-CSV genesis built + round-trip-proven (Phase 1), move every in-scope
TOOLCHAIN + BACKEND reference off `data/seeds/` onto the db-export paths: the `import_to_sqlite`
seed-path constants, the backend (`config.py`/`csv_integrity.py`/`routes_confirm.py` + the ~6 tests
that assert `seed_dir must stay at data/seeds`), and the broken rebuild tests + the paused Phase-6
6.2 batch tests. After this phase the toolchain + backend run on db-export and every rebuild-based
pytest is green (currently all error on the deleted `data/seeds/`). Gated by the data-core pytest +
the backend test.

**Design authority:** `data/maintainer-tool/design.md` **D38**. The path constants + test fixtures
move to the db-export genesis Phase 1 established. (The GOVERNANCE/design prose sweep —
`address-library.md`, `CLAUDE.md`, `policy.md` relocation/links, the public-private carve-out — is
DEFERRED, a separate later sweep; this phase is toolchain + backend only.)

**Precondition:** Phase 1 green (the exporter writes the bulk, `run_rebuild` reads the CSVs, the
widened oracle is green). These steps repoint consumers onto that genesis; they are verifiable only
after it exists.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 [CORE] Repoint import_to_sqlite SEED_DIR / *_SEED_CSV constants → db-export](step-1-import-seed-dir.md) | DONE | 076834e — SEED_DIR data/seeds → data/db-export (the 3 derived *_SEED_CSV follow); no active data/seeds literal remains in the toolchain rebuild path; 2 stale prose comments updated. Test: 10 ACCEPT items incl. a rebuild-through-default-constants resolution proof (falsifiable — un-repointed FileNotFoundErrors). Net: 6→4 failed, +10 passing. Step-review PROCEED. SURFACED (user's call, separate commit): test_rebuild_oracle red is now a pre-existing address_versions baseline drift the repoint uncovered (db-export CSV maintainer-edited post-baseline), NOT repoint-caused — a deliberate oracle baseline re-capture. |
| [2.2 [BE] Migrate the backend (config / csv_integrity / routes_confirm) + its tests off data/seeds](step-2-backend-config.md) | DONE | 7f26259 — config.py _SEED_SUBDIR → db-export; under D38 (supersedes D20) seed_dir == db_export_dir == data/db-export (the curated CSVs are both genesis + record; D1 holds, DBs stay out of git). csv_integrity/routes_confirm comment-only (parameterized); stale D20 prose + SETUP ledger swept (deletion-hygiene). ~6 backend tests flipped to assert db-export + REAL_SEED_DIR→db-export + the _git safe.bareRepository=all fix (greens test_confirm_endpoint git-push tests). Gate: backend 69/69. Step-review PROCEED. |
| [2.3 [TEST] Repoint the broken rebuild tests + the paused 6.2 batch tests → db-export](step-3-test-repoint.md) | DONE | d19ad50 — the remaining 9 data-core test files' REAL_SEED_DIR → db-export (one identical line each). The 6.2 batch tests + the backend tests were repointed in their own commits (42ebd79 / 7f26259); test_rebuild_oracle was fixed by the baseline re-capture (d8aae77). Gate: FULL data-core suite GREEN — 177 passed, 0 errors (the 73 data/seeds FileNotFoundError errors cleared). Step-review PROCEED. |

## Phase verification gate

Phase 2 is done when: `import_to_sqlite`'s seed-path constants point at the db-export genesis (2.1);
the backend (config/csv_integrity/routes_confirm) + its ~6 `seed_dir`-asserting tests run on
db-export, green (2.2); and the broken rebuild tests + the paused 6.2 batch tests (`test_db_editor_batch`,
`test_confirm_batch_endpoint`, `test_deferred_commit`, `test_confirm_endpoint`, …) pass against the
db-export baseline (2.3) — unblocking Phase 6 / step 6.2 + the pre-existing rebuild-test breakage.
Gated by the data-core pytest + the backend test. No in-scope toolchain/backend reference to
`data/seeds/` remains after this phase (the governance refs are the deferred sweep).
