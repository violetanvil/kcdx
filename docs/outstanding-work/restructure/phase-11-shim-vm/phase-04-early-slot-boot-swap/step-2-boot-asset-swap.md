# P4 step 2 — the cap-82 order-inversion regression

> **RE-SCOPED 2026-06-05 — read [`../RESUME-STATE.md`](../RESUME-STATE.md) first.**
> The boot-asset serve (a `kcdx.assets.replace` winning a boot open) + the AP14 warn
> decision moved to **Phase 5 step 7** (it consumes the early slot, also Phase 5). This
> step is the falsifiable PROOF the step-1 event gate holds — the cap-82 order-inversion
> regression — built on the gate + CAS alone, no slot-runner required.

## What

Ship the permanent regression that proves the step-1 happens-before EVENT GATE is a
real ordering guarantee and not a wall-clock coincidence: cap-82 FAILS if the boot
open resolved the runtime store BEFORE the worker signaled its readiness event (and it
was not the bounded-timeout degraded path). This pins the gate against the cross-thread
race for every future change.

## Scope

- A `test-plugins/cap-82-order-inversion/` regression (or the engine self-test form, if
  no plugin surface is needed to drive the gate) that observes the gate's edge directly:
  the boot-open path read the worker's readiness event as SIGNALED before it resolved
  the overlay store. The assertion is on the HAPPENS-BEFORE edge, never on "marker
  logged before boot-phase line" (the forbidden wall-clock check, `.claude/rules/concurrency.md`).
- **The falsifiable claim:** the row FAILS if the boot open resolved the store BEFORE
  the slot signaled — i.e. the gate was bypassed (and it was not the bounded-timeout
  degraded path, which is a distinct, separately-asserted fallback). A row that can only
  pass is not a gate (`.claude/rules/anti-patterns.md` AP15); cap-82 has a real red
  outcome.
- Assert the bounded-timeout degraded path is distinguishable (a worker that never
  signals → boot-open proceeds vanilla + `LOG_WARN_KV`, NOT a hang) — so cap-82 reads
  GATE-HELD vs DEGRADED vs INVERTED, three distinct outcomes.

## Test bar

cap-82 as described above — the order-inversion proof. Self-reports via the canonical
signal. Runnable at this phase (the gate + CAS are step 1; the VM is up from P3). PROBE Q
silent. Confirmed by the user's launch + the agent's dev-log read. The boot asset the
USER perceives rendering replaced is the Phase-5-step-7 acceptance, not this step.

## Dependencies

P4 step 1 (the event gate + CAS this regression proves). No early-slot runner needed —
cap-82 asserts the foundation, which the Phase-5 slot later consumes.

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §5 (the gate + the order-inversion proof) +
[`../RESUME-STATE.md`](../RESUME-STATE.md) (the foundation-only re-scope; cap-82 named as
the P4 regression). The boot-asset serve + AP14 warn this doc previously described are
Phase 5 step 7 — build them there, to the asset design.

## RE / author-burden note

No author-facing surface, no author hex, no new DB rows — a regression over the
cross-thread foundation.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage row E12 (the gate proof; the boot-serve
rows E13 belong to Phase 5 step 7); design §5.
