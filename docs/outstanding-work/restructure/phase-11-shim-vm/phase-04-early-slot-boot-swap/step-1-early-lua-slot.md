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
- The **ordering guard**: the early slot's asset-declaration calls complete BEFORE
  `CSystem::Init` opens the boot assets — ordered the way `g_kcdxReadyEvent` orders
  the seam install vs the first read (asset design §8). A boot open preceding the
  early slot is the failure to guard against.
- Lift the manifest-load error for the chosen shape so an author can declare it.

## Test bar

A `test-plugins/cap-NN-early-lua-slot/` regression: an early-slot Lua body runs and
self-reports a marker BEFORE a sentinel boot-phase log line (proving the ordering
guard holds), via the canonical acceptance signal. Runnable at this step (the VM is
up from P3). PROBE Q silent. Confirmed by the user's launch + the agent's dev-log
read.

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
