# Front 3 — script-driven events (perk_unlocked / level_up / quest_stage_advanced / dialogue_line_spoken)

RE front for the Phase-10 gameplay-event catalog. HYPOTHESIS under test (NOT
assumed): KCD2 is heavily Lua-scripted, so these gameplay events are likely
**fired by the game INTO its Lua VM** as subscribable script-events / global
callbacks — meaning kcdx (which owns the one Lua VM) could subscribe in-VM with
no C++ hook RE (the LUA-EVENT class).

**Verdict up front: the hypothesis is NOT supported for any of the four events.**
KCD2 exposes gameplay to Lua as *callable commands* (CryEngine ScriptBind
function tables — Lua → C++), plus FlowGraph nodes and Flash UI events — none of
which is a subscribable "the game fired X" Lua callback a mod can listen on for
these four. The script-event surface that DOES exist is a *command* surface, not
an *event* surface. So the four events fall to STATIC-FINDABLE (a C++ notify/
dispatch anchor) or NEEDS-LIVE-CORRELATION, not LUA-EVENT.

Ladder: tiers 1–4 walked (predecessor Lua projects + CryEngine SDK headers +
binary string evidence), then ONE fresh-Ghidra pass (tier 5, last) to ground the
classification. Reuse-first; CLAIMS + evidence only; honest-uncertainty kept.

---

## Tier 3 — predecessor Lua projects: what the game's Lua surface looks like

### yobson1-kcd2lua (drives the KCD2 Lua VM) — NO game-event globals referenced
- The whole driver hooks `lua_pcall` + `update` + `luaL_loadfile` (signatures in
  `cpp/src/dllmain.cpp:26-28`) and runs arbitrary Lua off a queue. It references
  **zero** game-side event globals (`OnQuest`/`OnPerk`/`OnLevelUp`/listener
  registration) — `_research/predecessor-sigs/yobson1-kcd2lua/cpp/src/dllmain.cpp`.
  *Evidence value: WEAK absence* — this tool never NEEDS a game event (it just
  injects code), so its silence does not prove one doesn't exist. But it does
  show the established way to reach the VM is `lua_pcall`/`update` hooking, not a
  game event bus.

### muyuanjin-kcd2db (registers C++ INTO Lua + listens for save/load) — the real surfaces, named
The strongest predecessor: it actually registers C++ functions into the game's
Lua VM and subscribes to engine events. Two reachable, USED surfaces (verbatim
from `src/db/LuaDB.cpp`):
- **`CScriptableBase::Init(gEnv->pScriptSystem, gEnv->pSystem)` + `SCRIPT_REG_TEMPLFUNC(...)`**
  — registers C++ methods as Lua-callable functions on a global table
  (`SetGlobalName("LuaDB")`), then `m_pSS->ExecuteBuffer(db_lua, ...)` runs Lua
  (`LuaDB.cpp:242-279`). This is the CryEngine **ScriptBind** mechanism: C++ →
  callable-from-Lua. It is a *command* surface, NOT an event surface.
- **`gEnv->pGame->GetIGameFramework()->RegisterListener(this, "LuaDB", FRAMEWORKLISTENERPRIORITY_DEFAULT)`**
  (`LuaDB.cpp:280`) — the engine event-listener surface. The events it delivers
  are the `IGameFrameworkListener` set (`IGameFramework.h:368-381`):
  `OnPostUpdate` / `OnSaveGame` / `OnLoadGame` / `OnLevelEnd` / `OnActionEvent`
  / `OnPreRender` / `OnSavegameFileLoadedInMemory` / `OnForceLoadingWithFlash`.
  **None of these is perk / level / quest-stage / dialogue.** This C++ listener
  is how `save_created` (front-0) is reachable; it does NOT cover front 3.
- Reached via `IGame::CompleteInit()` (vtable slot 5) hook — `muyuanjin CLAUDE.md`.

**Bearing:** the game's Lua integration is `IScriptSystem` (ScriptBind: Lua calls
C++) + `IGameFramework` listener (a FIXED, non-gameplay event set). Neither gives
a subscribable "perk unlocked / leveled up / quest advanced / line spoken" Lua
callback.

---

## Tier 3b — CryEngine SDK headers (predecessor `external/cryengine/`): the framework event vocabulary
- `IScriptSystem.h` — the Lua VM interface. `BeginCall("Table","Func")` /
  `SetGlobalAny` / `AddFunction(SUserFunctionDesc{sFunctionName, sFunctionParams,
  sGlobalName, ...})` — the registration shape is `(name, "param,list", "GlobalTable")`.
  This shape is the discriminator used in the Ghidra pass below.
- `IGameFramework.h` — `enum EGameFrameworkEvent` includes `eGFE_ScriptEvent`
  (line 240) — a generic "script event" GOE event, entity-targeted, not a
  gameplay-event bus. `enum EActionEvent` (lines 253-278) is save/connect/level
  events, no gameplay. `IGameFramework` exposes `GetIDialogSystem()` (line 521)
  and `GetISubtitleManager()` (line 529) — both C++ interfaces (relevant to
  dialogue, below). These are CryEngine 2016-era headers; KCD2 is a customized
  CryEngine, so they are a LEAD, the binary is the authority (AP3).

---

## Tier 5 — fresh Ghidra (LAST tier): grounding the classification against WHGame.dll

WHGame.dll string-literal observation (script `extract_strings.py`, pefile-based;
1,129,078 strings; `autoexec.cfg` anchor confirmed present as a sanity check).
Then a read-only Ghidra headless pass (`FindEventAnchors.java` →
`event-anchors-recon.txt`) found every candidate event-name literal, its
referencing functions, and decompiled each body to read the discriminator
(ScriptBind/Lua-registration vs internal C++ notify vs UI/Flash).

### Ground-truth string observations (what surfaces EXIST)
- **Rich C++ ScriptBind classes** (C++ → Lua-callable), e.g. `CScriptBindGame`,
  `CScriptBind_Entity`, `CScriptBind_ActorSystem`, `C_ScriptBindActor@entitymodule`,
  `C_ScriptBindDialog@dialogmodule`, `C_FactionScriptBind@rpgmodule`,
  `C_LocationScriptBind@rpgmodule`, `C_ScriptBindCalendar@rpgmodule`,
  `C_PlayerStateHandlerScriptBind`. — RTTI string literals in WHGame.dll.
  *These expose C++ FUNCTIONS to Lua; they are not event-fire sites.*
- **Generic engine→Lua call machinery**: `CallScriptFunction`,
  `CallScriptFunctionInTable`, `Entity:CallScriptFunction`,
  `CallScriptFunctionWithParam`, `Script.Misc`, `Dialog:PlayDialog`. — string
  literals. The CryEngine pattern: engine calls a NAMED method on a SPECIFIC
  entity's script table. Not a global subscribable bus.
- **Entity Lua scripts present**: `Scripts/Entities/WH/Dialogue/DialogueHolder.lua`,
  `.../Others/LevelHolder.lua`, `.../Others/RandomEvent.lua`. — `.lua` path
  literals. These receive engine→Lua callbacks per-entity, not mod-subscribable.
- **Lua `OnX` callback names that DO exist** (entity-script lifecycle): `OnDamage`,
  `OnHit`, `OnKill`, `OnUse`, `OnInit`, `OnEnter`, `OnInventoryItemUsed`,
  `OnDespawn`, `OnLevelLoaded`, `OnCompanionEvent`, `OnGeneralEvent`, `OnEvent`.
  **CRITICAL ABSENCE: there is NO `OnPerkUnlocked`, `OnLevelUp`,
  `OnQuestStageAdvanced`, or `OnDialogLineSpoken`** in the binary's string table.
  The only perk/dialog `OnX` strings are UI-widget callbacks (`OnPerkCodexTextOk`,
  `OnBedInteractiveDialog`, `OnAmountDialogConfirmClicked`).
- **`CCustomEventManager` + `Entity:BroadcastEvent`** — a generic id-based custom
  event system (`FireEvent: ... event id: %u`), entity-scoped, not named-gameplay.
- **Perks/levels/skills are C++ RTTR + database objects** in `rpgmodule`:
  `C_AddPerkEffect`, `C_LearnPerkEffect`, `C_PerkUsedEffect`, `S_Perk`,
  `S_PerkScript`, `S_SkillLessonLevel`, `C_HasPerk`, `wh::rpgmodule::storm::addPerk`.
  — mangled RTTI/RTTR literals. Gameplay state is C++-owned, not Lua-owned.

### Decompiled bodies (read, per AP19 — the load-bearing edges)
Each of the 13 candidate event-name literals IS referenced by ≥1 function. The
referencing functions decompose into three kinds:

1. **Interned name-CONSTANTS (not events).** `QuestStateChanged` (`FUN_18192dfd8`),
   `LearnPerk` (`FUN_182cac05c`), `QuestObjectiveFinished` (`FUN_182cac640`),
   `ShowLevelUp`/`ShowPerkUsed`/`ShowQuestEvent` are each built by a one-time
   thread-safe `std::string` initializer (`FUN_1804f692c(&DAT_..., "QuestStateChanged")`
   guarded by `_Init_thread_footer`) that returns the interned string's address.
   — read in `event-anchors-recon.txt` (e.g. lines 366-384 for `QuestStateChanged`).
   These are NAMED IDENTIFIERS (keys/message-type tags) consumed elsewhere; the
   actual fire-site is one more caller hop away and was NOT read this pass
   (marked unverified, not asserted).
2. **A C++ script-function REGISTRATION table** (`FUN_181675cdc`): registers a
   coherent RPG-action table onto an object `param_1` —
   `FUN_180a52c20(param_1,"AddPerk","perk_id",param_1,&local_res8)`, alongside
   `"GetId"`, `"GetState"`/`"state"`, `"AddSkillXP"/"skill,xp"`,
   `"GetNextLevelSkillXP"/"skill,level"`, `"AddBuff"/"buff_id"`,
   `"RemoveBuff"`, `"AddPerk"/"perk_id"`, `"RemovePerk"/"perk_id"`. — read at
   `event-anchors-recon.txt` lines ~419-525. The `(name, "param,names", ...)`
   shape MATCHES the CryEngine `SUserFunctionDesc{sFunctionName, sFunctionParams}`
   / `SCRIPT_REG_TEMPLFUNC` registration convention (`IScriptSystem.h:544-557`).
   **CLAIM (grounded):** `AddPerk`/`RemovePerk`/`AddSkillXP` are C++ functions
   registered as callable methods on a script object — a Lua-CALLABLE COMMAND
   surface (Lua → C++). **NOT GROUNDED (one hop unread):** that `param_1` is the
   Lua script-system table specifically (vs a FlowGraph/RTTR registry) — reading
   `FUN_180a52c20`'s body would settle it; not done this pass. Either way it is a
   COMMAND registration, not an event/callback subscription.
3. **FlowGraph node + Flash UI**: `Dialog:PlayDialog` (`FUN_1819ea6f8`) is a
   `CAutoRegFlowNode<CFlowDialogNode>::vftable` registration — a FlowGraph node
   type, not a Lua event (read at lines 1206-1212). `FE_DialogueSpeaking` /
   `FE_DialogueIdle` (`FUN_180c2dcd8`, `FUN_180792888(uVar2,"FE_DialogueSpeaking")`,
   lines 940-944) are **Flash Events** (`FE_` = Scaleform GFx UI event) — the
   dialogue/subtitle UI channel, consistent with the loc-manager finding
   (`LOC-MANAGER-FINDINGS.md`: dialogue text reachable via the loc manager;
   firing function int-ID/runtime, not statically bridged).

---

## Per-event verdicts

| # | Event | Verdict | Anchor / why-not |
|---|---|---|---|
| 9 | perk_unlocked | NEEDS-LIVE-CORRELATION (lead: STATIC-FINDABLE) | NOT a LUA-EVENT — no `OnPerkUnlocked` Lua callback exists. Perks are C++ `rpgmodule` (RTTR `C_AddPerkEffect`/`C_LearnPerkEffect`, `wh::rpgmodule::storm::addPerk`). The `AddPerk`/`LearnPerk` strings are a Lua-CALLABLE command registration (`FUN_181675cdc` table) + interned RTTR effect-type names, NOT a fire-into-Lua event. A C++ anchor for "perk unlocked fired" likely exists (`C_LearnPerkEffect::Apply` / the `addPerk` storm operator) but the fire-site was not statically pinned (the name strings are constants/registrations, not the notify). Trigger a perk unlock in-game and correlate. |
| 10 | level_up | NEEDS-LIVE-CORRELATION (lead: STATIC-FINDABLE) | NOT a LUA-EVENT — no `OnLevelUp` Lua callback. `LevelUp`/`ShowLevelUp`/`LastSkillLevelUp`/`LastStatLevelUp`/`GetNextLevelSkillXP` are C++ rpgmodule strings; `Show*` are interned HUD message-type name constants (`FUN_1804f692c` initializers, NOT fire-sites). KCD2 splits **stat-level vs skill-level** (`LastStatLevelUp` / `LastSkillLevelUp` / `AddSkillXP` registration) — two distinct level surfaces, decide which `level_up` means. C++ notify anchor likely exists; not statically pinned. Live-correlate. |
| 11 | quest_stage_advanced | NEEDS-LIVE-CORRELATION | NOT a LUA-EVENT — no `OnQuestStageAdvanced` Lua callback. `QuestStateChanged` / `QuestObjectiveFinished` / `ShowQuestEvent` exist but are interned name-CONSTANTS (`std::string` initializers, `FUN_18192dfd8`/`FUN_182cac640`), i.e. event-type KEYS, not the fire function. Quest is C++/data-driven (questlog UI, `databasemodule` quest objects). The fire-site that uses the `QuestStateChanged` constant is one unread caller hop away. Trigger a quest-stage advance in-game and correlate the key consumer. |
| 12 | dialogue_line_spoken | NEEDS-LIVE-CORRELATION | NOT a LUA-EVENT for a per-line callback. Dialogue is C++ (`CDialogSystem::CreateSession`, `IDialogSystem`) + FlowGraph (`Dialog:PlayDialog` = `CFlowDialogNode`) + Flash UI (`FE_DialogueSpeaking`/`FE_DialogueIdle`). Per the line-text → loc int-ID, this aligns with `LOC-MANAGER-FINDINGS.md`: the spoken TEXT is reachable (loc manager, by-id getters slots 1/27/28 @ vtable `0x183dbcf90`), but the per-line FIRING function is int-ID/runtime and not statically bridged. The cleanest "a line was spoken" signal is a live hook on the loc by-id getter or the `FE_DialogueSpeaking` Flash dispatch (`FUN_180792888`) — both need live correlation. |

**No event in this front is LUA-EVENT.** The hypothesis that the game fires these
into the Lua VM as subscribable global callbacks is falsified by the string +
body evidence: the Lua surface is a CALLABLE COMMAND surface (ScriptBind), the
event vocabulary is C++ (`IGameFrameworkListener`'s fixed non-gameplay set,
`CCustomEventManager` id-based, FlowGraph nodes, Flash `FE_` events), and the four
gameplay events have no `OnX` Lua callback and no statically-pinned fire site.

## Honest-uncertainty / what was NOT settled (per AP19, not asserted)
- The actual C++ fire-SITE for each event (the function that USES the interned
  `QuestStateChanged`/`LevelUp` constant or calls `C_LearnPerkEffect::Apply`) was
  NOT pinned — the name strings resolve to constants/registrations, one caller
  hop short of the notify. A follow-up Ghidra pass walking the consumers of
  `&DAT_1855e3900` (QuestStateChanged), `&DAT_1855e3890` (LearnPerk), and the
  `LevelUp`/`AddSkillXP` consumers would settle STATIC-FINDABLE vs
  NEEDS-LIVE-CORRELATION per event. They are leaning STATIC-FINDABLE (a named
  C++ notify probably exists) but are recorded as NEEDS-LIVE-CORRELATION because
  the fire-site is not yet read.
- Whether the `FUN_181675cdc` registration target (`param_1`) is the Lua
  script-system specifically (vs FlowGraph/RTTR) is one unread hop
  (`FUN_180a52c20` body). It is a COMMAND registration either way.

## Artifacts (this dir, reuse-first / producer co-located)
- `extract_strings.py` — pefile ASCII-string extractor (reusable; the binary has
  no usable in-tree `strings` tool — this replaces it).
- `FindEventAnchors.java` — Ghidra post-script (event-name literal → referencing
  fns → decompiled bodies). Run: PowerShell `& analyzeHeadless.bat <proj> KCD2
  -process WHGame.dll -scriptPath <scripts> -postScript FindEventAnchors.java
  -noanalysis -readOnly` (the `.bat` needs PowerShell's call operator or a
  wrapper — Git Bash mangles the spaced path).
- `event-anchors-recon.txt` — raw decompile output (1240 lines).
