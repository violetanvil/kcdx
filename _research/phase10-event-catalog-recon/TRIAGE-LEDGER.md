# Phase 10 event-catalog RE triage — fan-out ledger

The triage plan (plan-persistence: multi-item). Each front is a read-only Ghidra
disassembly subagent that returns CLAIMS + evidence for its events; the synthesizer
re-grounds load-bearing claims and fills the verdict table in
[`FINDINGS.md`](FINDINGS.md). Delete this ledger once all fronts are synthesized +
the FINDINGS verdict table is complete.

**TRIAGE COMPLETE (2026-06-13).** All three fronts reported, synthesized into the
[`FINDINGS.md`](FINDINGS.md) final verdict table. Kept in-tree as the audit trail of
the fan-out (how the triage was run); not a permanent lifecycle artifact.

## Fronts

| # | Front | Events | Shared anchor | Status | Output |
|---|---|---|---|---|---|
| 0 | (settled at tier 1) | save_created | seed 144 SaveGame | DONE — STATIC-FINDABLE (in DB) | FINDINGS row 1 |
| 1 | Combat-state | damage_taken, damage_dealt, combat_started, combat_ended | IsInCombat getter (seed 5/6/7/8) | DONE — all 4 NEEDS-LIVE-CORRELATION (only a state getter; transition dispatcher unpinned) | front-1-combat.md |
| 2 | Entity/world | item_picked_up, location_entered, npc_interacted_with | entity-script callback family | DONE — all 3 LUA-EVENT (entity-script `OnPickup`/`OnEnterArea`/`OnUse`) | front-2-entity.md |
| 3 | Lua script-event | perk_unlocked, level_up, quest_stage_advanced, dialogue_line_spoken | game→Lua event bus (HYPOTHESIS) | DONE — hypothesis FALSIFIED; all 4 NEEDS-LIVE-CORRELATION (C++ fire-sites, no Lua event bus) | front-3-lua-events.md |

## Synthesis gate

Per skill §4.5: any claim that will become design authority or a seed row is GATED
by an independent body-read verifier before it ships. The synthesizer re-grounds
each cross-front call-edge against the owning body; a claim it cannot ground returns
to "unverified," never ships on a front's word.

## Dispatch discipline (model-tiering)
RE fronts author durable `_research/` artifacts + their facts become seed rows →
they are NOT volume-absorbing digest dispatches → they inherit the session model
(no downgrade). General-purpose subagent each.
