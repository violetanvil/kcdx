# 2.1 [CORE] Repoint import_to_sqlite SEED_DIR / *_SEED_CSV constants → db-export

## What

Repoint the `import_to_sqlite.py` seed-path constants — `SEED_DIR`, `MODULE_SEED_CSV`,
`ADDRESS_NAMES_SEED_CSV`, `ADDRESS_VERSIONS_SEED_CSV` — from `data/seeds/` to the db-export genesis
(the curated CSVs at `data/db-export/`; the bulk constants for `data/db-export-bulk/` as the bulk
genesis needs). 1.3 changed `run_rebuild`'s genesis LOGIC (read CSVs not dump); this step moves the
PATH CONSTANTS those + any other seed-path readers resolve, so nothing in the toolchain points at the
retired `data/seeds/`.

## Scope

One commit in `data/refdata-extractor/python/import_to_sqlite.py` (+ any module importing those
constants): the four `SEED_DIR`/`*_SEED_CSV` constants repointed to the db-export locations; any
remaining `data/seeds/` literal in the toolchain's rebuild/read path updated. Does NOT touch the
backend (2.2) or the test fixtures (2.3). Kept separate from 1.3 so the genesis-logic change and the
path-constant change are each one clean, revertible commit.

## Test bar

Data-core pytest: a rebuild/import driven through the repointed constants resolves the db-export CSVs
(not `data/seeds/`) and succeeds; a grep-style assertion (or a test) that no `data/seeds/` path
literal remains in `import_to_sqlite`'s active rebuild path. Emits the canonical
`ACCEPT-RESULT`/`ACCEPT-SUITE`. FALSIFIABLE: a constant still pointing at `data/seeds/`, or an import
that resolves the retired path, fails the row. Per `.claude/rules/test-discipline.md`.

## Dependencies

- **1.3** — `run_rebuild` reads the CSVs as genesis (the logic these constants feed).
- **1.1 / 1.2** — the db-export-bulk CSVs exist + are LFS-tracked (the paths the bulk constants point
  at resolve to real files).

## Reference

[`../plan-spec.md`](../plan-spec.md) — the coverage map (the import_to_sqlite SEED_DIR element) +
cross-step invariant 5 (the direct-write mechanism unchanged; only paths/genesis move).

## Design authority

`data/maintainer-tool/design.md` **D38** §"`run_rebuild`'s genesis changes... to `data/db-export/` +
`data/db-export-bulk/`" — the path constants resolve to D38's genesis locations. Build to D38.

## Disassembler-test / author-burden

None — path-constant repoint; no author-facing input, no game-function target, no AP18 addition.
