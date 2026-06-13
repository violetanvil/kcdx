# Phase 10 — one-pilot-per-mechanism ledger

User decision (2026-06-13): prove ONE representative event per build mechanism
end-to-end before scaling to all 12. Each pilot becomes the template for its
siblings. Sequencing: static read on perk now + both live probes built in parallel
→ ONE launch covers both live pilots.

Pilots map to the three mechanisms the [`FINDINGS.md`](FINDINGS.md) triage surfaced:

| Mechanism | Pilot | Siblings it templates | Next action | Status |
|---|---|---|---|---|
| LUA-EVENT (entity-script → owned VM) | item_picked_up (`OnPickup`) | location_entered, npc_interacted_with | live probe (agent build/deploy → user launch) | PROBE-IN-BUILD |
| C++ NEEDS-LIVE (state-change, no anchor) | combat_started (IsInCombat edge) | combat_ended, damage_taken, damage_dealt | live probe (agent build/deploy → user launch) | PROBE-IN-BUILD |
| C++ caller-hop (interned constant) | perk_unlocked (`AddPerk`/`storm::addPerk`) | level_up, quest_stage_advanced | static read DONE → live confirm (hook `FUN_18046b704`) | PROBE-IN-BUILD |

## perk_unlocked static read — DONE (pilot-perk-static.md)
Static walk READ the unread caller-hop: the real grant fn is **`FUN_18046b704` @ RVA
`0x46b704`** — `__fastcall bool(void* perkCollection, GUID* perkId, void* ctx)`,
body-confirmed insert-if-absent + on-changed notify (abi_walker, not prologue-guess).
Verdict stays NEEDS-LIVE because the ANCHOR SELECTION is unproven: `FUN_182cee04c`
(the Lua `AddPerk` command impl) reaches it, but whether NATURAL-progression grants
also flow through `0x46b704` is RTTR/runtime-bound. The live confirm hooks
`0x46b704` entry → trigger a perk via play → if it fires, promote to STATIC-FINDABLE
+ AP18 seed row `perk_grant_insert -> bool (ptr,ptr,ptr) @ 0x46b704`.

## CORRECTED build model (user decision 2026-06-13) — DB by-name, NOT expert hatch

The events go into the DB as proper AP18-approved entities; the feature + probe
resolve them BY NAME (the disassembler-test cornerstone — the engine carries
address + ABI). The raw-RVA expert hatch is NOT the shipped surface; it is used
ONLY as the verification scaffold in the confirm launch.

**Sequence per pilot:** pin a verified address+ABI candidate (static RE) → ONE
confirmation launch that hooks the candidate by raw RVA *as verification only*
(confirm it's the right anchor + fires correctly) → seed the CONFIRMED anchor into
the DB via AP18 → rebuild the real `kcdx.on(<event>)` by-name feature + its cap-XX
test. Don't commit the DB to an unconfirmed anchor (results-driven).

## Verification levels (the gate is: pinned candidate BEFORE the confirm launch)

| Pilot | Address+ABI candidate | State |
|---|---|---|
| perk_unlocked | `FUN_18046b704` @ `0x46b704` — `bool(ptr coll, ptr guid, ptr ctx)`, body-read | **PINNED** — awaiting confirm launch (anchor-selection unproven: Lua-cmd path reaches it; natural-progression flow unconfirmed) |
| item_picked_up | `CScriptTable::CallFunction` slot 22 / +0xb0 = `FUN_180b9ceb4` @ `0xb9ceb4` — `u64(ptr scriptTable, ptr callDesc)`, body-read; desc->funcName at `*rdx` carries `"OnPickup"`/`"OnUse"`/… | **PINNED** — functionally GLOBAL (one shared `CScriptTable` class → one hook intercepts every entity `On<X>`; filter on funcName). Awaiting confirm launch: does a player pickup route through this slot with a knowable funcName? + volume/filter sanity (`pilot-pickup-pin.md`) |
| combat_started | **NO STATIC TRANSITION SITE** — getter `FUN_181a7dac0` @ `0x1a7dac0` is static; the WRITER is an inlined templated SetValue (2,523 candidate `mov [reg+8]` sites) + a runtime-bound listener list. front-1's old writer candidate (`FUN_18245e7c0`) was DISPROVEN (it's `System::Shutdown`). | **NO STATIC PIN** — structurally different. Either (a) hook the getter + edge-detect (fires on READ cadence, not the true transition) or (b) a live write-watch on `[prop+8]` (prop = `[combatComponent+0x90]+0xB60`) to DISCOVER the writer's address. Needs a discovery launch, not a confirm launch. (`pilot-combat-pin.md`) |

Goal: all three at "pinned candidate, awaiting live-confirm" → ONE launch confirms
all three → seed the confirmed ones (AP18) → by-name feature + cap-XX. Agent
writes/builds/deploys/reads-log per `agent-builds-and-deploys.md`; the launch is the
user's only gesture (pick up an item; enter combat; unlock a perk).

## Build-unknowns each pilot resolves
- **item_picked_up:** are entity-script `OnPickup` callbacks globally subscribable
  ONCE, or must they be wrapped per-entity script table? (gates all 3 LUA-EVENT events)
- **combat_started:** can a getter-hook + edge-detect cleanly observe the combat-state
  transition, or is the change-signal dispatcher needed? (gates all 4 combat/damage)
- **perk_unlocked:** does the `AddPerk`/`LearnPerk` fire-site resolve to a clean
  STATIC-FINDABLE address+ABI one caller-hop from the interned constant, or NEEDS-LIVE?

Probe findings capture into this dir (durable); seed rows only on a verified
address+ABI, AP18-approved. Delete this ledger when all 3 pilots conclude.
