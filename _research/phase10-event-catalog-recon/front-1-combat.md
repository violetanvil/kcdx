# Front 1 — Combat-state events RE recon

Events: `combat_started`, `combat_ended`, `damage_taken`, `damage_dealt`.
Shared anchor: `IsInCombat` (seed ids 5/6/7/8 — prose-named 1004–1007; phase7-recon;
`FindIsInCombatSlot.java` / `DumpIsInCombatWrappers.java` / `TraceSetterAt0xB60.java`).

Ladder walked tiers 1–3 + tier-2 RTTI dump; classification settled WITHOUT a fresh
Ghidra run (the setter/dispatcher trace the design would need is the multi-hour walk
the triage defers — honest-uncertainty per skill §4 is the correct answer, not an
invented function). Each line is `<fact> — <evidence>`; no assembled conclusions.

## Verified facts table

| # | Fact | Evidence (tier) | Verified? |
|---|---|---|---|
| F1 | `IsInCombat` is a **vtable method**, slot index 1 (`[vtable+8]`), called on the object at `[combat-component + 0x0B60]`. | Seed id 7 prose + version row 7 sig `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08` = `mov rax,[rcx+8]; mov rcx,[rax+0x90]; add rcx,0x0B60; mov rax,[rcx]; call qword ptr [rax+8]` (tier 1). | YES — seed-recorded, live_test_plugin. |
| F2 | `IsInCombat` is a **state-QUERY getter**, not a transition event. It returns a combat-state enum compared against thresholds; callers branch on the return. | Two distinct call sites: id 7 ends `3C 02` (`cmp al, 2`, RVA 0x5605BC), id 8 ends `3C 01` (`cmp al, 1`, RVA 0x566040). Wrapper fns `FUN_1805605b8` (==2) and `FUN_180566040` (==1) per `DumpIsInCombatWrappers.java` header (tier 1 + tier 2). | YES — two thresholds = a graded getter, not an edge. |
| F3 | Four known call sites in WHGame.dll all CALL the getter; **none is a transition write/setter.** | Version rows: id 5 RVA 0x56174C, id 6 RVA 0x561745, id 7 RVA 0x5605BC, id 8 RVA 0x566040 — all `kind=callsite`, all the `add rcx,0x0B60; ...; call [rax+8]` shape (tier 1). | YES. |
| F4 | The +0x0B60 value is a `C_ModelProperty` whose trait is `C_CombatSignalWithNewValueTrait` — the engine fires a **change-signal** when the property's value changes. The owning class is `combatmodule::I_CombatActor` / `C_CombatActorModelOwnership`. | `_research/phase6-save-load/_phase6_rtti.txt` lines 96–105 (10 entries), e.g. `.?AV?$C_ModelProperty@_N...C_CombatSignalWithNewValueTrait@_NPEAVI_CombatActor@combatmodule@wh@@...C_CombatActorModelOwnership...` — a bool model-property carrying a combat-signal-on-new-value trait, parameterized by `I_CombatActor*` (tier 2, RTTI type descriptors). | YES — the type names are in the binary; this is the data-model shape. |
| F5 | The combat-signal **dispatcher function address** (the code that fires when the bool combat-state property flips) is NOT statically located in any existing artifact. | `TraceSetterAt0xB60.java` exists but its raw output was never archived (`grep` of `_research/` finds no captured setter dump); RTTI gives type descriptors, not the dispatch fn RVA (tier 2). | NO — unverified, not read this turn. |
| F6 | CryEngine's canonical event surface carries **no** hit/damage/combat callback. `IGameFrameworkListener` has no Hit/Damage/Death method; `ICombatLog` is a forward-decl only (no methods). | `_research/predecessor-sigs/muyuanjin-kcd2db/external/cryengine/include/cryengine/IGameFramework.h` — `struct ICombatLog;` (L25, fwd-decl); `IGameFrameworkListener` (L368) has no damage method; grep for `virtual.*(Hit|Damage|Death|Kill|Combat)` across the cryengine headers = 0 matches (tier 3, AP3 lead — empty). | YES — the canonical lead is empty; KCD2 combat is game-DLL-specific, not stock CryEngine. |
| F7 | No seed row, prior `_research/` dump, or predecessor sig names a **damage-apply / health-hit** function (a `TakeDamage` / `ApplyHit` / health-component write). | Grep `data/db-export/` + `_research/` (ex-predecessor) for `damage|health|hit|TakeDamage|OnHit` returns only incidental hits (cvar/fopen/license text), zero combat-damage function (tiers 1–2). The binary is 321K fns / 0.1% named (FINDINGS tier 2) — no name to grep. | YES — absence verified across the corpus. |

## Per-event verdict

### `combat_started` — **NEEDS-LIVE-CORRELATION**
The combat-state surface is fully a GETTER (F1–F3). The natural event anchor is the
combat-signal dispatcher for the bool `C_ModelProperty` at +0x0B60 (F4 — the engine DOES
fire on change, the `C_CombatSignalWithNewValueTrait` proves a change-signal exists), but
that dispatcher's address is NOT statically located (F5). To anchor `combat_started`:
hook the IsInCombat getter and edge-detect (false→true), OR locate the signal dispatcher.
Either path needs a live run — the getter-hook to confirm fire timing, or a live trace to
find the dispatcher. **No static anchor because** the only static surface is a query getter,
not a transition, and the change-signal dispatcher was never RE'd. Do NOT invent it.

### `combat_ended` — **NEEDS-LIVE-CORRELATION**
Same surface, same gap (F1–F5). The `cmp al, 1` vs `cmp al, 2` thresholds (F2) show
combat-state is GRADED (≥2 = one level, ≥1 = another) — so `combat_ended` is the edge
back to 0, detectable by the same getter-hook-and-edge-detect or signal-dispatcher path
as `combat_started`. No independent static anchor; rides the same live-correlation step.

### `damage_taken` — **NEEDS-LIVE-CORRELATION**
No static anchor (F6, F7). No damage/health/hit function is named in any seed row, prior
dump, or predecessor sig, and the binary's naming density (0.1%) makes name-grep useless.
The CryEngine lead is empty (F6). Locating a damage-apply function requires the
anchor→caller-graph-walk model on a live-triggered site (deal/take damage in-game, capture
the address that fired), or a fresh Ghidra walk from the health model-property RTTI
(analogous to F4's combat-signal traits — a `health`/`hp` `C_ModelProperty` likely exists
and is the right fresh-Ghidra starting anchor, but was not searched this turn). **Honest:
no clean static anchor exists today.**

### `damage_dealt` — **NEEDS-LIVE-CORRELATION**
Same as `damage_taken`. Likely the same damage-apply site observed from the attacker side
(victim vs attacker is an argument/field of one apply function, per the FINDINGS triage
note), but that is an UNVERIFIED inference until the apply function is located — flagged,
not asserted. No static anchor (F6, F7).

## Seed-row note

**No new seed row is warranted from this front.** Every combat fact that COULD be a row
(the IsInCombat getter + its call sites) is ALREADY seeded (ids 5/6/7/8). The events
themselves have no statically-verified address+ABI to record — recording one would be the
AP2/AP18 violation (an invented ABI / an unapproved row for an unlocated function).

When a future live-correlation or fresh-Ghidra step LOCATES the combat-signal dispatcher
or the damage-apply function, the candidate row would be (proposed, NOT yet verified):
- `combat_signal_dispatcher` → `void (ptr combatActor, <newValue>)` — the `C_CombatSignalWithNewValueTrait` fire fn for the bool combat-state property; ABI to be walked with `phase6_abi_walker.py`, NEVER prologue-guessed. Owning class `combatmodule::I_CombatActor` (F4).
- `damage_apply` → ABI unknown — locate via live-triggered caller-graph-walk first.

## Fresh-Ghidra starting anchors for the NEXT step (handoff, not run this turn)
- Combat dispatcher: trace the `C_CombatSignalWithNewValueTrait@_NPEAVI_CombatActor` vtable
  at the RTTI VAs `_phase6_rtti.txt` lines 96–105 (e.g. 0x184AFFB00) → the signal-fire slot.
  `TraceSetterAt0xB60.java` is the existing (un-run) script for the +0x0B60 setter side.
- Damage: search RTTI for a `health`/`hp`/`vitality` `C_ModelProperty` (same combatmodule
  pattern as F4) — the health-property change-signal is the damage-event anchor candidate.
