# Seeds → tracked-CSV migration

**Intent:** migrate the maintainer-tool data layer off the retired `data/seeds/` to **D38** —
both reference DBs rebuild from the tracked CSV export (curated → `data/db-export/` in git, bulk →
`data/db-export-bulk/` under Git LFS); the ~1.3 GB Ghidra dump retires to an expert-only
bulk-regeneration tool; the DB stays the authoring source-of-truth, OUT of git (D1 holds). Core
data-layer migration only — the governance/doc sweep is a deferred follow-up
([`plan-spec.md`](plan-spec.md) §Scope).

**Design authority:** `data/maintainer-tool/design.md` **D38** (+ the revised D18/D19/§body
dataflow), committed `f5023f5`. Build to D38, not this README's summary. Shared spec +
coverage map: [`plan-spec.md`](plan-spec.md).

**Load-bearing ordering (the soundness gate's carry-forward):** the dump is TODAY the sole source
of the bulk corpus, so dump-retirement is the POST-WORK end-state — P0 probes the bulk-CSV
round-trip first, P1 builds the exporter + widened oracle, and NO step treats the dump as retired
before P1 lands green (`.claude/rules/incremental-delivery.md`, `results-driven.md`).

## Phase-grain status ledger

| Step | Status | Commit |
|---|---|---|
| [Phase 0 — Probe: the bulk round-trips CSV-losslessly (the gating unknown)](phase-00-probe/README.md) | DONE | (landed) — 0.1 PASS: the DEV bulk round-trips DB→CSV→DB byte-identical (no dump), ~17.7M rows; D38's rebuild-from-CSV premise verified. Phase 1 proceeds. |
| [Phase 1 — The exporter-over-bulk + the tracked CSV genesis + the widened oracle](phase-01-exporter-genesis/README.md) | DONE | All 4 steps landed: 1.1 (6af2d80) + 1.1-fix/1.3 (40d58b6) + 1.2 (99a1aab) + 1.4 (4adf95a). The widened round-trip oracle is green over the full corpus (rebuild-from-CSV → DB → re-export → byte-identical, both halves) — the dump is retired as a routine input; the D38 export/genesis machinery is proven lossless. Phase 2 (backend + toolchain migration off data/seeds) proceeds. |
| [Phase 2 — The backend + toolchain migration off data/seeds](phase-02-backend-toolchain/README.md) | NOT STARTED | — |
| [Phase 3 — Close the deprecation (delete data/seeds - deprecated)](phase-03-close-deprecation/README.md) | NOT STARTED | — |
