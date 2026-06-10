# Phase 4 — the cross-thread foundation (event gate + RegisterRuntimeOverlay CAS)

**Re-scoped 2026-06-05 to FOUNDATION-ONLY.** P4 builds ONLY the race-safety
infrastructure the Phase-5 early surface reuses verbatim — it does NOT build the
early Lua slot or the boot-asset serve (those moved to Phase 5, steps 5/7). The two
deliverables:

- **The cross-thread happens-before EVENT GATE** — the worker signals a readiness
  event (release); the boot-open path (`asset_overlay.cpp` HOOK 1 + HOOK 2, game-main
  thread) waits on it and BLOCKS until signaled (acquire) before resolving a boot
  overlay. A producer→consumer CROSS-THREAD DEPENDENCY made deterministic — NOT a race
  (the ~640ms–1.5s margin is the race-if-ungated; the gate is the guarantee,
  `.claude/rules/concurrency.md`). Timing-based ordering ("run the slot earlier so it
  finishes first") is the FORBIDDEN race this gate exists to kill, never a sleep/retry
  workaround. The exact event + its bounded-timeout fallback (lean: 5000ms +
  vanilla-serve + `LOG_WARN_KV` — surfaced, not yet user-decided) are settled at this
  phase's build under architect-review.
- **The `RegisterRuntimeOverlay` two-writer CAS** — `src/asset_namespace.{h,cpp}`'s RCU
  store is single-writer-main-thread today; the §"WRITER SERIALIZATION" header names the
  fix (a CAS retry loop) for a worker writer + the game-main writer.

The owed **worker-GC-safety probe** (a worker subsystem-bind + a
`RegisterRuntimeOverlay`-shape store write on the worker VM is GC-safe + PROBE-Q-silent)
gates this build — it is tracked as P5 step 1, runnable independently as the probe. See
[`../RESUME-STATE.md`](../RESUME-STATE.md).

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

The two step docs below still carry the OLD full early-slot + boot-swap scope (with
RE-SCOPED / BLOCKED banners) — their FULL slot/serve content is the Phase-5 input
(P5 steps 5/7), NOT this phase's build. The foundation this phase actually builds is
the gate + CAS + cap-82 regression above; the step docs are re-authored to that scope
when P4 builds.

| Step | Status | Commit |
|---|---|---|
| [1 — the cross-thread event gate + RegisterRuntimeOverlay CAS](step-1-early-lua-slot.md) | NOT STARTED | — |
| [2 — the cap-82 order-inversion regression](step-2-boot-asset-swap.md) | NOT STARTED | — |

## Phase verification gate

- **Build green.** The gate + the CAS compile and link; the foundation ships no
  user-facing surface of its own (the early slot + boot serve that the user perceives
  are Phase 5, steps 5/7).
- The boot-open path WAITS on (and BLOCKS until) the worker's signaled readiness event
  before resolving a boot overlay — the happens-before EVENT GATE holds. An ungated /
  un-signaled boot open is the failure, NOT a wall-clock margin; timing-based ordering
  is the forbidden race the gate exists to kill (`.claude/rules/concurrency.md`).
- **The cap-82 order-inversion regression FAILS if the boot open resolved the store
  BEFORE the slot signaled** (and it was not the bounded-timeout degraded path) — the
  falsifiable proof the gate holds, not a wall-clock check.
- The `RegisterRuntimeOverlay` two-writer CAS is exercised (a worker writer + the
  game-main writer do not lose an update) — its concurrency invariant documented +
  reviewed (`.claude/rules/concurrency.md`).
