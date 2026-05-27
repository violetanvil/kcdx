# Phase 6 — Save/Load lifecycle hook research (FINDINGS)

Survey date: 2026-05-19. Game version: `release_1_5_1164953_841`.
Target binary: `WHGame.dll`, ImageBase `0x180000000`,
`.text` `VA=0x180001000 size=0x3A01000` (per [`_phase6_strings_out.txt`](_phase6_strings_out.txt) line 12).

## Goal

Locate stable hook points in `WHGame.dll` that let kcdx dispatch SKSE-style
save/load lifecycle messages — `kPreLoadGame`, `kPostLoadGame`, `kSaveGame`,
`kDeleteGame` — to plugins. Each hook target needs:

- Static `.text` AOB that resolves to exactly one function entry.
- Standard Windows x64 calling convention recoverable from prologue.
- Main-thread invocation (static evidence sufficient; live confirmation
  scheduled as an open question).
- Single fire per save/load/delete operation (best-effort static evidence).
- No schema changes to kcdx's existing `[[hook]]` TOML block.

The companion file [`SAVE-LOAD-CANDIDATES.md`](SAVE-LOAD-CANDIDATES.md) is the
formal dossier with one block per lifecycle message; this document captures the
methodology, alternatives considered, and follow-up work that didn't fit a
candidate block.

## Methodology (mirrors CAP-03)

The pipeline is the same shape as
[`predecessor-sigs/CAP-03-CANDIDATES.md`](../predecessor-sigs/CAP-03-CANDIDATES.md)
but extended for the multi-target case. Steps in order:

1. **Predecessor-sig grep first.** Re-verified yobson1's `update`,
   `lua_pcall`, `luaL_loadfile` sigs and muyuanjin's `exec autoexec.cfg`
   gEnv anchor against the current build. All PASS — see "PREDECESSOR SIGS"
   and "MUYUANJIN gEnv anchor chain" sections of
   [`_phase6_final_verify.txt`](_phase6_final_verify.txt) (lines 1-17).
   The v1.4+ context-byte signature `4C 8B 92 18 01 00 00` preceding the
   LEA at `0x18086ADB0` is PRESENT, confirming we can still rely on
   muyuanjin's gEnv resolver if and when we adopt Strategy A.
2. **String-anchor enumeration.** Run
   [`phase6_dump_string_anchors.py`](phase6_dump_string_anchors.py) to find
   NUL-bounded C strings related to save/load and a loose-fit substring scan
   for partial hits. Output: [`_phase6_strings_out.txt`](_phase6_strings_out.txt).
   Augment with regex grep via
   [`phase6_grep_strings.py`](phase6_grep_strings.py) and `.rdata`
   region dumps via
   [`phase6_dump_strings_region.py`](phase6_dump_strings_region.py)
   for distinctive format strings like
   `"Loading saved game '%s' %s, created by '%s', ver. %d ..."`,
   `"SaveGame: '%s' %s. [Duration=%.4f secs]"`,
   `"Saving failed : player is dead!"`,
   `"Deleting savegame %d (%s) of playline %d"`,
   `"SaveGameManager::PostLoadGame"`,
   `"Loading last saved game."`,
   `"Quit requested by C_UISaveLoad::ExitGame"`.
3. **RTTI scan.** Run
   [`phase6_rtti_and_class_anchors.py`](phase6_rtti_and_class_anchors.py)
   to enumerate `.?AV...@@` RTTI type descriptors related to save/load/serialize
   in `.data`. Output: [`_phase6_rtti.txt`](_phase6_rtti.txt). Surfaces the
   `wh::framework::C_SaveGameManager` class (RTTI @ `0x184A66630`),
   `C_SaveGameDescription`, `C_WHSerializerXml`, `C_CrySaveGameHelper`,
   `C_CryLoadGameHelper`, `C_SaveInputZlibStream`, `CSaveWriter_CryPak` /
   `CSaveReader_CryPak`, `C_RTTRSerializer`, `ISaveGame`/`ILoadGame`
   interfaces, plus the `wh::conceptmodule` 5-phase load-completion enum
   discussed under "Forward-looking" below.
4. **Xref → function-entry walk.** For each anchor string VA, run
   [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) to find
   every `48 8D 15 ?? ?? ?? ??` (`LEA RDX, [rip+imm32]`) instruction in `.text`
   that targets it, then walk back to the containing function's entry and emit
   both a Tier-1 (24-byte) and Tier-2 (40-byte) prologue signature. The script
   also re-scans `.text` to report uniqueness count for each signature.
   Outputs:
   - [`_phase6_xrefs_pass1.txt`](_phase6_xrefs_pass1.txt) — first batch
     (LoadGame opener log, last-saved-game log, etc.)
   - [`_phase6_xrefs_pass2.txt`](_phase6_xrefs_pass2.txt) — PostLoadGame
     anchor, UpdateSaveGameDescriptions, ExitGame
   - [`_phase6_xrefs_pass3.txt`](_phase6_xrefs_pass3.txt) — SaveGame
     duration log, SaveGame player-dead log, LoadCryEngineData
   - [`_phase6_xrefs_delete.txt`](_phase6_xrefs_delete.txt) — DeleteSavegame
     and DeleteAllSavegamesOfPlayline
5. **Caller-chain walk.** For each function-entry VA discovered above, run
   [`phase6_find_callers.py`](phase6_find_callers.py) to scan `.text` for
   `E8 imm32` direct CALL instructions whose targets resolve to the entry,
   then walk back to the calling function's own entry and emit prologue sigs.
   This produces the orchestrator-→-wrapper-→-target chain that justifies
   the "thin wrapper, one upstream caller" claims in the candidate dossier.
   Outputs: [`_phase6_callers.txt`](_phase6_callers.txt) (for
   `LoadGame`, `PostLoadGame`, `cryaction_postloadgame_string_owner`,
   `C_UISaveLoad::LoadLastSave`) and
   [`_phase6_callers2.txt`](_phase6_callers2.txt) (for the LoadGame
   wrapper `0x1825BCEEC`).
6. **Single-bb call dump (sanity).** For specific functions where we want a
   full picture of outbound calls (e.g. PostLoadGame's 14 callees, SaveGame's
   tail-call structure), run
   [`phase6_dump_function_calls.py`](phase6_dump_function_calls.py). Used
   to confirm e.g. SaveGame is a thin wrapper terminating in a tail call to
   `0x180A61D00`, and to look for `CreateThread`/`QueueUserWorkItem`
   primitives in any of the four bodies (none found — see "Thread context"
   below).
7. **Final uniqueness verification.** Run
   [`phase6_verify_existing_sigs.py`](phase6_verify_existing_sigs.py)
   over the full set: predecessor sigs (must still PASS), the muyuanjin gEnv
   anchor chain (must still resolve), and every Phase 6 candidate sig (Tier-1
   and Tier-2 where applicable). Output: [`_phase6_final_verify.txt`](_phase6_final_verify.txt).
   All entries `[PASS] N hit(s) (expect 1)`.

This is the same shape as CAP-03 (string anchor → LEA xref → function entry →
prologue sig → uniqueness verify), just done four times with caller-chain
walks layered on for additional confidence.

## Recommended hook surface — Strategy B (function-entry detours)

Four `wh::framework::C_SaveGameManager` methods, all `__thiscall`. Detail in
[`SAVE-LOAD-CANDIDATES.md`](SAVE-LOAD-CANDIDATES.md); summary:

| Lifecycle message | Function | RVA | VA | Confidence |
|---|---|---|---|---|
| `kPreLoadGame` | `C_SaveGameManager::LoadGame` | `0x25BD1BC` | `0x1825BD1BC` | HIGH |
| `kPostLoadGame` | `C_SaveGameManager::PostLoadGame` | `0x25BCF94` | `0x1825BCF94` | HIGH |
| `kSaveGame` | `C_SaveGameManager::SaveGame` | `0x3581B04` | `0x183581B04` | HIGH |
| `kDeleteGame` | `C_SaveGameManager::DeleteSavegame` | `0x25BC510` | `0x1825BC510` | MEDIUM-HIGH |

All four Tier-2 (40-byte) AOBs verified unique in `.text`
([`_phase6_final_verify.txt`](_phase6_final_verify.txt) lines 22-49).
Tier-1 (24-byte) sigs collide with sibling methods that share the class
prologue:

- `LoadGame` Tier-1: 4 hits (3 sibling collisions)
- `PostLoadGame` Tier-1: 3 hits (2 sibling collisions)
- `SaveGame` Tier-1: 2 hits (1 sibling collision)
- `DeleteSavegame` Tier-1: 400 hits — the prologue
  `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40`
  is an MSVC `__thiscall` register-save pattern that this build emits all
  over `.text`

`DeleteSavegame`'s Tier-1 collision count alone justifies the Tier-2 recommendation
for the whole set. We pin the contract on the 40-byte sigs for v0.1.

## Major alternative considered — Strategy A (CryEngine listener)

`muyuanjin/kcd2db` ships and works on KCD2 1.5 by implementing CryEngine's
`IGameFrameworkListener` interface and registering it via
`gEnv->pGame->GetIGameFramework()->RegisterListener(this, name, priority)`.
The engine then calls `OnSaveGame(ISaveGame*)` and `OnLoadGame(ILoadGame*)`
on every save/load — no detour needed. Full interface declaration at
[`predecessor-sigs/muyuanjin-kcd2db/external/cryengine/include/cryengine/IGameFramework.h`](../predecessor-sigs/muyuanjin-kcd2db/external/cryengine/include/cryengine/IGameFramework.h)
lines 368-380 (six virtual hooks: `OnSaveGame`, `OnLoadGame`, `OnLevelEnd`,
`OnActionEvent`, `OnPreRender`, `OnSavegameFileLoadedInMemory`).
`RegisterListener` declared at line 714 of the same header. Resolver path at
[`predecessor-sigs/muyuanjin-kcd2db/src/kcd2db.cpp`](../predecessor-sigs/muyuanjin-kcd2db/src/kcd2db.cpp)
line 41 onward — string anchor `"exec autoexec.cfg"` at `.rdata 0x184095E58`,
LEA xref at `0x18086ADB0`, v1.4+ context-byte signature
`4C 8B 92 18 01 00 00` preceding the LEA. Both still PASS-verified against
the current build (see [`_phase6_final_verify.txt`](_phase6_final_verify.txt)
lines 13-17).

### Why Strategy A is NOT v0.1

Two independent reasons, either one disqualifying:

1. **Schema mismatch.** Strategy A is a vtable hook on `IGame::CompleteInit`
   (to interpose listener registration) plus a runtime listener registration
   after `gEnv` populates. That requires (a) gEnv-resolver infrastructure
   kcdx does not have yet, (b) a new `[[vtable_hook]]`-style TOML schema
   entry, and (c) timing logic that defers registration until after engine
   init. This is the same blocker that pushed `IGame::CompleteInit` from
   CAP-03 candidate #2 ([`predecessor-sigs/CAP-03-CANDIDATES.md`](../predecessor-sigs/CAP-03-CANDIDATES.md)
   lines 74-95) to the v0.2+ roadmap.
2. **Listener interface doesn't cover all four messages.** Even with the
   schema work done, the CryEngine `IGameFrameworkListener` interface only
   surfaces *post-load* and *save* events. It has **no `OnPreLoadGame`**
   (pre-load is only partially observable via the optional
   `OnSavegameFileLoadedInMemory(const char* pLevelName)` override at
   line 379 of `IGameFramework.h`, which fires after the file is in memory
   but the semantics aren't a clean "before deserialization") and **no
   `OnDeleteGame` at all**. To deliver SKSE-parity messages for all four
   lifecycle stages, function-entry detours are the only path.

### Decision

Strategy B (the four Tier-2 AOBs in [`SAVE-LOAD-CANDIDATES.md`](SAVE-LOAD-CANDIDATES.md))
for v0.1. Document Strategy A as a v0.2+ candidate; revisit when kcdx grows
the gEnv resolver infrastructure for the broader CryEngine subsystem hookup
work.

## Save format hints (informational)

Surfaced incidentally by the string scan; not required for the four hook
points but useful for future co-save / parallel-file work
(`kPostLoadGame` handler wanting to deserialize a sibling file alongside
the .whs that just loaded).

- **File naming**: `autosave%03d.whs`, `quicksave%03d.whs`, `save%03d.whs`,
  `crucialdecision%03d.whs`, `permanent%03d.whs`, `exit.whs` (format strings
  at `.rdata 0x183E16828` – `0x183E169E0`). The `.whs` extension is uniform.
  Suggests **`.kcdx`** for any kcdx co-save naming convention to mirror the
  established three-letter pattern and remain unambiguous.
- **IO stack**: `CSaveWriter_CryPak` / `CSaveReader_CryPak` wrap CryPak
  archive IO; `C_SaveInputZlibStream` handles zlib decompression;
  `C_WHSerializerXml` handles XML metadata. RTTI confirms in
  [`_phase6_rtti.txt`](_phase6_rtti.txt) lines 9-13 and 68-77.
- **Save directory CVar**: `sys_user_folder.KingdomCome2` (mentioned at
  `.rdata 0x183DC8247` per [`_phase6_strings_out.txt`](_phase6_strings_out.txt)
  line 53).
- **Description schema**: `%d|%d|%s|%s|%d|%s|%f|` — (playline, slot, name,
  mod-list, game-mode, location, timestamp). Format string lives in
  `C_SaveGameDescription`-region `.rdata`.

## Thread context

All four candidate functions are reached from the `CCryAction::PreUpdate` /
`PostUpdate` chain that drives the per-frame `update` kcdx already hooks
(per hard rule #16 in [`kcdx/CLAUDE.md`](../../../CLAUDE.md)). Static
analysis via
[`phase6_dump_function_calls.py`](phase6_dump_function_calls.py) found:

- No `CreateThread` / `_beginthreadex` / `QueueUserWorkItem` / similar
  thread-spawn primitives in any of the four bodies (LoadGame ~125 B,
  PostLoadGame ~570 B, SaveGame ~248 B, DeleteSavegame ~comparable).
- LoadGame's body terminates in a call to `[rax+0x28]` followed by
  `C_SaveGameManager::UpdateSaveGameDescriptions` (entry @
  `0x1806EBF34`, sig verified unique in
  [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 41), which
  itself is a non-threaded UI-list refresh.
- SaveGame's body is a thin wrapper that does one `[rax+0x200]` vtable
  dispatch and tail-calls `0x180A61D00`. No thread spawn.
- PostLoadGame fires 14 outbound calls, the most notable being the
  action-event dispatcher at `0x180FBE628` (entry sig also verified unique).

Conclusion: **static evidence is consistent with main-thread invocation
for all four**. We follow hard rule #16 and recommend a debug-mode
`GetCurrentThreadId()` assert against kcdx's captured main-thread ID on
first invocation of each hook — see Open Question #4 below.

## Open questions — need live in-game verification

These are NOT blockers for v0.1 implementation; they are confirmations to
schedule against the dev build of the game once the four hooks are wired up.

1. **Fire ordering for LoadGame relative to deserialization.** The
   `"Loading saved game '%s' %s, created by '%s', ver. %d ..."` log string
   at `.rdata 0x183E16C00` is referenced by exactly one LEA from inside
   `C_SaveGameManager::LoadGame` at `0x1825BD2F2`
   ([`_phase6_xrefs_pass1.txt`](_phase6_xrefs_pass1.txt) lines 4-13).
   It reads syntactically like an opener (logs *that* a load is starting,
   not its outcome), which would make hooking `LoadGame`'s entry the
   correct `kPreLoadGame` site. But a hook at the function entry fires
   *before* the body runs — which means before the `[rax+0x28]` vtable
   dispatch that probably does the deserialization. If live testing shows
   the wrapper `0x1825BCEEC` (one frame up the stack, verified unique
   Tier-2 in [`_phase6_final_verify.txt`](_phase6_final_verify.txt) line 26)
   actually completes deserialization before calling `LoadGame`, then
   `kPreLoadGame` should instead hook inside the orchestrator at
   `0x180FBEE78` *before* its call to the wrapper. The current pick assumes
   the simpler interpretation; verify with a debug-mode print on first fire
   that the savegame state hasn't yet been applied to the player entity.

2. **Filename / load-context extraction path.** The Phase 6 design spec
   wants the savegame filename (or equivalent identifier) plumbed into
   `Message::data` for plugin consumption. `ISaveGame` / `ILoadGame` ship
   with a `GetFileName()` vtable method (RTTI presence confirmed at
   [`_phase6_rtti.txt`](_phase6_rtti.txt) lines 74-75); call it on the
   `rdx` argument that the engine passes to `SaveGame`/`LoadGame`. The
   exact vtable offset needs a debug-print on first fire — Ghidra-only
   guesses are not safe enough to commit. Also worth confirming whether
   `this->some_field` on the `C_SaveGameManager` instance carries a
   direct char-pointer that's safer to read than chasing a vtable.

3. **Multi-fire per save/load operation.** Static analysis says one fire per
   operation (one caller per function in the per-function caller-chain
   walk; see [`_phase6_callers.txt`](_phase6_callers.txt) lines 3-31).
   Live confirm: the plugin's `OnSaveGame` callback should not double-fire
   when the user spams F5. If it does, the hook is on a re-entrant path and
   we need a higher-up choke point.

4. **Main-thread confirmation.** Per workspace hard rule #16, add a
   debug-mode `assert(GetCurrentThreadId() == g_main_thread_id)` at the top
   of each of the four hook bodies. Static evidence supports this; the
   assert exists to catch the (unlikely) case where one of the four
   functions is genuinely called from a background worker we missed in the
   call-graph walk.

## Forward-looking — RTTI hierarchy for Phase 6+ extension hooks

The RTTI scan at [`_phase6_rtti.txt`](_phase6_rtti.txt) lines 79-90
surfaced five `wh::conceptmodule` load-completion phase enums that look like
finer-grained extension hooks the engine itself uses to coordinate module
load/save handlers:

- `E_PartialGameLoadFinishedPhase`
- `E_GameLoadFinishedPhase`
- `E_AfterGameLoadDeserializationPhase`
- `E_EntityModuleOnPostLoadGamePhase`
- `E_OnSaveGame`

These look like the engine's own phased-callback system for letting
`wh::conceptmodule` / `wh::entitymodule` subsystems register save/load
participation. **Not needed for v0.1** — `kPostLoadGame` is single-phase by
design in SKSE — but worth documenting in case we want finer-grained
post-load dispatch later (e.g. an `OnAfterGameLoadDeserialization` message
for plugins that need to act *between* file-load and entity-rehydrate).
Tracing the dispatchers that fire these enums would be a future Phase 6+
research task; the RTTI dump and helper scripts here are sufficient to
restart that work without re-disassembling.

## Helper scripts (all in `_research/`)

- [`phase6_dump_string_anchors.py`](phase6_dump_string_anchors.py) —
  NUL-bounded + loose-fit string scan for save/load anchors plus the
  muyuanjin gEnv anchor
- [`phase6_grep_strings.py`](phase6_grep_strings.py) — regex grep over
  all C strings in the binary; used to chase variants of "save", "load",
  "delete" that the bounded scan missed
- [`phase6_dump_strings_region.py`](phase6_dump_strings_region.py) —
  dump all C strings in a VA range; used to enumerate the contents of the
  `.rdata 0x183E16828..0x183E16E00` block where SaveGameManager's format
  strings cluster
- [`phase6_rtti_and_class_anchors.py`](phase6_rtti_and_class_anchors.py)
  — RTTI type-descriptor scan with detailed-context windows; produced
  [`_phase6_rtti.txt`](_phase6_rtti.txt)
- [`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) —
  LEA xref → walk back to function entry → emit 24/40B sig + uniqueness
  check; produces the `_phase6_xrefs_pass{1,2,3}.txt` and
  `_phase6_xrefs_delete.txt` outputs
- [`phase6_find_callers.py`](phase6_find_callers.py) — `E8 imm32` direct
  CALL target scan → walk back to caller entry; produces
  [`_phase6_callers.txt`](_phase6_callers.txt) and
  [`_phase6_callers2.txt`](_phase6_callers2.txt)
- [`phase6_dump_function_calls.py`](phase6_dump_function_calls.py) —
  single-bb walk of a function, dump every call with uniqueness; used for
  thread-context evidence and to confirm SaveGame's tail-call structure
- [`phase6_verify_existing_sigs.py`](phase6_verify_existing_sigs.py) —
  final uniqueness pass over predecessor + Phase 6 sigs (all PASS); produces
  [`_phase6_final_verify.txt`](_phase6_final_verify.txt)

All scripts are pure Python (capstone + pefile) and re-runnable against any
future game build. Re-run order on a fresh KCD2 patch: predecessor verify
first (`phase6_verify_existing_sigs.py`) — if any of yobson1's or
muyuanjin's sigs broke, the rest of the methodology needs an upstream
refresh too. If they still PASS, the four Phase 6 sigs need re-scanning
against the new `WHGame.dll` via
[`phase6_find_xref_functions.py`](phase6_find_xref_functions.py) on the
same anchor strings; the strings tend to survive across point releases
(Warhorse uses them in log calls so they're locked).
