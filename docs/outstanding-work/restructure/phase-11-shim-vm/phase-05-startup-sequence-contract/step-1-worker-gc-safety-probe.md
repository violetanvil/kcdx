# P5 step 1 — worker GC-safety probe (a worker subsystem-bind + store-write is PROBE-Q-silent)

## What

Open the phase with a probe that confirms the one runtime-mechanism the early-bind
surface rests on but that is NOT yet observed: binding a kcdx subsystem's author
surface on the worker VM + the relevant store write (e.g. a `RegisterRuntimeOverlay`
write) is GC-safe and PROBE-Q-silent. PROBE INITORDER already proved the subsystem
ORDERING (console/cvar movable, all subsystems up before the boot open); this probe
proves the worker BIND is hazard-free before steps 2/5 land binds on it. Probe-first
per `.claude/rules/results-driven.md` — the keystone proved build+adopt on the
worker; it did NOT prove a subset-bind + RCU-write on the worker.

## Scope

- A throwaway `// === DIAGNOSTIC (PROBE …)` instrumentation set (or a minimal
  `test-plugins/` probe plugin) that, on the worker post-VM-publish point, performs a
  representative worker subsystem-bind + a `RegisterRuntimeOverlay`-shape store write
  on the adopted VM, with an outcome→meaning map written UP FRONT.
- Observe ground truth: PROBE Q (`frealloc` kcdx-image-sentinel canary) reads ZERO
  across the worker bind + write + a forced GC + a save-load cycle; log the raw
  result, tid, and the bind/write outcome.
- Theory-independent: an outcome that FALSIFIES "the worker bind is GC-safe" (PROBE Q
  fires → a kcdx-image sentinel was embedded → the bind is NOT safe on the worker →
  surface, do not proceed to the binds).
- Capture the finding + reusable wiring to `_research/probe-archive/`, then REMOVE the
  probe from source (no residue — `.claude/rules/working-artifacts.md`). The probe is
  not committed in source; the archived finding is.

## Test bar

The probe is the test: PROBE Q reads zero across the worker bind + store write + GC +
save-load (the falsifiable GC-safety claim). Agent writes/builds/deploys the probe,
the user launches once, the agent reads `kcdx-dev.log` against the pre-committed
outcome map. NO permanent `cap-NN` row (this is a probe, not a feature) — the
permanent GC-safety coverage is PROBE Q itself, already in the suite.

## Dependencies

P3 (the adopted VM must be up + published for a worker bind to run on it) — DONE.
P4 foundation (the `RegisterRuntimeOverlay` two-writer CAS, so a worker writer is the
sanctioned shape this probe exercises) — the CAS is the Phase-4 foundation; if a
worker write predates the CAS, the probe uses the pre-CAS single-writer path and step
2's CAS upgrade is what makes the steady-state worker write safe.

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §8 claim 2 (the worker GC-safety
probe — the one provisional mechanism the binds rest on) + §3 (PROBE INITORDER, the
ordering already proven, cited so this probe is scoped to GC-safety only, not
ordering). Build the probe to the design's §8.2 claim, not this doc's summary.

## RE / author-burden note

No author hex. The probe reads PROBE Q's existing canary output + the bind/write
outcome. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" coverage row "Worker GC-safety of each
early subsystem bind"; design §8.2.
