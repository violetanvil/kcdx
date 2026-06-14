# 1.1 [FE/probe] Probe divergent-DLL behavior + per-field attribution (E1, E2)

## What

Settle the two `unverified, probe before building` claims D45 fact 2 marks, BEFORE any attribution
layer or render is built (`.claude/rules/results-driven.md` + `.claude/rules/incremental-delivery.md`
— the probe is the earlier step its consumers rest on). Two questions, one fixture:

1. **(E1) Divergent-DLL behavior.** Run the EXISTING per-kind check (`runVerdictCheck` →
   `extractSurvivalCheck` → `checker`, in `frontend/src/editor/verdictCheck.ts`) against a
   **divergent** (non-version-matching) DLL — the build a `failed` report row diverged on. The
   existing check is documented + tested against a VERSION-MATCHING DLL (s04 §"The per-author static
   check"); the question is whether each kind's check produces a correct, non-throwing verdict when
   the DLL is the divergent build (the recorded `content_hash` / aob / anchor no longer matches).
2. **(E2) Per-field attribution.** `runVerdictCheck` returns ONE row-level `CheckResult` per row (a
   `function` row runs one body-hash compare keyed off the recorded `content_hash`). Can that one
   verdict be ATTRIBUTED to the specific diverged kind-relevant field — signature vs rva for a
   `function` row; the survival column for the other kinds — or does attribution need additional
   per-field derivation the worker must add?

The probe writes its outcome→meaning map FIRST, runs against a real-or-synthesized divergent-DLL
fixture, and the answer SHAPES Phase 2's `fixDivergence` worker (it states exactly what attribution
each kind needs). A probe leaves no residue in live source — the finding + the fixture are captured
to `_research/`, no production code changes this step.

## Scope

In the SEPARATE frontend repo (`data/maintainer-tool/frontend/`), probe-only — NO production-source
change (`.claude/rules/working-artifacts.md` — a scratch probe leaves no residue in live source):
- A throwaway probe test/script (e.g. a vitest spike under `frontend/src/editor/__probes__/` or a
  one-off `.test.ts` that is captured-then-removed) that constructs a divergent-DLL fixture (a
  version-matching DLL's body MUTATED at a recorded target, OR a real divergent build's bytes if
  available) and runs `runVerdictCheck` over each kind, logging the raw verdict + whether the diverged
  field is attributable from the result.
- **Captured as durable process-output** to `_research/maintainer-tool-fix-divergence-probe/` — the
  outcome→meaning map, the per-kind result, the attribution answer, AND the reusable fixture (the
  synthesized divergent-DLL bytes + the script that built them) so Phase 2's worker test reconstructs
  it. The probe is then removed from `frontend/src/` (zero residue).

Does NOT build the `fixDivergence` worker (2.1 — gated on this answer); does NOT touch the s04 UI.

## Test bar

The probe IS the verification (this step ships a finding, not a feature). Outcome→meaning map written
BEFORE running, each outcome pre-committed flat (`.claude/rules/results-driven.md` §theory-independent):
- **E1:** for each kind, run `runVerdictCheck` over the divergent fixture. Outcome A (returns a
  defined `Changed`/`CannotCheck` verdict, no throw) → the existing check is divergent-DLL-safe for
  that kind. Outcome B (throws / returns a spurious `Unchanged`) → the worker must guard/extend that
  kind before the render can trust it. Outcome C (returns `CannotCheck` for a reason the divergent
  build introduces) → record the reason; the no-divergence/cannot-check state (E10) must surface it.
- **E2:** for a `function` divergent fixture, is the row-level verdict alone enough to say "signature
  diverged" vs "rva diverged"? Outcome A (the verdict + the existing extracted inputs already
  distinguish the field) → attribution is a thin mapping. Outcome B (one body-hash verdict cannot
  split the two) → the worker needs a per-field derivation (e.g. re-resolve the rva separately, hash
  the body separately); the probe records exactly which per-field check each kind needs.

**FALSIFIABLE:** the probe FAILS to settle the step if it runs only against a version-matching DLL
(it must use a DIVERGENT fixture — the whole point), or if it reports an attribution answer it did
not actually observe (a guessed "yes attributable" with no per-kind result is theory-seeking, not a
probe). The captured finding names the per-kind attribution requirement for EACH kind, not just
`function`.

Gate: the probe ran against a divergent fixture, the per-kind outcome + the attribution requirement
are recorded in `_research/`, the fixture is captured + reusable, and the probe is removed from
`frontend/src/` (no two live probes — `guard-probe-stack.py`).

## Dependencies

- The existing `runVerdictCheck` / `extractSurvivalCheck` / `checker` machinery (the verification
  engine, Phase 2 of `maintainer-tool-verification-engine`) — the seam under probe. Present.
- The existing PE-parse path (`frontend/src/dll-resolver/peSections.ts`) — to parse the fixture DLL.
- No prior step in THIS plan (1.1 is the first step).

## Reference

[`../plan-spec.md`](../plan-spec.md) — E1, E2; the settled facts (fact 2 — the two `unverified`
clauses); the cross-step invariant "probe-first ordering is mandatory".

## Design authority

`data/maintainer-tool/design.md` D45 fact (2) — the `assumes correct-against-divergent-DLL —
unverified, probe before building` + the per-field-attribution `unverified` clause. The probe's job
is to RESOLVE those two clauses; build the Phase-2 worker to whatever the probe finds, not to a
guess.

## Disassembler-test / author-burden

None — a maintainer-tool FE probe; no author-facing plugin input, no game-function target. (The
"DLL" here is the maintainer's linked game build, read by the tool — the maintainer picks the file,
the tool does the byte work.)
