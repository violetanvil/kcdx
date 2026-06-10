# Phase 1 — The exporter-over-bulk + the tracked CSV genesis + the widened oracle

**Intent:** build the D38 export/genesis machinery — extend the exporter to capture the bulk half
losslessly to `data/db-export-bulk/` (under Git LFS), repoint `run_rebuild` to rebuild both DBs from
the tracked CSVs (the dump → expert-only), and widen the round-trip oracle over the bulk so the
completeness bar (rebuild-from-CSV → DB → re-export → byte-identical, both halves) is a standing
green gate. This is the core of the migration; Phase 2 then moves the backend + toolchain seed-path
constants onto it. Gated by the data-core pytest (`data/refdata-extractor/tests/`).

**Design authority:** `data/maintainer-tool/design.md` **D38** + the revised §body dataflow. Build
to D38's curated/bulk seam (`kcdx_id IS NOT NULL`) + the LFS-for-bulk + the round-trip bar.

**Precondition:** Phase 0 (the probe) PASSED — the bulk round-trips CSV-losslessly. If P0 surfaced a
divergence, this phase does not start until the user reconciles the D38 model.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1.1 [CORE] Extend the exporter to capture the bulk half losslessly → data/db-export-bulk/](step-1-exporter-bulk.md) | NOT STARTED | — |
| [1.2 [CORE] Git LFS tracking for data/db-export-bulk/ (.gitattributes + LFS init)](step-2-lfs-tracking.md) | NOT STARTED | — |
| [1.3 [CORE] Repoint run_rebuild genesis to the tracked CSVs; the dump → expert-only](step-3-rebuild-genesis.md) | NOT STARTED | — |
| [1.4 [CORE] Widen the round-trip oracle over the bulk (the completeness bar)](step-4-roundtrip-oracle.md) | NOT STARTED | — |

## Phase verification gate

Phase 1 is done when: the exporter writes complete, lossless bulk CSVs to `data/db-export-bulk/`
(1.1); those CSVs are Git-LFS-tracked (1.2); `run_rebuild` rebuilds BOTH DBs from
`data/db-export/` + `data/db-export-bulk/` with no dump (1.3); and the widened round-trip oracle is
green over the full corpus — rebuild-from-CSV → DB → re-export → byte-identical for both the curated
and bulk halves (1.4). All gated by the data-core pytest. Build-green is necessary, not sufficient —
the round-trip oracle (1.4) is the real completeness proof (`.claude/rules/skeptical-expert.md`).
After 1.4 is green, the dump is retired as a routine input (the soundness carry-forward's gate is
satisfied).
