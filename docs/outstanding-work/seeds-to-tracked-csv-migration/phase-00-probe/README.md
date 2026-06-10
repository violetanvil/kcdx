# Phase 0 — Probe: the bulk round-trips CSV-losslessly (the gating unknown)

**Intent:** before building the D38 migration on it, PROVE the load-bearing premise the soundness
gate flagged: that the DEV bulk corpus (`statements`/`referenced_vars`/`call_edges` + the
`kcdx_id`-NULL discovery rows) can be exported to CSV and rebuilt back into a byte-identical DEV DB
with NO Ghidra dump. The dump is TODAY the sole source of the bulk (verified from source:
`build_rows` reads it via `iter_table(dump_dir)`), so if the dump contributes data the DB's tables
don't capture, "rebuild from CSV alone" is unbuildable as designed — and that must surface NOW, not
after the migration is half-built (`.claude/rules/results-driven.md` — a checkable unknown is
probed before it becomes the basis for the work).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [0.1 [PROBE] Bulk DEV DB → CSV → rebuilt DEV DB round-trips byte-identical (no dump)](step-1-probe-bulk-roundtrip.md) | DONE | (landed) — PASS: ~17.7M rows round-trip DB→CSV→DB byte-identical (statements 5.24M, referenced_vars 10.88M, call_edges 1.29M, address_versions 321k incl. 320,987 bulk); the export is lossless from the DB alone, no dump. Finding: `_research/seeds-migration-probe/FINDINGS.md`. D38 buildable; Phase 1 proceeds. |

## Phase verification gate

Phase 0 is done when the probe (0.1) has RUN and its outcome is recorded against the outcome→meaning
map: **byte-identical** → the D38 rebuild-from-CSV model is buildable, Phase 1 proceeds;
**divergence** → the dump provides data the DEV DB tables do not capture (so the export cannot be
lossless from the DB alone) → STOP and surface to the user — D38's "rebuild from CSV alone, dump
retired" needs the export to capture more, or the dump cannot fully retire (a design reconciliation,
not a build-around). The probe is a throwaway verification script (`.claude/rules/working-artifacts.md`
— captured finding, no residue in production code); it ships no production change, so its "test" is
the recorded probe outcome itself.
