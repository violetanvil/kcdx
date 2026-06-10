# 1.3 [CORE] Repoint run_rebuild genesis to the tracked CSVs; the dump → expert-only

## What

Change `run_rebuild`'s genesis from `Ghidra dump + data/seeds/*.csv → both DBs` to
`data/db-export/ (curated) + data/db-export-bulk/ (bulk) → both DBs` (D38). The rebuild now reads
the tracked CSV export — the curated CSVs for the curated overlay, the bulk CSVs (from 1.1, LFS from
1.2) for the bulk corpus — and constructs both `reference.sqlite` + `reference-dev.sqlite` with NO
dump. The from-dump path is RETAINED but demoted to an **expert-only mode** that REGENERATES the
bulk CSVs when the dump itself changes (a new game version's fresh disassembly) — it is no longer the
routine rebuild input.

## Scope

One commit in `data/refdata-extractor/python/import_to_sqlite.py`: `run_rebuild` reads the bulk +
curated CSVs as its row source instead of the dump + seeds (reuse `build_rows`' row-encode, fed from
the CSVs rather than `iter_table(dump_dir)`); the from-dump `build_rows(dump_dir)` path moves behind
an explicit expert-only mode/flag (regenerate-bulk-CSVs-from-dump), clearly labeled, NOT the default.
The `SEED_DIR` constants themselves are repointed in 2.1 (this step changes the GENESIS SOURCE logic;
2.1 changes the path constants — kept separate so each is one clean commit). Does NOT change the
exporter (1.1) or the oracle (1.4).

## Test bar

Data-core pytest: `run_rebuild` from `data/db-export/` + `data/db-export-bulk/` (no dump) produces
both DBs, and the rebuilt DBs match the DBs the dump-based rebuild produced (the curated rows + the
bulk corpus present + correct). This is the "rebuild from CSV alone" proof at the `run_rebuild` level
(1.4 then makes it a standing round-trip oracle). Emits the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE`.
FALSIFIABLE: a CSV-genesis rebuild that produces a DB missing bulk rows (or differing from the
dump-based rebuild) fails the row; an expert-mode flag that silently still requires the dump for a
NORMAL rebuild fails (the dump must NOT be a routine input). Per `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md` (this step is verifiable only AFTER 1.1/1.2 produce the bulk
CSVs it reads).

## Dependencies

- **1.1** — the bulk CSVs exist (the genesis source for the bulk half).
- **1.2** — the bulk CSVs are LFS-tracked (a clone+pull has them present for the rebuild).
- **Phase 0 (0.1)** — the round-trip-losslessness premise this rebuild rests on, proven.

## Reference

[`../plan-spec.md`](../plan-spec.md) — cross-step invariant 2 (the dump-retirement is the post-work
end-state, gated on this + 1.4 landing green) + the coverage map (the run_rebuild genesis element).

## Design authority

`data/maintainer-tool/design.md` **D38** §"`run_rebuild`'s genesis changes from `Ghidra dump +
data/seeds/*.csv → both DBs` to `data/db-export/ + data/db-export-bulk/ → both DBs`... the dump
retires to an expert-only one-off" + the revised D19 (the write mechanism unchanged; only the genesis
source moved) + the §body rebuild dataflow diagram. Build to D38's genesis change.

## Disassembler-test / author-burden

None — a rebuild-genesis repoint; no author-facing input, no game-function target, no AP18 addition.
