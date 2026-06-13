# Phase 10 gameplay-event catalog — RE triage findings

Triage for [`docs/outstanding-work/restructure/phase-10-event-catalog/`](../../docs/outstanding-work/restructure/phase-10-event-catalog/README.md).
Goal: for each candidate `kcdx.on(<event>)` gameplay event, find the backing
mechanism's address + ABI where statically discoverable, and classify each event:
**STATIC-FINDABLE** (RE settles the address + ABI; one confirmation launch) vs
**NEEDS-LIVE-CORRELATION** (no clean static anchor; must trigger in-game to locate
the call path) vs **LUA-EVENT** (the game already fires this into the Lua VM —
kcdx subscribes via its owned VM, no C++ hook RE needed).

Read the plan first ([phase-10 README](../../docs/outstanding-work/restructure/phase-10-event-catalog/README.md));
this holds the evidence + per-event verdicts.

## Ladder tiers 1–4 walked centrally (2026-06-13) — before any fan-out

Walked once at the top, so subagents don't each re-discover the shared corpus.

### Tier 1 — Address Library (`data/db-export/`): one event already done
- **`save_created` → seed id 144 `SaveGame`** — 7-arg `__fastcall(self, filename,
  reason, flag_a, arg5, flag_b, description)`, the save entry point fired on every
  save, **live-confirmed**. Verdict: **STATIC-FINDABLE, already in DB.** The event
  just needs the `kcdx.on("save_created")` bridge wired to the existing hook target
  (an engine-fan-out step, no new RE). Adjacent rows: 147 `DeleteSavegame`, 148
  `SaveGameRecord_SlotResolver`.
- No other candidate event matched a named seed row.

### Tier 2 — prior `_research/` dumps: the load-bearing constraint
- **`parallel-ghidra-research/inventory/ENUMERATION-FINDINGS.md`** — the binary is
  **321,120 functions, 0.1% named**, and the named 0.1% is library code (CRT/Win32/
  MSVC), NOT game subsystems. **There is no `damage`/`combat`/`dialogue`/`inventory`
  categorization to grep for.** Static discovery of a gameplay function = the
  **anchor → caller-graph-walk** model (string/cvar/loc-key literal ref → walk UP
  the caller graph). The call graph is the proven 93%-reliable backbone; string
  refs are sparse (~8% of fns ref any literal); on-screen text is int-ID-interned
  (zero LEA xrefs on the key).
- **`parallel-ghidra-research/LOC-MANAGER-FINDINGS.md`** — dialogue/quest TEXT is
  reachable (loc manager RE'd: ctor `FUN_1809f0ce4`, vtable @ `0x183dbcf90`), but
  the text→consuming-FUNCTION bridge is int-ID-based, runtime-determined, and
  UNPROVEN statically — needs a live dump. Bearing: `dialogue_line_spoken` and
  `quest_stage_advanced` are NOT cleanly static; the *text* is findable, the
  *function that fired the line* is not (without live correlation).

### Tier 3 — predecessor sigs (`_research/predecessor-sigs/`)
- `muyuanjin-kcd2db` — verified `gEnv` resolver (via `"exec autoexec.cfg"` string
  anchor) + 12+ vtable offsets. The entry into the engine-globals graph.
- `yobson1-kcd2lua` — KCD2 is **heavily Lua-scripted**; the predecessor tools drive
  the game's Lua VM. KCD2 gameplay events (quest/dialogue/perk/level) are classic
  CryEngine **script-event** territory — likely already fired into Lua. kcdx OWNS
  the one Lua VM (Phase 11) → for those, the anchor is a **Lua script-event /
  global callback**, not a C++ hook. This is the LUA-EVENT class.

### Tier 4 — Warhorse wiki: not yet queried per-event (low ABI value; carries behavior)

## The three-way classification (the triage product)

| Class | Meaning | Build path |
|---|---|---|
| **STATIC-FINDABLE** | RE settles address + ABI; one confirmation launch | RE → AP18 seed row → C++ fan-out hook → cap-XX |
| **NEEDS-LIVE-CORRELATION** | no clean static anchor; trigger in-game to locate the call path | anchor probe → live correlate → then as static |
| **LUA-EVENT** | game already fires it into the Lua VM | subscribe via kcdx's owned VM; no C++ hook RE |

## Per-event verdicts — FINAL (fronts reported 2026-06-13)

| # | Event | Verdict | Evidence / anchor | Source |
|---|---|---|---|---|
| 1 | save_created | **STATIC-FINDABLE (done)** | seed 144 `SaveGame` — 7-arg `__fastcall`, live-confirmed | tier 1 |
| 2 | item_picked_up | **LUA-EVENT** | entity-script callback `OnPickup` fired into the VM (literal present); no C++ GameplayRecorder `eGE_*` event for it | front 2 |
| 3 | location_entered | **LUA-EVENT** | area-trigger callbacks `OnEnterArea`/`OnLeaveArea`/`OnEnterNearArea` (literals); C++ `C_RPGLocationManager` setter = NEEDS-LIVE fallback | front 2 |
| 4 | npc_interacted_with | **LUA-EVENT** | entity-script `OnUse`/`OnUsed` (literals); C++ is RTTR-reflected `C_UseItemTrigger` + behavior-tree nodes, no single hookable fn | front 2 |
| 5 | combat_started | **NEEDS-LIVE-CORRELATION** | only a state-query getter exists (`IsInCombat`, seed 5/6/7/8, vtable slot 1 on `combatmodule::I_CombatActor`, prop `[+0xB60]`); engine fires a `C_CombatSignalWithNewValueTrait` change-signal but the dispatcher RVA is unpinned | front 1 |
| 6 | combat_ended | **NEEDS-LIVE-CORRELATION** | same surface/gap as #5; state is graded (`cmp al,1`/`cmp al,2`) — the edge back rides the same live dispatcher trace | front 1 |
| 7 | damage_taken | **NEEDS-LIVE-CORRELATION** | no damage/health/hit fn named in any seed/dump/predecessor; CryEngine lead empty (`IGameFrameworkListener` has no damage method); 0.1%-named binary = grep useless | front 1 |
| 8 | damage_dealt | **NEEDS-LIVE-CORRELATION** | as #7; *likely* the same apply-site from the attacker side — flagged inference, NOT asserted | front 1 |
| 9 | perk_unlocked | **NEEDS-LIVE-CORRELATION** (lead STATIC) | C++ `rpgmodule` (`C_LearnPerkEffect`/`C_AddPerkEffect`, `storm::addPerk`); `AddPerk`/`LearnPerk` are Lua-CALLABLE commands + RTTR type-names, not a fire-into-Lua event; fire-site one unread caller-hop away | front 3 |
| 10 | level_up | **NEEDS-LIVE-CORRELATION** (lead STATIC) | C++; `LevelUp`/`ShowLevelUp` interned HUD constants. KCD2 splits **stat-level vs skill-level** (`LastStatLevelUp`/`LastSkillLevelUp`) — a design pick | front 3 |
| 11 | quest_stage_advanced | **NEEDS-LIVE-CORRELATION** | C++/data-driven; `QuestStateChanged`/`QuestObjectiveFinished` are interned event-KEY constants, not the fire fn; consumer one hop unread | front 3 |
| 12 | dialogue_line_spoken | **NEEDS-LIVE-CORRELATION** | `CDialogSystem` + FlowGraph (`Dialog:PlayDialog`) + Flash (`FE_DialogueSpeaking`); matches loc-manager (text reachable, per-line fire int-ID/runtime). Cleanest signal = live hook on loc by-id getter or `FE_DialogueSpeaking` | front 3 + loc-manager |

### The shape that emerged (three findings the candidate list did not assume)

1. **The Lua surface is a CALLABLE-COMMAND surface, not an event bus** (front 3, hypothesis FALSIFIED by direct read of WHGame.dll's 1.13M literals). There is NO `OnPerkUnlocked`/`OnLevelUp`/`OnQuestStageAdvanced`/`OnDialogLineSpoken` Lua callback. The predecessor Lua tools CALL into the VM (Lua→C++ ScriptBind); the game does not fire RPG/quest/dialogue events back OUT to a subscribable Lua global.
2. **BUT the ENTITY-script callbacks DO fire into the VM** (front 2): `OnPickup`/`OnUse`/`OnEnterArea` are real CryEngine entity-script callbacks (60+ `OnXxx` family + `ScriptBind_Entity`/`Entity:CallScriptFunction` dispatch present). So `item_picked_up`/`location_entered`/`npc_interacted_with` ARE LUA-EVENT — but via the **entity-script** mechanism, not an RPG event bus.
3. **Combat is a GETTER, not transition events** (front 1): the whole `IsInCombat` surface is a state-query. No transition write is hooked; the change-signal dispatcher was never RE'd.

### Net triage
- **0 events are cleanly STATIC-FINDABLE-and-new** (save_created is already in the DB).
- **3 are LUA-EVENT** (item_picked_up, location_entered, npc_interacted_with) — pending ONE live build-unknown (see below).
- **8 are NEEDS-LIVE-CORRELATION** — the firing function is real but its address is unpinned, and a live probe (or one more caller-hop read) is required to locate it.

### The single load-bearing build-unknown for the LUA-EVENT path
Whether the `OnPickup`/`OnUse`/`OnEnterArea` entity-script callbacks are **globally subscribable once** vs **must be wrapped per-entity script table** — settled only by a live probe on the entity script-call dispatch (front 2). This is the real Phase-10 build question for the LUA-EVENT class, not yet answered.

### Seed rows
**NONE proposed this pass.** No event reached the STATIC-FINDABLE bar with a new verified address + ABI, so nothing is recorded into `data/db-export/` (an invented address would violate AP2/AP18). When a future step LOCATES a fire-site, the front files carry the candidate ABI shapes (e.g. front 1: `combat_signal_dispatcher -> void (ptr combatActor, <newValue>)`, ABI via `abi_walker`, AP18 approval required).

## Correction (front 2)
The triage's tier-2 note above referenced an "inventory front" as if it were inventory-system RE. It is NOT — `_research/parallel-ghidra-research/inventory/` is the 321K-function ENUMERATION (the function inventory). There is **no prior inventory/entity/pickup RE** in the corpus; front 2's entity-script findings are new.

## Existing anchors worth handing each front
- `IsInCombat` is already RE'd (seed rows 1004/1005 reference the outfit-swap
  callsite that calls into it; the IsInCombat slot work is in `phase7-recon`/the
  `FindIsInCombatSlot.java` Ghidra script) → the combat fronts START there.
- `gEnv` resolver + the entity system are reachable via the muyuanjin recipe.
- The CryEngine `IGameFramework` / `IEntitySystem` listener interfaces are the
  canonical CryEngine event surfaces — a *lead* (AP3: the binary wins), not the answer.
