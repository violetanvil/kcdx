# Init cycle ownership — kcdx owns the full game init

**Status:** PRE-RESEARCH. Design approved 2026-05-27. No code lands until step 1
(research) returns and consult round 2 settles the open questions.

## The problem

kcdx and the game currently initialize in **parallel** with an **implicit timing
race** as the only synchronization. Specifically:

- The launcher (`src/loader/main.cpp:374`) creates KingdomCome.exe with
  `CREATE_SUSPENDED`, injects kcdx.dll via `CreateRemoteThread(LoadLibraryW)`,
  then `ResumeThread`s the game's main thread immediately after injection
  reports success.
- kcdx.dll's `DllMain` (`src/dllmain.cpp:376-385`) runs `before_game` work
  synchronously under the loader lock, then spawns a worker thread and
  returns. `ResumeThread` is now free to run.
- The worker thread (`src/dllmain.cpp:84+`) blocks on `WaitForGameDll`, then
  proceeds linearly: version detect → `hooks::Install` → install the
  `ModManager_Select` detour (Address Library id 3100) →
  register deferred-apply handlers → `DiscoverAndLoad` plugins → save/load
  hooks → serialization.
- The game's main thread, meanwhile, runs WHGame.dll init, enters
  `CSystem::Init`, and calls `ModManager_Select` "within a second or two" of
  WHGame mapping (`src/dllmain.cpp:172-196`).
- The **race**: the SELECT detour must be installed BEFORE the game's main
  thread fires it. Today's mitigation is execution-order on the worker thread
  (detour install at line 196 precedes `DiscoverAndLoad` at line 217). This
  works on a fast machine; on a slow machine, or one where
  `MinHook::CreateHook` takes longer, the original SELECT can run before the
  detour is live and the takeover silently never fires.

This is documented in
`docs/known-issues/step-1.5-init-reorder-broke-absorb-detour-race.md` and
appears in the worker thread's own comment block. The current design accepts a
nondeterministic timing window; that is unacceptable as a shipped UX.

## The direction — kcdx owns the cycle

**Approved 2026-05-27.** kcdx OWNS the full game initialization sequence. The
vanilla mod loader is FULLY ABSORBED — it effectively does not exist anymore.
The game's main thread WAITS until kcdx is ready before any kcdx-relevant
init step (the mod load) runs. Parallel work on kcdx's side is fine where
safe; the bright line is that the game thread does not advance past the
sentinel until kcdx returns.

## The architecture

### One sentinel hook, on the game's main thread

Hook the **immediate caller** of `ModManager_Select` (one level up in the
call graph, inside `CSystem::Init`). Inside that hook callback — which fires
ON THE GAME'S MAIN THREAD — synchronously:

1. Finish any pending pre-mod-load work (parsed manifests not yet applied,
   bytes patches queued for the after-WHGame zone, etc.).
2. Run `DiscoverAndLoad` to discover + load every kcdx plugin DLL.
3. Apply the SELECT-time absorption (the rebuilt enabled-list) inline.
4. Return control to the game.

Because the install AND the fire happen on the same thread (the worker thread
installs the hook in advance, but the work done inside the hook is on the
game's main thread), there is no cross-thread race. The game's main thread
literally cannot proceed past the sentinel until our work returns.

### Worker thread becomes thin

The worker thread's job collapses to:

1. Block on `WaitForGameDll` (existing flow).
2. Version detect (existing flow).
3. `hooks::Install` (MinHook init + the engine's own hooks).
4. Install the sentinel hook on the immediate caller of id 3100.
5. Signal "kcdx armed" (mechanism TBD — likely just a return; the launcher
   does not wait, see below).
6. Exit.

No more `DiscoverAndLoad` on the worker thread. No more SELECT-detour install
on the worker thread (the absorption logic moves into the sentinel callback).

### Launcher stays simple

The launcher does NOT need a new gate. The sentinel hook is the gate — the
game's main thread blocks naturally inside the callback. The launcher's
`ResumeThread` returns to its current role: "kcdx.dll has been loaded; let
WHGame.dll start mapping." The synchronization moves from the launcher
(impossible — WHGame isn't mapped yet under suspend) to the sentinel (correct
— game thread is right there, asking us the question).

### Vanilla mod loader: fully absorbed

User's direction: the vanilla loader effectively does not exist. Today's
implementation lets the original SELECT run first (it builds native records
+ runs per-mod validation) then wholesale-replaces the enabled-list vector
with kcdx's rebuilt list. **Open question — see "Decisions" below:** does the
new architecture call the original at all, or does kcdx synthesize every
record itself? RE will surface what the original does that we still need
(record layout, validation passes); the call-or-not question lands at consult
round 2 with those facts in hand.

## Sequence to land this

### Step 1 — `/research-disassembly` (BEFORE any code)

Resolve the **immediate caller of `ModManager_Select`** (id 3100) inside
`CSystem::Init`. Deliverables:

- The caller's address, verified ABI (arg count, types, calling convention,
  `this`-handling if any).
- A new Address Library row for the caller (next free ID, append-only).
- A summary of what the original `ModManager_Select` actually does — which
  passes are validation, which build native records, which we currently rely
  on the original to perform before we wholesale-replace. This informs the
  "call-original-or-fully-synthesize" decision.
- A note on whether the caller is a single deterministic site or whether
  `ModManager_Select` is invoked from multiple places (if multiple, the
  bracket choice changes).

### Step 2 — `/senior-architect-consult` round 2

With RE facts in hand, settle the open questions:

1. **`before_game` zone semantics.** Three options surfaced in the original
   consult: (a) keep the plan's DllMain-to-DllMain meaning, call the new
   bracket something else; (b) widen `before_game` to "before the engine
   asks us to load mods" (the new sentinel becomes the zone boundary);
   (c) split into `before_whgame` + `before_engine_init`. Lean = (b).
2. **Call original SELECT or fully synthesize.** Decided post-RE per above.
3. **Concrete bracket site.** Confirm the immediate caller is the right
   site, or pick a tighter / wider bracket based on what RE surfaces.

### Step 3 — `/feature`

Implementation, ordered:

1. Worker thread restructure: remove `DiscoverAndLoad` + SELECT-detour install
   from the worker thread's body.
2. Sentinel hook install (the caller of id 3100): worker thread installs it
   after `hooks::Install` and before returning.
3. Sentinel callback (game main thread): synchronous load + apply +
   absorption logic moves here. The current `select_detour.cpp` absorption
   path becomes a helper called from inside the new callback.
4. Test plugin: a new `cap-NN` row exercising "the sentinel hook fired and
   kcdx's load completed before the game observed mods." Boot-only check
   via existing dev-mode aggregator.
5. Matrix row + doc entry. Doc entry goes under `docs/lua/` (if it surfaces
   to authors as a new event) and/or `docs/init.md` (the operational doc
   for init order).

## What's PARALLEL vs SERIAL

**Parallel (safe):** anything kcdx does that the game thread does not yet
need:
- Manifest parsing.
- Conflict-engine preflight (read-only analysis of the queued footprints).
- Logging subsystem init.
- Pak-mod registry construction (it walks disk, not game state).

**Serial (linear inside the sentinel callback, game main thread):**
- `DiscoverAndLoad` (loads plugin DLLs, fires Plugin_Preload + Plugin_Load).
- All `kcdx*Interface` work plugins do at Load time (queues a hook, registers
  a command, etc.).
- The wholesale absorption / synthesis of the enabled-list.
- The game thread's read of the result.

The worker thread continues to do the parallel-safe work; the sentinel
callback drains everything that must be visible to the game by the time it
proceeds past the bracket. The game thread waits inside the callback exactly
as long as necessary — same wall-clock as today (~2s on a populated tree),
but deterministic rather than raced.

## Rule + cornerstone anchors

- **UX cornerstone** (`.claude/rules/cornerstones.md`): an environmental,
  silent, machine-speed-dependent failure mode is the worst kind of UX
  defect. Deterministic sequencing fixes it.
- **Results-driven** (`.claude/rules/results-driven.md`): "install order is
  fast enough" is a theory dependent on hardware. The sentinel-hook
  architecture removes the theory entirely.
- **Reverse-engineering** (`.claude/rules/reverse-engineering.md`): a new
  game-function (the immediate caller of id 3100) is a fact to resolve via
  the reuse ladder, not Ghidra-first. Hence step 1.
- **Restructure plan** (`docs/outstanding-work/restructure-plan.md`): the
  zone model adapts to the new sentinel; the wider question of
  `before_game` semantics is settled at consult round 2.

## Decisions settled (consult round 2, 2026-05-27)

1. **Bracket site:** `ModManager_ctor`, resolved via its `kcdx_id` in the
   in-flight reference DB (the seed.csv row 3101 is the cache row for that
   same entity; the resolution path at runtime is by kcdx_id, not the
   seed.csv numeric id). No new entry needed — the bracket function is
   already a known entity.
2. **kcdx OWNS discovery end-to-end.** kcdx does NOT call the original
   ctor or the original SELECT. The original SELECT was vestigial in the
   absorb path — kcdx's `record_synth::BuildRecord` already synthesizes
   every I_Mod record with correct CryString headers, vtables (ids
   3105/3106), and string fields. The ONE thing the original SELECT
   currently provides that kcdx's `pak_mod_registry` does not is the
   Steam-Workshop scan. **Resolution: extend `pak_mod_registry` to walk
   Steam Workshop content** (`steamapps/workshop/content/1771300/`) so
   kcdx is the full discovery layer. After this, the original SELECT is
   never invoked — kcdx fully replaces it.
3. **Parallel by default, single wait point at the ctor hook.** Worker
   thread runs `DiscoverAndLoad` + plugin Load + record synthesis in
   parallel with WHGame's own DllMain + early CSystem::Init. The ctor
   hook is the SINGLE synchronization point: when the game's main thread
   reaches `ModManager_ctor`, the hook callback waits on a signal from
   the worker. Race-free because the wait is explicit. The hook callback
   does NOT call the original ctor — it: (i) waits for kcdx's worker to
   signal "ready," (ii) writes the kcdx-synthesized enabled list into
   the C_ModManager object, (iii) returns. The game thread continues
   into MOUNT with kcdx's records in place.
4. **`before_game` semantics:** doc-only fix. The mechanism in
   `src/ldr_notify.cpp` already applies `before_game` patches to any
   mapped DLL via `ApplyEntriesForModule`. The WHGame-specific framing in
   `docs/outstanding-work/restructure-plan.md` + `docs/load-order.md` was
   a documentation defect from when the mechanism's scope was narrower.
   Docs catch up to code.
5. **New bracket is engine-internal**, not a new author-facing zone.
   Authors keep declaring `zone = "before_game"` (LDR-window, any DLL) or
   `zone = "after_game"` (default). The bracket gets a new `InitPhase`
   enum entry in `src/init_phase.h` (name TBD at feature-build time —
   likely `ModManagerCtorBracket` or similar). No TOML schema change.

## The architecture (final, post-round-2)

### Threads + sync

```
LAUNCHER (kcdx.exe)
  ├─ CreateProcess(KingdomCome.exe, CREATE_SUSPENDED)
  ├─ Inject kcdx.dll via CreateRemoteThread(LoadLibraryW)
  └─ ResumeThread(game main thread)

KCDX.DLL DllMain (game main thread, loader lock)
  └─ RunBeforeGameZoneInDllMain (synchronous):
       paths/log init, manifest parse, load_order resolve,
       before_game patches against already-mapped modules,
       LDR notification register
  └─ CreateThread(WorkerThread); DllMain returns

WORKER THREAD (parallel with game's own init from here)
  ├─ WaitForGameDll (until WHGame.dll mapped)
  ├─ hooks::Install (MinHook init + engine's own hooks)
  ├─ InstallCtorBracket (MinHook detour on ModManager_ctor's kcdx_id)
  ├─ DiscoverAndLoad (plugin DLLs, Plugin_Preload, Plugin_Load)
  ├─ pak_mod_registry: walk mods/ + kcdx-plugins/ + Steam Workshop
  ├─ Build kcdx's resolved enabled list (record_synth per record)
  └─ SetEvent(g_kcdxReadyEvent)  ← signal "kcdx is ready"

GAME MAIN THREAD (running CSystem::Init in parallel with worker)
  └─ ... (lots of engine init) ...
  └─ reaches ModManager_ctor call
       └─ CTOR HOOK CALLBACK:
            ├─ WaitForSingleObject(g_kcdxReadyEvent, INFINITE)
            ├─ Construct C_ModManager (allocate, set vtables, register
            │  wh_mod_GenerateReport console cmd, zero the lists)
            ├─ Write kcdx's synthesized enabled list into self+0x30..0x40
            └─ Return self (the constructed C_ModManager)
  └─ continues into MOUNT, which walks kcdx's list
```

### What changes vs today

- **Today's `src/mod_absorb/select_detour.cpp` is REPLACED by a ctor
  hook.** SELECT is no longer detoured — the ctor is. The
  `record_synth` + `enabled_list_builder` machinery stays (kcdx's own
  record synthesis was always correct; only the orchestration moves).
- **Worker thread loses `InstallSelectDetour`** and gains
  `InstallCtorBracket`. The `DiscoverAndLoad` call stays on the worker
  (parallel by default per round-2 decision 3) — the ctor hook is the
  ONE wait point, not the place plugin loading runs.
- **`pak_mod_registry` gains Steam Workshop discovery.** New code path
  walking `steamapps/workshop/content/1771300/`; same record-synthesis
  pipeline applies to every Workshop mod.
- **`g_kcdxReadyEvent`** — new Win32 manual-reset event, signaled by
  worker after enabled-list construction completes, waited on inside
  the ctor hook callback.
- **The original ctor + SELECT are never invoked.** kcdx replaces both.

### What does NOT change

- Launcher's CREATE_SUSPENDED + inject + ResumeThread flow.
- `RunBeforeGameZoneInDllMain` (synchronous, under loader lock) — its
  job (parse manifests, resolve load order, apply already-mapped before_game
  patches, register LDR notify) stays as-is.
- LDR-notify mechanism — `before_game` patches already apply to any
  mapped DLL.
- Plugin interface ABIs.
- `record_synth` + `enabled_list_builder` — only the call site moves
  (worker → ctor hook).

## Sequence to land (post-round-2)

1. ~~`/research-disassembly`~~ DONE 2026-05-27.
2. ~~`/senior-architect-consult` round 2~~ DONE 2026-05-27 — decisions
   above.
3. (Optional) `/research-disassembly` for the Steam Workshop scanner —
   IF kcdx's own Workshop walk is simpler than RE'ing the engine's, this
   sub-step may not be needed. Settle at `/feature` start.
4. `/feature` — implement, ordered:
   - Worker thread restructure (remove SELECT detour install).
   - `pak_mod_registry` Workshop walk.
   - `InstallCtorBracket` + ctor hook callback + `g_kcdxReadyEvent`.
   - Move enabled-list build to worker; signal event when done.
   - Doc updates (restructure-plan.md, load-order.md) for the
     `before_game` widening.
   - New `InitPhase` enum entry for the bracket.
   - Test plugin: bracket fired, kcdx's list installed, vanilla SELECT
     never ran. Matrix row.

## RE findings (research-disassembly, 2026-05-27 — pre-build, all tier-1/2)

**Bracket site: `ModManager_ctor` (Address Library id 3101, RVA 0x00DA0EB0).**

- Single-caller chain confirmed: `CSystem::Init` (`FUN_1807a6c64`, RVA
  0x007A6C64) → call at `0x1807A76FE` → `ModManager_ctor` (id 3101) → tail
  call to `ModManager_Select` (id 3100). One deterministic site each. No
  reload-mods re-entry path.
- ABI verified (id 3101 seed prose, capstone E8-sweep against binary):
  `ptr (ptr outResult /*rcx*/, ptr sys /*rdx*/, ptr modsDir /*r8*/)` —
  3-arg `__fastcall`, returns the constructed C_ModManager pointer.
- Bracket cleanliness: the ctor is 222 bytes — a thin wrapper around
  "allocate → init lists → register `wh_mod_GenerateReport` console cmd →
  call SELECT → return." Hooking the ctor brackets the whole C_ModManager
  construction; nothing else is mixed in. `CSystem::Init` itself is 7611
  bytes of mostly-unrelated WHGame subsystem init that kcdx plugins do not
  yet care about (the existing `InputLoaded` event covers gEnv-up).
- **No new Address Library row needed for the bracket itself.** Id 3101 is
  the hook target.
- Next free ID in the 3100 mod-loader band: **3107** (if a higher
  `CSystem::Init` waypoint is later wanted as a stricter sentinel — defer
  claiming until round 2 decides). Should land via the Phase-9.0 DB
  unification path, not direct seed.csv edit.

**Original ctor / SELECT passes — load-bearing for free.** The original
SELECT runs: list-clear → Workshop scan → local `mods/` alphabetical scan →
`ModManager_ParseManifest` (id 3104) per-mod manifest parse + version-gate
→ `ModManager_ReadModOrder` (id 3103) mod_order.txt read → fallback
"enable all scanned." The manifest parse is the engine's own parser doing
the I_Mod string-field population (path, id, name, description, author,
version, date) with **real CryString headers + real vtables**. The
keystone crash class kcdx already burned cycles avoiding
(`project_kcdx_crystringt_record_fields` — garbage `nLength` → multi-GB
alloc + CryFatalError during MOUNT) is exactly what re-implementing the
manifest parse from scratch would reintroduce.

**RE recommendation for "kcdx owns the loader":** CALL the original ctor +
SELECT, then wholesale-replace the enabled-list vector with kcdx's
resolved order before the ctor's hook returns. This is the natural
extension of today's "let original SELECT run, then replace" pattern, just
one level up at the ctor. "kcdx owns the loader" = kcdx OWNS WHICH mods
load and in what ORDER + kcdx surfaces the loader concept to authors —
**not** "kcdx re-implements every CryEngine SELECT pass from scratch."
The vanilla mod loader's PUBLIC role disappears (authors talk to kcdx);
its INTERNAL implementation passes stay because they own the
crash-avoiding engine-correct record layout.

**Side effects preserved by letting the ctor run:** the console command
`wh_mod_GenerateReport` registration (skipping the ctor drops it).

**Confidence:** HIGH on all primary deliverables. All facts via tier 1
(seed.csv verified rows 3100–3106) + tier 2 (`docs/mod-loader-absorb.md`,
which cites live capstone E8-sweep + two-boot live-binary verification).
No fresh Ghidra needed; no new `_research/` artifact produced.

**Round-2 surface shrinks to:**

1. `before_game` semantics: (a)/(b)/(c), lean (b) anchored on
   "before `ModManager_ctor` fires."
2. Confirm bracket at id 3101 (vs a stricter id-3107-style earlier
   `CSystem::Init` waypoint — lean no).
3. Confirm call-original (RE lean: yes — call original ctor + SELECT,
   then replace enabled-list).
4. Confirm synchronous `DiscoverAndLoad` on the game main thread inside
   the ctor hook callback (~2s on a populated tree — same wall-clock as
   today, deterministic instead of raced).

## What does NOT change

- The launcher's CREATE_SUSPENDED + inject + ResumeThread flow.
- The worker thread's `WaitForGameDll` + version-detect + `hooks::Install`
  sequence.
- The plugin interface ABIs (`kcdx*Interface`).
- The existing `mod_absorb::record_synth` machinery — it stays; only WHERE
  it runs (worker thread vs sentinel callback) moves.
