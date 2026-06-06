# P4 step 1 — the early Lua slot + ordering guard vs the boot open

> **RE-SCOPED 2026-06-05 — read [`../RESUME-STATE.md`](../RESUME-STATE.md) first.**
> This step now builds the FOUNDATION ONLY (the cross-thread event gate + the
> `RegisterRuntimeOverlay` two-writer CAS + the cap-82 order-inversion
> regression). The worker slot-RUNNER shape + the early-bind surface set below
> are DEFERRED to the new Phase 5 (`bring-forward-early-capability`), which
> `/design` settles (it generalizes the runner to Lua+C++ per the approved
> early-C++/Lua-capability decision). A worker-GC-safety probe (subset-bind +
> RCU-write on the worker VM, PROBE Q silent) is owed before the foundation
> build. The "Scope"/"Test bar" below describe the FULL early-slot and are the
> Phase-5 input, NOT this step's current build. Do not build the slot-runner
> here.

## What

Build the early Lua slot — the pre-boot-open execution window that runs a plugin's
early Lua on the DllMain VM, in the Phase-1-decided shape. This is the single
primitive three consumers share (before_game Lua hooks, the boot-asset serve, the
serve-execute path); this step ships the slot + the ordering guard that keeps it
before the engine's boot asset open.

## Scope

- Implement the slot in the SETTLED shape (PROBE P4, 2026-06-05 — candidate B):
  a **new WORKER-RUN early entrypoint** (e.g. `lua_before`), run on the kcdx worker
  thread right after the VM is published (before the engine's game-main boot open);
  `plugin.lua` (RunAll) keeps its existing game-main first-tick timing. Candidate A
  (reuse `plugin.lua` early) is RULED OUT: PROBE P4 showed `plugin.lua`/RunAll runs
  game-main ~10 s after the boot open and cannot move to the worker without moving all
  of RunAll (kept game-thread for `kcdx.*` registration). Design §5/§4.4 + the
  archive `_research/probe-archive/p4-early-slot-thread-topology.md`.
- The **ordering guard — a MANDATORY happens-before EVENT GATE** (design §5, hard
  invariant; PROBE P4-CONFIRMED cross-thread, 2026-06-05: the worker VM-publish point
  precedes the first boot open by ~1.5 s and on a DIFFERENT thread, with no viable
  game-main slot point before the boot open — genuinely cross-thread, the gate is the
  proven mechanism, not same-thread program-order). Build it as: the early slot
  (worker), after its
  asset-declaration calls, **signals a new readiness event**; the boot-open path
  (`asset_overlay.cpp` HOOK 1 + HOOK 2, game-main thread) **waits on that event and
  BLOCKS until signaled** before resolving a boot-asset overlay. No existing edge
  covers this (`g_kcdxReadyEvent` gates the ctor-bracket, not the boot open;
  `g_whgameLoadedEvent` gates the worker's WHGame-mapped wait) — P3/P4 ADDS the edge.
  **Timing-based ordering ("run the slot earlier so it finishes first") is FORBIDDEN
  — it is the cross-thread race the gate exists to kill** (`.claude/rules/concurrency.md`,
  `.claude/rules/polling.md`). An ungated boot open is a DEFECT fixed at source, never
  a sleep/retry workaround. Confirm the exact event + its bounded-timeout fallback
  under this step's architect-review.
- Lift the manifest-load error for the chosen shape so an author can declare it.

## Test bar

A `test-plugins/cap-NN-early-lua-slot/` regression that proves the GATE, not a timing
margin: the boot-open path observed the early-slot readiness event as SIGNALED before
it resolved the overlay (the happens-before edge held). **An order-inversion row that
FAILS if the boot open resolved the store BEFORE the slot signaled** — the falsifiable
proof the gate holds (design §5). NOT "marker logged before boot-phase line" (that is
the forbidden wall-clock check). Runnable at this step (the VM is up from P3). PROBE Q
silent. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

P3 step 3 (the VM must be up + adopted for the early slot to run on it), P1 step 1
(the slot shape verdict this step builds).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §5 (the slot candidates + the decision
criterion + the ordering guard) + §4.4 (the probe that settles the shape) +
[`../../before-game-hooks.md`](../../../before-game-hooks.md) §6b (the early-slot
deliverable). Build to the P1-recorded shape, not this doc's summary.

## UX

The author-facing surface is the chosen entrypoint shape. Candidate A reuses the
`zone` knob the author already sets (no new surface to learn); candidate B adds one
`lua_before` entrypoint. Either way the author DECLARES intent (an early-running
script) and the engine runs it at the right time — the disassembler-test posture
(`.claude/rules/cornerstones.md`): the author never wires the timing by hand. An
error path (a slot that fails to run early) logs a teaching message, never a silent
non-run.

## RE / author-burden note

No author hex. The slot's timing is engine-owned (the ordering guard); the author
declares a zone/entrypoint name. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage row E11; design §5.
