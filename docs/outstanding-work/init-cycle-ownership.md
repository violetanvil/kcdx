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
   - ~~Worker thread restructure (remove SELECT detour install).~~ DONE step 4.
   - `pak_mod_registry` Workshop walk. (separate follow-up; not in step 4.)
   - ~~`InstallCtorBracket` + ctor hook callback + `g_kcdxReadyEvent`.~~ DONE step 4.
   - ~~Move enabled-list build to worker; signal event when done.~~ DONE earlier; preserved.
   - Doc updates (restructure-plan.md, load-order.md) for the
     `before_game` widening. (deferred per round-2 decision 4.)
   - ~~New `InitPhase` enum entry for the bracket.~~ DONE step 4
     (`ModLoaderTakeoverArmed` renamed to `CtorBracketInstalled`).
   - ~~Test plugin: bracket fired, kcdx's list installed, vanilla SELECT
     never ran. Matrix row.~~ DONE step 4 (cap-61).

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

## Probe findings — comprehensive ctor probe (commit `87a2a38`, 2026-05-27)

The three-point probe ran twice (no-mods + with-mods boots) and resolved
every uncertainty the narrow step-1 probe had surfaced. Full analysis in
[`_research/init-cycle-recon/FINDINGS.md`](../../_research/init-cycle-recon/FINDINGS.md);
this section captures the architecture-relevant pieces.

### The C_ModManager layout (0x68 bytes) — confirmed

| Offset | Width | Field | Notes |
|---|---|---|---|
| +0x00 | 8 | C_ModManager vtable | Static address (image RVA `0x3AA2E60`); 8/8 code pointers |
| +0x08 | 8 | `CSystem* sys` (ctor arg2) | — |
| +0x10 | 8 | `CryStringT modsDir` in-place | Content always "mods"; header `{pad=0, nRefs=1, nLength=4, nAllocSize=4}` |
| +0x18..+0x28 | 3×8 | scanned-list `std::vector<I_Mod>` (0x70-stride INLINE records, NOT pointers) | begin / end / end_of_storage |
| +0x30..+0x40 | 3×8 | enabled-list `std::vector<I_Mod*>` (8-byte stride) | begin / end / end_of_storage |
| +0x48 / +0x50 / +0x58 | 3×8 | **unused — zero in both boots** | — |
| +0x60 | 1 | initialized flag (byte=1) | Upper 7 bytes zero |

The two vector triples have **different element strides**: the enabled
list is pointers (8 bytes each), the scanned list is inline records
(0x70 bytes each). Both first-element vtables match Address Library id
3105 (`ImodVtable_primary`).

**MOUNT iterates only the enabled list** (per seed row 3102: *"iterates
the ENABLED wh::I_Mod list (modMgr+0x30)"*). The scanned list is an
intermediate working set for SELECT's mod_order.txt + manifest-parse
passes; nothing downstream of SELECT reads it.

### POINT B vs POINT C — separating ctor writes from SELECT writes

At POINT B (SELECT entry — post-ctor-zero-init, pre-SELECT-body), only
four slots are non-zero in both boots: `+0x00` (vtable), `+0x08` (sys),
`+0x10` (modsDir CryString), `+0x60` (init flag).

At POINT C (ctor return — SELECT has run inline), six more slots become
non-zero: the scanned-list triple (`+0x18`/`+0x20`/`+0x28`) and the
enabled-list triple (`+0x30`/`+0x38`/`+0x40`).

**This cleanly separates the ctor's responsibility from SELECT's.** The
kcdx-owned replacement ctor in the implementation step writes only
`+0x00`/`+0x08`/`+0x10`/`+0x60`; its caller (kcdx's own enabled-list
builder, running on the worker thread) populates `+0x30`/`+0x38`/`+0x40`
directly. The scanned list is left empty — MOUNT does not iterate it.

### "kcdx owns the loader" — REVISED in light of probe findings

The earlier RE finding lean was *"CALL the original ctor + SELECT, then
wholesale-replace the enabled-list vector"*. The user overruled this in
consult round 2: *"don't call the original, we do ours; we can call what
the original calls if needed, but we do it through our loop."*

The probe findings show this is fully viable:

- The ctor's writes are 10 fields (4 ctor-direct + 6 from SELECT's
  inline call). Kcdx can replicate all 10 directly using existing
  machinery (`record_synth` already builds I_Mod records with correct
  CryString headers, vtables ids 3105/3106, the absorb pattern).
- The scanned list can be left empty (MOUNT does not iterate it).
- The console command `wh_mod_GenerateReport` registration is a one-line
  call kcdx can replicate (#5 in the follow-ups below) or explicitly
  drop (it's a developer command, not a user-facing feature).
- The three unused slots (`+0x48`/`+0x50`/`+0x58`) confirmed-zero in
  both boots — kcdx leaves them zero.

### The previous narrow probe was reading the wrong memory

The narrow step-1 probe (commit `bf21802`) read `outResult` directly,
which is the caller's stack slot, not the C_ModManager itself. The ctor
stores into `*outResult` last (`mov [rsi], rbx`), so the object lives at
`*outResult`. The "+0x30 = 'mods' ASCII" / "+0x48 = 15" / "+0x50 = 54"
findings the narrow probe reported were ALL CSystem::Init's local stack
data (its temporary CryString scratch for building the modsDir arg). The
comprehensive probe corrects this and walks the actual heap object.

### Address Library rows the implementation step needs

These rows are needed before the kcdx-owned bracket can land. None of
them existed before the probe; none block step 2 (Workshop walk in
`pak_mod_registry`).

| Function | RVA | Purpose |
|---|---|---|
| C_ModManager vtable | `0x03AA2E60` | Write into `+0x00` of the synthesized object |
| WHGame allocator (`FUN_1804f7820`) | `0x004F7820` | Allocate the 0x68 block; dtor's free must match |
| CryString placement-construct (`FUN_1804fd468`) | `0x004FD468` | Build the in-place CryString at `+0x10` |
| CryString init-from-string (`FUN_1804f692c`) | `0x004F692C` | Init the stack-local CryString from the modsDir arg (already named `tmp_name_init` in phase7-recon work) |
| Console-cmd register (`FUN_180B99098`) | `0x00B99098` | Register `wh_mod_GenerateReport` (optional — drop if we don't want the dev command) |

Per [[project-kcdx-phase9-db-addrlib-unification]], these land via the
DB unification path (the in-flight three-track work), not direct
seed.csv edits. Surface to the user when step 4 needs them.

### Open follow-up — the C_ModManager destructor

Not analyzed yet. Will be needed at game shutdown / save-reload — kcdx's
replacement ctor must allocate via WHGame's allocator (`FUN_1804f7820`,
above) so the destructor's matching `free` call lines up. Add to the
RE worklist before step 4.

## Status (post-step-4)

Step 4 LANDED: the kcdx-owned ctor bracket
(`src/mod_absorb/ctor_bracket.{h,cpp}`, `InstallCtorBracket`) replaces the
prior SELECT detour in full. The native `ModManager_ctor` and the native
SELECT it tail-calls are NEVER invoked; kcdx allocates the 0x68-byte
C_ModManager via WHGame's own allocator (refdb name `WHGame_allocator`),
writes the vtable at +0x00 (refdb name `C_ModManager_vtable`), the
`CSystem* sys` at +0x08, the in-place modsDir CryString at +0x10 (built
via refdb names `CryString_init_from_string` + `CryString_placement_construct`),
the enabled-list vector triple at +0x30/+0x38/+0x40 pointing at the
kcdx-owned process-lifetime `std::vector<void*>` (built earlier on the
worker thread by `BuildEnabledListOnWorker`, with a `WaitForSingleObject`
on the manual-reset readiness event), the init flag at +0x60, and leaves
the scanned-list slots (+0x18/+0x20/+0x28) and unused slots
(+0x48/+0x50/+0x58) zero. The 0x68 block is memset-zeroed before the
field writes so the allocator's contract (uninitialized) does not leak
garbage into the unused slots.

Surface deltas in this step:

- CREATED: `src/mod_absorb/ctor_bracket.{h,cpp}`.
- CREATED: `test-plugins/cap-61-init-cycle-ownership/`.
- DELETED: `src/mod_absorb/ctor_probe.{h,cpp}` (the transient
  observation probe — its question is answered, comprehensive findings
  preserved in `_research/init-cycle-recon/FINDINGS.md`).
- MODIFIED: `src/mod_absorb/select_detour.{h,cpp}` — the
  MinHook detour on `ModManager_Select` and its `HookedSelect`
  callback are gone; the worker-side build + signal machinery
  (`CreateReadyEvent`, `BuildEnabledListOnWorker`, the storage for
  the enabled list + diagnostic entries + readiness event handle)
  survives, with two new accessors (`GetReadyEventHandle`,
  `GetEnabledListData`) that the bracket reads on the game thread.
- MODIFIED: `src/dllmain.cpp` — `InstallSelectDetour()` and the
  `ctor_probe::Install()` ride-along call are gone; the bracket
  install slots into the same worker-thread position the SELECT
  detour install held (right after `EngineHooksInstalled`,
  immediately after `CreateReadyEvent`, before
  `BuildEnabledListOnWorker`).
- MODIFIED: `src/init_phase.{h,cpp}` — `ModLoaderTakeoverArmed`
  renamed to `CtorBracketInstalled` (same ordinal position, same
  advance site in dllmain).
- MODIFIED: `CMakeLists.txt` — drops `ctor_probe.cpp`, adds
  `ctor_bracket.cpp`.
- MODIFIED: `docs/init.md`, `docs/mod-loader-absorb.md` — prescriptive
  references updated for the bracket.

The Steam Workshop walk in `pak_mod_registry` (round-2 decision 2) is a
SEPARATE follow-up cycle, not in step 4.

## Post-step-4 follow-ups — surfaced by live-launch verification

Step 4 landed mechanically (bracket installs, records synthesize, MOUNT
walks kcdx's list verbatim), but the larger plan's terminal goal — **boot
to main menu** — is NOT yet met. Two crashes surfaced in live boots after
step 4 deployed, in this order:

### Crash #1 — I_Mod-vtable-null AV at `WHGame+0x244D085` (CLOSED)

**Symptom.** First live boot post-step-4 AV'd at `WHGame+0x244D085` reading
`[rcx+0x60]` from a synthesized I_Mod record whose vtable at +0x00 was 0.
`rcx` carried instruction bytes (`6c894808245c8948`) instead of a pointer
— a NULL-vtable dispatch the engine took into garbage.

**Root cause.** `src/mod_absorb/record_synth.cpp::BuildRecord` resolved
the I_Mod concrete-class vtables by the **legacy seed IDs 3105 / 3106**
(`address_library::Resolve(3105)`, `Resolve(3106)`). The flattened DB
schema re-keyed those entities to kcdx_ids 138 / 139; the static
`kEntries[]` mirror in `address_library.cpp` was the only place the legacy
IDs still resolved. After the seed flatten that table was a stale mirror
and `Resolve(3105/3106)` returned 0 → record built with NULL vtable at
+0x00 → MOUNT's first virtual dispatch AV'd at the recorded RVA.

**Fix.** Commit `498934c` (refdb-owns-the-cache refactor). `refdb::Open()`
bulk-builds the curated cache once at boot; the engine-internal call sites
in `record_synth.cpp` / `record_synth_selftest.cpp` / `record_validate.cpp`
moved to `refdb::ResolveAddrByName("ImodVtable_primary")` and
`ResolveAddrByName("ImodVtable_subobject")`. The static `kEntries[]` table
was deleted; address_library shrank to the plugin-precedence / alias /
author-target / validation surface. Live verification on 2026-05-28
(`kcdx-dev_2026-05-28_15-27-10.log`) shows
`[REFDB] cache_built name_count=143`, `resolve_hit input_name="ImodVtable_primary"
kcdx_id=138 rva=…`, `ctor_bracket_complete obj=… enabled_n=79`, and no
`[GUARD] FAULTED` at `module_rva=38014085`. The vtable-null AV is gone by
construction. Full refactor scope + 9.7-phase consumer plan in
[restructure-plan.md §Phase 9.7](restructure-plan.md).

### Crash #2 — `ModManager_ParseManifest` AV at `WHGame+0x243FC85` (OPEN — next-cycle target)

**Symptom.** Second live boot post-step-4 (with the crash-#1 fix
deployed) reached the menu init farther than any prior boot and AV'd at
`WHGame+0x243FC85`. BugSplat's UnhandledExceptionFilter caught it before
kcdx's `[GUARD] FAULTED` SEH could fire — watchdog records *"no
kcdx-side dmp … SEH handler didn't run — crash class probably bypassed
it: fast-fail, kernel kill, etc."*; BugSplat report MFA reads
`WHGame!0243fc85`.

**RVA → function.** `0x243FC85 - 0x243E7B8 = 0x14CD` → **+0x14CD inside
`ModManager_ParseManifest`** (seed kcdx_id 137, RVA `0x243E7B8`). Deep
inside the body, not the prologue — past the early manifest-file open and
into the field-population / version-gate logic.

**Why this surfaced now.** The implemented step-4 bracket fully replaces
ctor + SELECT — `ModManager_ParseManifest` is **never called by the
bracket itself**. Synthesized records carry the strings `record_synth.cpp`
sets (path, id, name, description, author, version, date — all wrapped as
CryStringT with real headers) but do NOT carry the full state
ParseManifest would populate. Something downstream — MOUNT itself, or a
post-MOUNT pass — appears to invoke ParseManifest on a synthesized record
that lacks state ParseManifest expects, and AVs inside it.

**Design tension.** The RE recommendation in
[§RE findings](#re-findings-research-disassembly-2026-05-27--pre-build-all-tier-12)
of THIS doc was *"CALL the original ctor + SELECT, then wholesale-replace
the enabled-list vector before the ctor's hook returns"* — explicitly
to get the engine's own manifest parse + real vtables for free and avoid
the keystone-crash-class kcdx already burned cycles avoiding
(`project_kcdx_crystringt_record_fields` — garbage `nLength` →
multi-GB alloc + CryFatalError during MOUNT). The implemented bracket
chose the opposite path (full ctor + SELECT replacement, records from
scratch). That choice cleared the SELECT-list-append-rejection crash that
the narrow-takeover probe had hit, but it also bypassed the engine's own
manifest parse — and crash #2 is plausibly the consequence.

**Next-cycle scope (debug agent owns).** Three candidate paths, surface
to /senior-architect-consult before implementing:
1. **Populate the missing fields in `record_synth.cpp`** so a synthesized
   record looks indistinguishable from a ParseManifest-populated one (the
   "match the engine's record shape exactly" path; high upfront RE cost,
   no behaviour change to the bracket).
2. **Hook `ModManager_ParseManifest`** to detect kcdx-synthesized records
   and short-circuit (skip the parse, return success); changes the
   contract of an engine function but localises the workaround.
3. **Revisit the round-2 design decision** and CALL the original ctor +
   SELECT, then wholesale-replace the enabled-list vector (the RE
   recommendation as written). The implemented bracket moves from
   "replace the ctor" to "post-process the ctor's output."

The decision belongs to the consult; the debug agent's first job is the
ground-truth probe — which thread calls ParseManifest, with what record
pointer, on what state delta.

### Deferred-from-step-4 (independent of #1 / #2)

- **`pak_mod_registry` Workshop walk** (round-2 decision 2). Kcdx
  currently inherits the engine's Workshop scan via the original SELECT
  pass; with SELECT gone the Workshop side of discovery is unowned. Not
  a blocker for boot-to-menu; ships when the debug agent's choice for
  crash #2 settles whether SELECT runs at all.
- **Doc updates for `before_game` widening** (round-2 decision 4). Doc
  sweep across [restructure-plan.md](restructure-plan.md) +
  [load-order.md](../load-order.md); independent of every above
  follow-up.
