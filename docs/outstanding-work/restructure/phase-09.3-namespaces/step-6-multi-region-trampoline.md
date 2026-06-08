# Phase 9.3 step 6 — multi-region trampoline-pool expansion

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 6.

## What

Extend `src/trampoline.cpp` (currently single-region) to multi-region on-demand
branch-pool expansion: when the current branch pool exceeds 80% used, allocate an
additional ±2GB-adjacent region (extending the existing `AllocateFromBranchPool`
machinery). Up to N regions before genuine exhaustion. A capacity change to an
existing unit (touches-existing-code) — additive, no surface replaced.

## Why

Required at TC scale, where hundreds of plugins each install dozens of hooks. The
auto-pad-and-trampoline behavior of `kcdx.statement.*` (step 5) and the callback
thunks of `kcdx.hook.*` (step 4) both draw from this pool. (The single-target tests
of steps 4/5 do NOT cross the threshold — this step is the TC-scale capacity
guarantee, independent of those steps' correctness.)

## Scope (`src/trampoline.cpp`, extended)

- On the 80%-used threshold: allocate the next ±2GB-adjacent region (extend
  `AllocateFromBranchPool`, do not rewrite it).
- Genuine exhaustion (N regions tried): a strong specific error naming the pool, the
  percentage used, the fallback attempted, and an actionable next step (the
  author-clear-error discipline, `.claude/rules/cornerstones.md` / AP14 — fail loud,
  never a silent no-install).
- Impact analysis before editing: grep every caller of `AllocateFromBranchPool` +
  the pool's region-tracking state; the change is atomic across them.

## Test bar (runs AT this step)

A `test-plugins/cap-NN-trampoline-multiregion/` (suite-gated): install enough hooks
to cross the 80% threshold; a SECOND region allocates (the pool GROWS) rather than
failing — a row that FAILS if the install errors at the single-region boundary
instead of expanding. The exhaustion-error path is unit-checkable with a small
synthetic cap (a row that asserts the teaching error fires + names the pool, not a
bare failure). PROBE Q silent.

## Dependencies

The existing single-region `trampoline.cpp` (live today). Independent of steps 1-5 in
code (it extends the pool they draw from, but its own correctness — does a second
region allocate at 80%? — is checkable standalone).

## Design authority

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "Multi-region
on-demand branch-pool expansion". Build to that §, not this summary.

## Disassembler-test / author-burden note

Author-invisible — a capacity/performance change to the engine's trampoline pool. No
author-facing surface, no hex, no DB rows.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "Multi-region
on-demand branch-pool expansion".
