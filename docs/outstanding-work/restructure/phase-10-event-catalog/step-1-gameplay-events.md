# Phase 10 step 1 — RE + hook the first gameplay events

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

RE and hook the first 10–15 gameplay events, each exposed via `kcdx.on(...)`.
Candidate list (gap #15): `damage_taken`, `damage_dealt`, `save_created`,
`dialogue_line_spoken`, `item_picked_up`, `location_entered`, `combat_started`,
`combat_ended`, `perk_unlocked`, `level_up`, `quest_stage_advanced`,
`npc_interacted_with`.

## Scope

- For each event: RE the underlying CryEngine call path; land the Address Library
  entry by name (the name carries the verified ABI); install ONE kcdx-owned hook
  that fans out to all `kcdx.on(<event>, ...)` subscribers — plugins do NOT each
  re-hook the function.
- Each event site is hash-tracked the same as a direct hook (reference-DB
  function-name reference; `on_changed` posture on version drift).
- This step may itself decompose into per-event sub-cycles if the RE load is heavy
  — each event is independently shippable. The phase ledger gains a row per event
  if so (decompose-when-picked-up applies within the step).

## Disassembler test

The author writes `kcdx.on("damage_taken", fn)` — a named event, no hex. The
engine carries the hook + the ABI. Clean.

## Test bar

Step 2.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 10".
