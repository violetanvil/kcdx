# Phase 9.3 step 6 — multi-region trampoline-pool expansion

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 6.

## What

**Scope corrected against the as-built code (2026-06-09, user-settled).** The
design § and the original step framing asserted `src/trampoline.cpp` is
"currently single-region." That premise is stale: `Allocate()` ALREADY grows
multi-region — `g_reservations` is a `std::vector`, and when a reservation fills
(`BumpAlloc` returns null) it allocates a fresh ±2GB-adjacent reservation and
pushes it. Reactive multi-region growth exists today. The real buildable delta
the design's GOAL (a TC-scale capacity guarantee + fail-loud exhaustion) names
is therefore narrower than "make it multi-region":

1. **Proactive 80%-used expansion** — pre-allocate the next ±2GB-adjacent region
   for an anchor group BEFORE its current reservation is completely full (today
   it waits until full), so a large allocation never fails for want of
   contiguous room when capacity was available. The 80% basis is
   **per-reservation** (the specific anchor-group reservation being served), NOT
   aggregate — branch reservations are not fungible across anchors (a
   WHGame-anchored block can't serve a far target; the existing
   `BranchReservationReachesFrom` guard), so capacity is per-anchor-group.
2. **N-region exhaustion CAP** — today there is no cap; the pool grows until
   `ReserveNearby` finds no nearby free region (a real OOM). Add a bounded N per
   anchor group before declaring genuine exhaustion.
3. **The strong teaching error on genuine exhaustion** — names the pool, the
   percentage used, the regions tried, the fallback attempted, and an actionable
   next step (today exhaustion returns null with a generic `ReserveNearby`
   error).

A capacity change to an existing unit (touches-existing-code) — additive, fully
contained in `Allocate()` + the reservation-tracking; no caller signature
changes (every branch-pool caller funnels through `AllocateBranch` →
`Allocate`), no surface replaced.

## Why

Required at TC scale, where hundreds of plugins each install dozens of hooks. The
auto-pad-and-trampoline behavior of `kcdx.statement.*` (step 5) and the callback
thunks of `kcdx.hook.*` (step 4) both draw from this pool. (The single-target tests
of steps 4/5 do NOT cross the threshold — this step is the TC-scale capacity
guarantee, independent of those steps' correctness.)

## Scope (`src/trampoline.cpp`, extended — inside `Allocate()`)

- **Proactive per-reservation 80% expansion:** after a `BumpAlloc` succeeds from a
  branch reservation, if that reservation now exceeds 80% used, eagerly make + push
  the next ±2GB-adjacent reservation for the SAME anchor (WHGame-anchored for
  `nearVa==0`, else `nearVa`) so the next request for that anchor has headroom.
  Extends `Allocate()` + `MakeBranchReservation`, does not rewrite them.
- **N-region cap per anchor group:** count the in-range branch reservations for the
  request's anchor; once N have been tried and none can serve, declare genuine
  exhaustion (a named constant `kMaxBranchRegionsPerAnchor`).
- **Genuine exhaustion → a strong specific error** naming the pool (branch), the
  percentage used on the fullest in-range reservation, the regions tried (N), the
  fallback attempted (a fresh `ReserveNearby`), and an actionable next step (the
  author-clear-error discipline, `.claude/rules/cornerstones.md` / AP14 — fail loud,
  never a silent no-install).
- Impact analysis (done): every branch-pool caller funnels through
  `kcdx::trampoline::AllocateBranch` → `Allocate(branchPool=true)`
  (`dynamic_call_jit.cpp`, `lua_bind_code.cpp`, `lua_bind_dynamic_call.cpp`,
  `runtime_func_t.cpp`, `trampoline_engine.cpp`, the two interface thunks) — none
  touches the reservation-growth logic, so the change is atomic in `trampoline.cpp`
  with no caller signature changes.

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
