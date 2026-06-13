# Front 2 — entity/world events (item_picked_up, location_entered, npc_interacted_with)

RE front for the Phase 10 gameplay-event catalog. Shared anchor: the CryEngine
entity system + KCD2's `wh` game modules. Returns CLAIMS + evidence, not
assembled conclusions. Every claim cites the tier it came from.

## Method + tier reached

- Tier 1 (Address Library, `data/db-export/`): **no entity/inventory/area/
  interaction row exists.** The only adjacent named rows are the ModManager
  init-cycle work (id 137/141/142/143) and the save/load hooks (144–148). No
  match for any front-2 event. (`grep` over `address_names_seed.csv`, 158 rows.)
- Tier 2 (prior `_research/` dumps): **no prior inventory/entity-system/pickup/
  interaction RE.** NB — the brief calls
  `_research/parallel-ghidra-research/inventory/` "the inventory front"; that dir
  is the function-ENUMERATION inventory (the 321K-fn CSV + ENUMERATION-FINDINGS),
  **not** an inventory-SYSTEM RE. There is no inventory-system front to reuse.
  The reusable prior fact carried forward: discovery = anchor → caller-graph-walk
  (321K fns, 0.1% named, no subsystem categorization to grep).
- Tier 3 (predecessor sigs): the muyuanjin `external/cryengine` headers carry the
  CryEngine surfaces — `IGameplayListener::OnGameplayEvent(IEntity*,
  GameplayEvent&)`, `IGameFrameworkListener` (`OnLevelEnd`, `OnActionEvent`), and
  the `gEnv->pEntitySystem` (`IEntitySystem`) pointer at env.h offset for the
  entity system. These are LEADS (AP3: the binary wins), confirmed against the
  binary below.
- Tier 5 (the binary itself, read-only): a string-anchor probe of `WHGame.dll`
  (`find_strings.py` + `enum_subsystems.py`, here) — ground-truth observation of
  which event anchors exist as literals, BEFORE any caller-walk. **No fresh
  Ghidra postscript run, no caller-body read** — the verdict does not need an RVA,
  and asserting a C++ edge/RVA not read in a body would be AP19/AP2. Honest stop.

The string extractor was needed because no `strings` binary is on PATH and the
binary mixes ASCII + UTF-16LE literals; control anchors (`exec autoexec.cfg`,
`LocalizedStringManager.cpp`, `console command`) all hit → extractor validated.

## Ground-truth facts (binary string anchors)

| Fact | Evidence (literal in WHGame.dll) | Tier |
|---|---|---|
| Entity system statically linked | `CryEngine\CryEntitySystem\EntitySystem.cpp`, `CArea`, `CAreaManager`, `IAreaManager`, `CEntityAreaProxy`, `SEntityEvent`, `IEntityEventListener` | 5 |
| Entity↔Lua bridge present | RTTI `.?AVCScriptBind_Entity@@`; `CryEntitySystem\ScriptBind_Entity.cpp`; `Entity:CallScriptFunction`, `CallScriptFunctionInTable[WithParam]`, `ActivateOutput called with undefined event %s for entity %s` | 5 |
| **Full CryEngine entity-script-event family present** | 60+ `On<Xxx>` literals incl. `OnPickup`, `OnUse`, `OnUsed`, `OnEnterArea`, `OnLeaveArea`, `OnEnterNearArea`, `OnAudioListenerEnterArea/LeaveArea`, `OnHit`, `OnSpawn`, `OnReset` | 5 |
| CryAction GameplayRecorder present | RTTI `.?AVCGameplayRecorder@@`, `IGameplayRecorder`; `SendGameplayEvent`; `GameplayRecorder.cpp` | 5 |
| GameplayRecorder enum is STOCK MP set | `eGE_Connected/Disconnected/Rank/Scored/RoundEnd/SuddenDeath/WeaponHit/Currency/Damage/Death/...` — **NO `eGE_ItemPickedUp / eGE_LocationEntered / eGE_Interact`** | 5 |
| Item system present + Lua-exposed | `CryAction\ItemSystem.cpp`; RTTI `.?AVCItemSystem@@`, `.?AVCScriptBind_ItemSystem@@`; `EquipmentManager.cpp` | 5 |
| Pickup/use modeled as RTTR NPC-AI actions | `C_PickUpAction@NPCState@xgenaimodule@wh`, `C_PickUpFromSetAsideAction`, `S_ActorAnimPickUpRequest@entitymodule@wh`, `C_UseItemTrigger@entitymodule@wh` (templated, RTTR-reflected) | 5 |
| **KCD2 has its own location subsystem** | RTTI `.?AVC_RPGLocationManager@rpgmodule@wh@@`, `I_RPGLocationManager`; `game\modules\rpgmodule\Location\LocationManager.cpp`; fields `m_LocationId`, `m_location`; a `_Binder` to a `C_RPGLocationManager` member taking `CryStringT` (a location-name setter) | 5 |
| Area/proximity triggers present | `CArea`, `CAreaManager`, KCD2 `C_TriggerAreaManager@xgenaimodule@wh`, `C_RegisterProximityTrigger` (behavior-tree nodes), `CEntityAreaProxy` | 5 |

## Per-event analysis + verdict

### item_picked_up — LUA-EVENT (primary) / NEEDS-LIVE-CORRELATION (C++ fallback)

- The CryEngine pickup signal into scripts is the entity-script callback
  **`OnPickup`** (present as a literal; the engine calls the picked-up entity's
  Lua script table). KCD2 owns the one Lua VM (Phase 11) → kcdx subscribes there;
  no C++ hook RE needed. This is the LUA-EVENT class.
- There is **NO C++ GameplayRecorder event** for pickup (`eGE_*` enum has none).
  The C++ pickup machinery is `CItemSystem` + RTTR-reflected `wh` NPC-AI action
  types (`C_PickUpAction`, `S_ActorAnimPickUpRequest`) — heavily templated, not a
  single hookable "player picked up item X" function. A C++ anchor, if wanted,
  would be a specific `CItemSystem`/inventory-add method — **not statically pinned
  here** (no RVA walked; would need Ghidra + abi_walker + live confirmation that
  it fires on player pickup). Hence the C++ path is NEEDS-LIVE-CORRELATION.
- **Verdict: LUA-EVENT.** Backed by the `OnPickup` entity-script callback +
  `ScriptBind_Entity` + `CallScriptFunction` dispatch. C++ hook path is a
  fallback that needs live correlation, not a clean static anchor.

### location_entered — LUA-EVENT (primary) / NEEDS-LIVE-CORRELATION (C++ fallback)

- Two surfaces. (a) **Area-trigger Lua callbacks** `OnEnterArea` / `OnLeaveArea`
  / `OnEnterNearArea` — the CryEngine Area entity fires these into the area's Lua
  script when the player enters/leaves a trigger volume. LUA-EVENT. (b) **KCD2's
  `C_RPGLocationManager`** owns a named "current location" (`m_LocationId`,
  `m_location`) with a member setter taking a `CryStringT` (the `_Binder`
  evidence) — the "you entered <named location>" notification almost certainly
  flows through this setter. That C++ edge is **not walked** here (no RVA, no
  caller body read) → NEEDS-LIVE-CORRELATION to confirm which method fires on a
  location change and what it's passed.
- `IGameFrameworkListener::OnLevelEnd(const char* nextLevel)` is NOT the right
  anchor — KCD2 is one streamed open world; a level-end fires on a map/region
  swap, not on in-world location entry.
- **Verdict: LUA-EVENT** (via Area `OnEnterArea`) as the clean subscription path;
  the `C_RPGLocationManager` C++ setter is a NEEDS-LIVE-CORRELATION fallback for a
  named-location signal.

### npc_interacted_with — LUA-EVENT (primary) / NEEDS-LIVE-CORRELATION (C++ fallback)

- The CryEngine interaction/use signal into scripts is the entity-script callback
  **`OnUse` / `OnUsed`** (present as literals; fired on the used entity's Lua
  script table when the player interacts). For an NPC entity this is the
  "interacted with" hook. LUA-EVENT.
- The C++ side is the RTTR-reflected `C_UseItemTrigger@entitymodule@wh` +
  `moveableInteractionTrigger` (a reserved modid, wiki KM-A-35) + the
  behavior-tree proximity-trigger nodes — interaction is modeled as
  reflected/behavior-tree data, **not a single hookable C++ interaction
  function**. No static anchor pinned (no RVA walked).
- **Verdict: LUA-EVENT** (via entity `OnUse`/`OnUsed`); C++ path is
  NEEDS-LIVE-CORRELATION.

## Why no STATIC-FINDABLE verdict for any of the three

A STATIC-FINDABLE verdict (per FINDINGS.md) means RE settles address + ABI; one
confirmation launch. None of the three reaches that bar:
- KCD2 did NOT extend the C++ GameplayRecorder enum (`eGE_*`) for these events —
  the one central C++ broadcast that WOULD have been a clean static anchor does
  not carry them.
- The C++ machinery that DOES exist (CItemSystem, C_RPGLocationManager,
  C_UseItemTrigger, the RTTR/behavior-tree action types) is per-event scattered,
  templated/reflected, and its "fires on the player doing X" edge is unproven
  statically — pinning an RVA + confirming the edge needs Ghidra + abi_walker +
  a live launch (NEEDS-LIVE-CORRELATION), not a settled static fact.
- The clean, low-RE path is the Lua entity-script callbacks (`OnPickup`,
  `OnEnterArea`, `OnUse`/`OnUsed`), which the engine already fires into the VM
  kcdx owns — LUA-EVENT, the same class FINDINGS.md anticipated for quest/dialogue
  events.

## Open follow-ups (for the synthesizer / a later /execute, NOT done here)

- **Per-entity vs global Lua subscription (the load-bearing build unknown).** The
  `OnPickup`/`OnUse`/`OnEnterArea` callbacks are the CryEngine convention of the
  engine calling a PER-ENTITY script table's method — NOT proven to be a single
  global broadcast kcdx can subscribe to once. Whether kcdx can hook these
  globally (one subscription for all entities) vs must wrap per-entity script
  tables is **unverified** and is the real Phase-10 build question for these
  events. A live probe (own the VM, install a global hook on the entity
  script-call dispatch `CallScriptFunction`, log which `On<X>` fires for which
  entity) settles it — that is the NEEDS-LIVE-CORRELATION work if the LUA-EVENT
  path is taken. Checkable; do not assume (AP10/AP19).
- If a C++ anchor is wanted for any event, the next step is a targeted Ghidra
  pass (NOT done here): `CItemSystem` pickup method / `C_RPGLocationManager`
  location setter / the entity `OnUse` ProcessEvent dispatch — each then
  abi_walker'd and live-confirmed it fires on the player action.

## Worker scripts (reusable, co-located)

- `find_strings.py` — ASCII+UTF-16LE string extractor for WHGame.dll + regex
  anchor sweep. `python find_strings.py <regex>...` or `--control`.
- `enum_subsystems.py` — enumerates embedded CryEngine/`wh` source-subsystem dirs
  from the .cpp path literals (which subsystems are statically linked).

(No `_*.txt` raw dumps committed — outputs are short and reproducible from the
two scripts above; re-run for the full lists.)
