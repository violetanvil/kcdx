# P4 step 1 — the early Lua slot + ordering guard vs the boot open

## What

Build the early Lua slot — the pre-boot-open execution window that runs a plugin's
early Lua on the DllMain VM, in the Phase-1-decided shape. This is the single
primitive three consumers share (before_game Lua hooks, the boot-asset serve, the
serve-execute path); this step ships the slot + the ordering guard that keeps it
before the engine's boot asset open.

## Scope

- Implement the slot in the P1-decided shape:
  - **Candidate A (lean)** — run a `before_game`-zoned plugin's existing `plugin.lua`
    in the early window; `after_game` plugins keep the late slot.
  - **Candidate B** — a new `lua_before` entrypoint, `plugin.lua` untouched.
  - P1's §4.4 outcome picks which; build that one.
- The **ordering guard — a MANDATORY happens-before EVENT GATE** (design §5, hard
  invariant; verified by PROBE P11 v2 that the slot runs on the worker thread and the
  boot open on the game-main thread — two threads, a cross-thread dependency, and
  currently UNGATED). Build it as: the early slot (worker), after its
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
