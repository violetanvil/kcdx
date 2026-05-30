# Phase 9.3 step 6 — multi-region trampoline-pool expansion

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 6.

## What

Extend `src/trampoline.cpp` (currently single-region) to multi-region on-demand
branch-pool expansion: when the current branch pool exceeds 80% used, allocate an
additional ±2GB-adjacent region (extending the existing `AllocateFromBranchPool`
machinery). Up to N regions before genuine exhaustion.

## Why

Required at TC scale, where hundreds of plugins each install dozens of hooks. The
auto-pad-and-trampoline behavior of `kcdx.statement.*` (step 4) and the callback
thunks of `kcdx.hook.*` (step 3) both draw from this pool.

## Scope

- On 80%-used threshold: allocate the next ±2GB-adjacent region.
- Genuine exhaustion (N regions tried): a strong specific error naming the pool,
  percentage used, fallback attempted, and an actionable next step (per the
  author-clear-error discipline).

## Independence

Independent of steps 3/4 in code, but their tests at TC-ish scale exercise it.
Can land any time in the phase.

## Test bar

A test that installs enough hooks to cross the 80% threshold and confirms a second
region allocates (pool grows) rather than failing. The exhaustion error path is
unit-checkable with a small synthetic cap.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "Multi-region
on-demand branch-pool expansion".
