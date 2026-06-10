# P4 step 1 — the cross-thread event gate + RegisterRuntimeOverlay CAS

> **RE-SCOPED 2026-06-05 — read [`../RESUME-STATE.md`](../RESUME-STATE.md) first.**
> This step is FOUNDATION-ONLY: the cross-thread happens-before event gate + the
> `RegisterRuntimeOverlay` two-writer CAS. The early Lua slot itself (the worker-run
> entrypoint, its author-facing shape, the manifest knob) moved to **Phase 5 step 5**
> (`lua_before` + the worker before-game runner); the boot-asset serve that consumes
> it moved to **Phase 5 step 7**. This step builds ONLY the race-safety machinery
> those two reuse — no slot-runner here.

## What

Build the two pieces of cross-thread race-safety the Phase-5 early surface rests on:
the happens-before EVENT GATE that orders the worker's readiness against the game-main
boot open, and the `RegisterRuntimeOverlay` two-writer CAS that lets a worker writer
and the game-main writer share the RCU store without a lost update. Both are pure
infrastructure — no user-facing surface ships here.

## Scope

- **The ordering guard — a MANDATORY happens-before EVENT GATE** (design §5, hard
  invariant; PROBE P4-CONFIRMED cross-thread, 2026-06-05: the worker VM-publish point
  precedes the first boot open by ~640ms–1.5s and on a DIFFERENT thread, with no viable
  game-main slot point before the boot open — genuinely cross-thread, the gate is the
  proven mechanism, not same-thread program-order). Build it as: the worker, after its
  asset-declaration calls, **signals a new readiness event** (release); the boot-open
  path (`asset_overlay.cpp` HOOK 1 + HOOK 2, game-main thread) **waits on that event and
  BLOCKS until signaled** (acquire) before resolving a boot-asset overlay. No existing
  edge covers this (`g_kcdxReadyEvent` gates the ctor-bracket, not the boot open;
  `g_whgameLoadedEvent` gates the worker's WHGame-mapped wait) — P4 ADDS the edge.
  **Timing-based ordering ("run the slot earlier so it finishes first") is FORBIDDEN —
  it is the cross-thread race the gate exists to kill** (`.claude/rules/concurrency.md`,
  `.claude/rules/polling.md`). An ungated boot open is a DEFECT fixed at source, never a
  sleep/retry workaround. Confirm the exact event + its bounded-timeout fallback (lean:
  5000ms + vanilla-serve + `LOG_WARN_KV` — surfaced, not yet user-decided) under this
  step's architect-review.
- **The `RegisterRuntimeOverlay` two-writer CAS** — `src/asset_namespace.{h,cpp}`'s RCU
  store is single-writer-main-thread today; its §"WRITER SERIALIZATION" header names the
  fix. Build the CAS retry loop so a worker writer (the early slot, in P5) and the
  game-main writer publish without losing an update. Documented lock/ordering invariant
  per `.claude/rules/concurrency.md`.

## Test bar

The cap-82 order-inversion regression is step 2; this step's own gate is the event +
CAS compiling and the gate's happens-before edge being observable. **The CAS path is
exercised** (a worker write + a game-main write do not lose an update — a race-shaped
test or a documented + reviewed invariant where no permutation tool exists,
`.claude/rules/concurrency.md`). Runnable at this step (the VM is up from P3). PROBE Q
silent.

## Dependencies

P3 (the VM must be up + adopted for a worker write to exist). The early slot that
SIGNALS the gate is P5 step 5 — this step builds the gate + CAS the slot will use, not
the slot.

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §5 (the ordering guard + the gate
mechanism) + §4.4 (the probe that confirmed the cross-thread topology) +
[`../RESUME-STATE.md`](../RESUME-STATE.md) (the foundation-only re-scope). Build to the
recorded gate/CAS design, not this doc's summary.

## RE / author-burden note

No author-facing surface, no author hex, no new DB rows — pure cross-thread
infrastructure.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage row E11; design §5. The early-slot author
surface this foundation serves is Phase 5 step 5.
