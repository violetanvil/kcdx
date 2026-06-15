# Phase 1 — Probe the two unverified mechanisms

Settle, BEFORE any attribution layer or UI is built, the two `unverified, probe before building`
claims D45 fact 2 marks (`.claude/rules/results-driven.md` — a design clause asserting a runtime
mechanism is provisional until probed; `.claude/rules/incremental-delivery.md` — the probe is the
earlier step its consumers rest on):

1. Does the EXISTING per-kind check (`runVerdictCheck` → `extractSurvivalCheck` → `checker`) produce
   a correct verdict when run against a **divergent** (non-version-matching) DLL?
2. Can that one **row-level** verdict be **attributed to the specific diverged kind-relevant field**
   (signature vs rva for a `function` row; the survival column for the other kinds)?

The probe's finding + its divergent-DLL fixture are the durable artifact (`_research/` per
`.claude/rules/working-artifacts.md`); the fixture feeds Phase 2's worker test. The answer SHAPES
Phase 2 — if attribution needs more than the row-level verdict (likely for a `function` row, whose
one body-hash can't split signature-vs-rva by itself), the probe states exactly what the attribution
layer must do.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1.1 — Probe divergent-DLL behavior + per-field attribution](step-1-probe-divergent-dll-attribution.md) | DONE | dea42f7 |

Finding captured to `_research/maintainer-tool-fix-divergence-probe/` (FINDINGS.md + the reusable
`fixDivergenceProbe.spike.ts`). E1: `runVerdictCheck` is divergent-DLL-safe for every kind (no throw,
no spurious Unchanged). E2: the one body-hash verdict CANNOT split signature-vs-rva for a `function`
row — `signature` is never hashed, so the worker needs a `cannot-derive` per-field status for it; the
other kinds attribute their one survival column directly. Third finding: the worker must build the
check input via `savedSeedRow(row)`, not a bare `{}` prospective dict.

## Phase gate

The probe's outcome→meaning map is answered against a real (or synthesized) divergent-DLL fixture,
the finding captured to `_research/`, and the captured fixture + the attribution answer are committed
as the durable process-output. No production code changes this phase (a probe leaves no residue in
live source — `.claude/rules/working-artifacts.md`); the deliverable is the answer + the fixture
Phase 2 builds on. Gate: the probe ran and its result is recorded (not a build-green gate — this
phase ships a finding, not a feature).
