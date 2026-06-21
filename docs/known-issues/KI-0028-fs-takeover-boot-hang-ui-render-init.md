---
id: KI-0028
opened: 2026-06-20
status: open
commit_at_filing: 4befc07
---

# Boot hangs at UI/render bring-up (sound loads, no video, no input) — after KI-0027's table-DB load succeeds

**Status:** open

With the file-system-takeover directory-enumeration triplet live (KI-0027 fixed,
`4befc07`), the boot now passes the table-database load and proceeds — but **HANGS**
at UI/render bring-up: **sound loads, no video ever appears, and the game accepts no
input** (the user had to kill the process via Task Manager — not a crash, a hang; no
crash dump produced). This is a NEW failure the KI-0027 fix unblocked the boot far
enough to reach — the same chain pattern as KI-0026 → KI-0027 (each fix exposes the
next latent boot blocker).

## Relationship to KI-0027

KI-0027 (the table-DB load failing because kcdx's fs-takeover did not serve the
`<base>__*.xml` override-glob directory enumeration) is **FIXED + verified** in this
same run: zero "Database system error" / `err_id=259` in `kcd.log`, the table globs
return correct match counts (`matched=0` for vanilla `__*` overrides, not the
pre-fix 528 whole-directory over-match), and the test suite ran to `passing=320/343`.
KI-0028 is a DISTINCT subsystem — render/UI device bring-up, not filesystem — that the
boot only NOW reaches. The fs-takeover enumeration is functioning correctly; this hang
is downstream of it.

## Symptom

- **User-reported** (the live experience): game launched with the KI-0027-fixed engine
  (`4befc07`): sound loaded, "no video", input unresponsive; the user killed the process
  via Task Manager (a hang, not a crash; no crash dump).
- **Log-corrected** (see §Reframe — the user report was a perception, not ground truth):
  `kcd.log` proves the **main menu DID render** — `PlayVideoOnly 'main_menu_kutnohorsko3'`
  (menu background video), `[MFX] Loading FXLib` (material effects), and `[Pros]` online
  banners downloaded over HTTP (200 OK) and shown. The boot completed to a live, rendering,
  networked main menu, THEN hung — input dead, no further log output on the single boot
  thread. The "no video" was almost certainly "menu up but frozen / no gameplay", not a
  black screen.

## Reframe (2026-06-20 — /debug §B static-log pass; corrects the original premise)

The original Facts were authored from the **kcdx-dev log tail alone** and never read
`kcd.log`. Reading both logs together by timestamp corrects the picture: the menu
rendered, and the cursor serve the original facts blamed actually **succeeded**.

**Corrected boot timeline (single thread, `tid=46452`, both logs merged):**

| Time | Log | Event |
|------|-----|-------|
| 15:58:24–26 | dev | fs-takeover `FindFirst` enum over saves / materialeffects / table `__*` globs — all `matched=N` correct (KI-0027 holds) |
| 15:58:26.472 | dev | `read_entry ... cursor_green.dds` (name **`engineassets/...`**, no alias) served OK → boot continues 24 s more |
| 15:58:46.584 | dev | suite SUMMARY `passing=320 ... pending=21 total=343` |
| 15:58:47.549 | kcd | LAST `kcd.log` line: live menu — banners over HTTP 200, MFX libs, `PlayVideoOnly 'main_menu_kutnohorsko3'` |
| 15:58:50.093 | dev | LAST dev line: `read_entry ... cursor_green.dds` (name **`%engine%/engineassets/...`**, aliased) — same `data_off=79371331`, `usize=4232`, **served OK** |
| (after) | — | nothing further in EITHER log; process hangs; user kills it |

## Facts

- The table-DB load SUCCEEDS — zero "Database system error" / `err_id=259` in
  `kcd.log` this run (the KI-0027 fix holds). (FACT — `kcdx_2026-06-20_15-58-02.log`)
- **The main menu rendered** — `kcd.log` (652 lines, ZERO error/fatal/assert) ends at a
  live menu: `PlayVideoOnly 'main_menu_kutnohorsko3'`, MFX FXLibs loaded, 3 online banners
  downloaded (HTTP 200) and shown. The original "menu never renders" is disproven.
  (FACT — `kcd.log` lines 600–652, `game/kcd.log` in the crash zip)
- **The last cursor serve SUCCEEDED — kcdx did NOT wedge inside the read.** The final dev
  line logs a completed `read_entry` (valid `data_off`, `usize=4232`); PAK_READER logs the
  successful serve, not an attempt. The engine got its bytes and then stopped asking kcdx
  for anything. The hang is in the engine's consumer of those bytes / the single boot
  thread, NOT in the kcdx FS read. (FACT — `kcdx-dev` line 83562 + `src/fs_takeover` read path)
- **The `%engine%/` alias prefix on the last read resolves correctly** — `data_off` is
  identical to the earlier un-aliased serve of the same file, so `ExpandEngineAliasToIndexKey`
  mapped it to the same pak entry. The alias is not failing. (FACT —
  `src/fs_takeover/asset_index.cpp` `ExpandEngineAliasToIndexKey` + the two matching `data_off`)
- **There is no engine crash.** The crash zip at the failing timestamp is the watchdog's
  kill-time snapshot; `crash/bugsplat_F62P7UL5.log` is empty (BOM only). Confirms hang, not
  fault. (FACT — `crash_2026-06-20_15-58-02.zip` contents)
- The `[Warning] Unknown command: kcdx_find WHGame.dll --string "..."` in `kcd.log` is a
  benign red herring — a config/autoexec replay of a leftover console line, not on the hang
  path (`[Pros]` banner activity continues normally after it). (FACT — `kcd.log` line 645 +
  `src/console_commands_find.cpp` registration)
- Everything ran on ONE thread (`tid=46452`). A block on that single thread explains why
  BOTH logs stop simultaneously with no further FS or engine output. (FACT — every dev line
  tagged `tid=46452`)
- The boot reached deep init: trampoline pool, LUA_SHIM passes, FOREIGN_HOOK selftest,
  320 suite tests passing — engine, hooks, Lua VM, FS all up. (FACT — `kcd.log` + dev tail)
- **The P-E run PROGRESSES for ~41s, then the log goes SILENT, then the dump (37s later) shows the
  wedge.** The dev log emits 30 `[TEST] SUMMARY` lines (`HookedUpdate` steady-state ran), first
  `20:29:02.380`, last `20:29:43.475` (`passing=320`); PAK_READER continues to `20:29:43.455`. The
  log then STOPS at `20:29:43.475`. The cdb capture was taken at `20:30:20.935` — 37s AFTER the last
  log line. So the wedge ONSET is `~20:29:43` (where the log goes silent), after a window of real
  progress — NOT at boot start, NOT absent. (FACT — P-E dev log SUMMARY timestamps + capture-file
  mtime `20:30:20.935`)
  - **WITHDRAWN (was an over-read):** "the tick firing proves the engine got past
    `C_Game::CreateInstance`." The update tick runs on a different path; ShaderCompile is still INSIDE
    `CreateInstance` at capture. The tick firing does NOT prove `CreateInstance` returned.
- **At capture (`20:30:20`), three threads are parked in NGX/FSR2/CreateInstance.** RenderThread
  (`b1cc.3144`=tid 12612, cdb-named "RenderThread"): `NtWaitForAlertByThreadId ←
  RtlSleepConditionVariableSRW ← _Cnd_wait ← NVSDK_NGX_UpdateFeature+0x20139e ← ffxFsr2ResourceIsNull`.
  ShaderCompile (`b1cc.b2b0`): `SleepEx ← C_Game::CreateInstance+0x46514`. Main:
  `SleepConditionVariableSRW ← NVSDK_NGX_UpdateFeature`. (FACT — `ki28_pe_allthreads.txt` resolved
  stacks)
- **No `kcd.log` evidence exists for the P-E run's menu state** — the `kcd.log` on disk has mtime
  `20:40` (the P-F swap-OFF run that reached the menu), which OVERWROTE the P-E (`20:28`) engine log.
  Any "the menu rendered" claim for a swap-ON run is from a DIFFERENT run, not P-E. (FACT — `kcd.log`
  mtime `20:40:42` ≠ P-E `20:28`)
- **The FS_BOOT_TRACE (kept diagnostic) recorded 46,762 render-window ops this run** — the
  render thread `tid=12612` is a heavy kcdx-FS caller (19,060 ops: FReadRaw_byPakIndex 8034,
  FSeek 6216, FOpen 1710, FClose 1668, FTell 953); the table/script thread `tid=46280` is the
  other (27,183 ops). how-distribution: index-pak 7434, index-pak-serve 2094, miss-original 1397,
  original 354. (FACT — P-E dev-log `FS_BOOT_TRACE` lines, tid + slot + how tallies)
- **During the render thread's 14.5s stall (`20:29:08.451` → `20:29:22.933`) the table thread
  `tid=46280` is ACTIVELY churning** — thousands of `PAK_READER read_entry` (Scripts.pak /
  IPL_GameData.pak flownodes + entity Lua) plus 47 `[LEGACY] hook_chain: re-entrant dispatch
  depth=2` events. The system is NOT idle-wedged during the slow window; script/table load + the
  legacy hook chain are running concurrently while the render thread waits. (FACT — P-E dev log
  `20:29:20`–`20:29:21` PAK_READER + hook_chain lines, tid=46280)
- **ZERO `double_close` / `bad_handle` errors logged the entire P-E run.** `Close()` logs
  `double_close` on an already-closed slot and `bad_handle` on a bad-tag/out-of-range id; neither
  appears. So the render thread's 884-`FClose handle=3` vs 883-`FOpen→3` count imbalance is NOT a
  stale/double close hitting the kcdx pool (it would have logged) — the recycled-handle-id
  corruption theory is FALSIFIED. (FACT — `grep -c double_close|bad_handle` P-E dev log = 0;
  `src/fs_takeover/file_handle.cpp` `Close` lines 552–588)

## P-A — live thread-stack capture (RAN 2026-06-20, cdb on the hung process)

Attached `cdb -pn KingdomCome.exe` to the live hung process, dumped all 199 thread stacks,
and re-sampled the main thread twice to confirm a true wedge vs. progress.

**Result: the main thread (#0 "Main") is genuinely wedged (two samples byte-identical), in
a stack that passes THROUGH `kcdx!HookedUpdate`:**

```
ntdll!NtDelayExecution -> KERNELBASE!SleepEx+0x91      <- TOP: sleeping (not a lock wait)
WHGame!...+0x36af90
WHGame!wh::game::C_Game::CreateInstance+0x2e8c63
WHGame!wh::game::C_Game::CreateInstance+0x2e8d7d
WHGame!...+0x16cce2   (ret-addr 0x91b42a15 - in kcdx range)
kcdx!kcdx::hooks::`anonymous namespace'::HookedUpdate+0x945   <- OUR per-frame update hook
WHGame!...+0x16c7a0   (the engine's update dispatcher - calls HookedUpdate)
KingdomCome+0x36db / +0x4ad5 / +0x898a (main)
```

- **FACT — no DEADLOCK.** The other 198 threads are all idle worker pools
  (`NtWaitForSingleObject` x127, `NtWaitForAlertByThreadId` x34, `NtWaitForWorkViaWorkerFactory`,
  etc.). No thread is blocked on a kcdx lock; `g_poolLock` is not held anywhere. (PROBE P-A)
- **FACT — the wedge is a `SleepEx`, on the MAIN thread, inside WHGame's
  `C_Game::CreateInstance` -> FSR2 code path** — reached via our `HookedUpdate` trampoline
  calling the game's original `update`. The thread is sleeping/spinning in the GAME's own
  upscaler/instance-create code, NOT in kcdx code. (PROBE P-A)
- **FACT — kcdx does NOT hook any FSR2 / render / present / swapchain / d3d12 function.** The
  ONLY per-frame kcdx hook is `update` itself (`HookedUpdate`); `find_slots` (the Phase-5
  triplet) is file-ops only, off the frame path. So the FSR2 frames above `HookedUpdate` are
  the GAME's code reached through our pass-through update hook, not a kcdx FSR hook. (FACT —
  `grep` of `src/*.cpp` install sites + `src/fs_takeover/find_slots.*`)
- **CORRECTION to a sample-1 reading:** the FIRST cdb sample (before symbols fully reloaded)
  showed the frame as a bare `WHGame!...` address and I read it as "no kcdx frame on the
  stack." Samples 2/3, with kcdx symbols loaded, resolve it to `kcdx!HookedUpdate` — kcdx IS
  on the wedged stack (as the per-frame update pass-through). The corrected fact supersedes
  the sample-1 reading. (Per results-driven: re-observe, don't carry a stale read.)

## P-B — vanilla (no-kcdx) control launch (RAN 2026-06-20, kcdx.disabled switch)

Dropped a `kcdx.disabled` marker next to `kcdx.exe` (the launcher's pre-everything disable
switch → `LaunchGameVanilla`, zero injection / zero engine / zero logging), launched, then
removed the marker.

**Result: VANILLA boots clean to an interactive main menu — no hang, no crash, and no new
kcdx log produced (the disabled path sets up no logging, so zero new logs is the success
signature).** This is P-B's decisive outcome.

- **FACT — the hang is kcdx-introduced (H1), not engine/environment (H2).** Same game, same
  machine, kcdx the only variable: disabled → interactive menu; enabled → wedge in
  `C_Game::CreateInstance`/FSR2. H2 (vanilla stalls identically) is FALSIFIED — vanilla does
  not stall. (PROBE P-B)

## Reframe 2 (2026-06-20 — /debug §B code read of `HookedUpdate`; corrects P-A's next-probe premise)

The "Open questions" below proposed the next probe as "bypass `hook_chain::DispatchPre` in
`HookedUpdate`." Reading `HookedUpdate`'s body (`src/hooks.cpp` 358–1027) shows **it makes no
`DispatchPre`/`DispatchPost` call** — the per-frame chain dispatch happens INSIDE the game's
`g_orig_update` via the MinHook detours `hook_chain` installed on OTHER engine functions, not
as an explicit call in `HookedUpdate`. The design comment at `hooks.cpp:1094` ("drives the
chain's per-frame DispatchPre/Post") is an unproven runtime-mechanism assertion the code read
disproves (results-driven §"a design clause asserting a runtime mechanism is a probe target").

`HookedUpdate`'s ACTUAL per-frame body (steady state, after the one-shot `done` latch):
1. `lua_registry::ApplyZone(AfterGame)` — idempotent drain, no-op when queue empty (line 692)
2. `task::DrainQueue()` — runs plugin AddTask work on the main thread (line 696)
3. `test::EmitSummaryIfChanged(...)` + several `static bool`-latched cap-NN report blocks (702–1024)
4. `g_orig_update(p1, p2, p3)` — the GAME's original update; the wedged FSR2 frames are below THIS (line 1026)

So the correct next probe bypasses kcdx's whole per-frame body, not a non-existent `DispatchPre`.

## P-C — bypass kcdx's per-frame body (RAN 2026-06-20, live cdb on the hung process)

Built + deployed `// === DIAGNOSTIC (PROBE C)` early-jump in `HookedUpdate` (`src/hooks.cpp`
683) — first tick runs full one-shot init (VM/plugins/InputLoaded unchanged), every tick then
calls ONLY `g_orig_update` and returns, skipping the per-tick ApplyZone drain + DrainQueue +
cap-NN report blocks. Engine `kcdx.dll` redeployed (hash-verified), dev mode on. User launched.

**Result: HANGS IDENTICALLY.** Attached `cdb -pv -p <pid>` to the live hung process; dumped
all ~199 thread stacks.

- **FACT — kcdx's per-frame body is INNOCENT.** With the whole steady-state body bypassed the
  wedge is unchanged → `HookedUpdate`'s per-tick work (ApplyZone / DrainQueue / reports) does
  not cause the hang. (PROBE C)
- **FACT — ZERO kcdx frames on ANY of the ~199 threads.** No thread is executing kcdx code at
  hang time (grep of the full `~*k` dump for `kcdx` → empty). The hang is entirely inside the
  game's NGX/FSR2 init. (PROBE C, `cdb ~*k 8`)
- **FACT — the wedge is an NGX feature-update deadlock, not a kcdx file-read.** Main thread
  (`Main`): `NtWaitForAlertByThreadId` ← `RtlSleepConditionVariableSRW` ←
  `SleepConditionVariableSRW` ← `WHGame!NVSDK_NGX_UpdateFeature+0x368f0` — waiting on an SRW
  condition variable for an NGX feature update to complete. An NGX/FSR2 `JobWorker_NN` thread
  (stack base `fb2ff…`) is spinning in `KERNELBASE!SleepEx` ← `NVSDK_NGX_UpdateFeature+0x1eea94`
  — the producer that should signal the main thread's condvar is itself stuck spinning inside
  NGX, never completing. The other `JobWorker_NN` threads idle in `RtlSleepConditionVariableSRW`
  (no jobs). A producer-never-signals deadlock INSIDE `NVSDK_NGX_UpdateFeature`. (PROBE C, live cdb)
- **FACT — NGX/FSR2 modules ARE loaded** (`_nvngx`, `nvngx_dlss`, `amd_fidelityfx_upscaler_dx12`,
  `nvngx`) — not a missing-DLL load failure. NO thread is blocked on `NtReadFile`/`NtCreateFile`
  at hang time — so the deadlock is NOT a kcdx FS read blocking in the act. (PROBE C, `lm` + IO grep)

## Gate A (architect-review, 2026-06-20) + P-D — remove the live PROBE N confound

Dispatched `architect-review` cold (leading theory withheld) on the H3/H4 root-cause +
fix-direction, with the **no-thunk full-init-ownership invariant** as a binding constraint
(every thunk-back option cut before surfacing). Key results:

- **FALSIFIED the lock-inversion theory by code read:** `g_poolLock` (`src/fs_takeover/file_handle.cpp:40`)
  is a documented LEAF lock — never calls outward under hold, cannot self-deadlock; P-A
  confirms no thread holds it at hang time. A kcdx-internal lock-order inversion is NOT it.
- **Found PROBE N LIVE in HEAD** (`vtable_swap.cpp` `KcdxFOpenMarker`): on EVERY boot-window
  FOpen, on EVERY thread the takeover dispatches on (~45k+28k hits across tids incl. worker
  threads), it ran the engine's ORIGINAL FOpen+FClose (`g_probeN_orig*`) + four 0x400-byte
  whole-object snapshots, THEN the real kcdx open. A `working-artifacts.md` no-residue
  violation AND the strongest M1 suspect — it double-opens every boot file through the engine
  CRT cross-thread, exactly the perturbation P-C points at. **Every prior KI-0028 probe ran
  with this confound in the tree** (its `objdiff` output was never even examined).
- Architect verdict `re-task`: remove PROBE N first (mandatory cleanup + cheapest falsifying
  test), re-launch; if it persists, run the engine-original-thunk tid/lock-ordering probe.

**P-D (done, commit `678fd4f`):** removed PROBE N (marker image-diff block + captured
`g_probeN_orig*` + swap-time capture) and the dead PROBE G/J scaffolding (both compile-time
`false`, zero runtime effect — so the behavioral delta is exactly "PROBE N gone", re-test
stays one-variable). `KcdxFOpenMarker` keeps its production logic (first-fire cap-108 seating
signal → delegate to real `kcdx_FOpen`). Build green; engine redeployed (hash-verified).

**P-E (next — the falsifying re-launch):** boot with PROBE N gone.
- Boots past the menu (interactive) → **M1 confirmed**: PROBE N's per-open engine-CRT
  re-entry across worker threads was the root cause (write the AP17 mechanism paragraph: the
  engine-original FOpen+FClose, run re-entrantly from FSR2 JobWorker threads at an NGX-init
  point the engine never called it from, deadlocked NGX's `UpdateFeature` job).
- Still hangs → M1 ruled out; run architect Option B (instrument the engine-original thunks —
  index-miss + 8 metadata-miss arms — for tid + lock-acquire ordering during boot, stack-capture
  an NGX JobWorker entering a kcdx slot). Outcome map there decides M2 (serialize/confine the
  thunks — a CLEARLY-GATED kcdx resolver lock, never a timing fix) vs a state-perturbation
  upstream of the resolver.

## P-E — falsifying re-launch with PROBE N gone (RAN 2026-06-20, clean build, live cdb)

Boot with PROBE N removed (commit `678fd4f`). **Result: STILL HANGS — audio, no menu.**
Live cdb on the clean-build hung process (PID 45516), all-thread dump:

- **FACT — M1 (PROBE N re-entry) FALSIFIED.** Same wedge with PROBE N gone → the per-open
  engine-CRT re-entry was a real rule violation but NOT the hang cause. (PROBE E)
- **FACT — M2-as-live-contention FALSIFIED.** The ONLY kcdx frame on any of ~199 threads is
  `HookedUpdate` (the expected per-frame update pass-through). **NO CCryPak / FOpen /
  AdjustFileName / engine-original-thunk frame on ANY thread.** No NGX/FSR2 worker is sitting
  inside a kcdx file slot or a resolver thunk at hang time — the architect's "FSR2 JobWorker
  blocked inside a kcdx thunk" hypothesis is disproven. (PROBE E, `~*k` grep)
- **FACT — the takeover COMPLETED cleanly before the wedge.** `seat_index_stored entries=307006`,
  cap-108 seating PASS, every serve `diffs=0`. kcdx's file work is DONE; the wedge is downstream
  of a finished takeover. (PROBE E, dev log `kcdx-dev_2026-06-20_20-28-59.log`)
- **FACT — the wedge runs INSIDE the update loop.** Main-thread stack:
  `KingdomCome` → FSR2 frames → `kcdx!HookedUpdate+0x945` → engine update dispatcher →
  `C_Game::CreateInstance` → `NVSDK_NGX_UpdateFeature` → `SleepConditionVariableSRW`. So FSR2/NGX
  `UpdateFeature` is called EACH FRAME from the original update, and each frame blocks on the NGX
  condvar. The one worker in `UpdateFeature` SleepEx is named `SteamRequestThread(NoCfgFound)`
  (a Steam/NGX-library thread name — NOT verified to be a live this-boot signal; do not over-read
  it). (PROBE E, live cdb)

## Reframe 3 — the mechanism is a state-perturbation UPSTREAM of the resolver, still UN-PINNED

Every concrete theory is now falsified: not wrong file content (`diffs=0`), not the per-frame
body (P-C), not PROBE N (P-E), not a live kcdx-thunk contention (no thunk frame on any thread),
not a kcdx-internal lock inversion (`g_poolLock` is a leaf, unheld). What remains is the
architect's third branch: **kcdx's (now-completed) FS takeover changed some boot STATE that NGX's
async `UpdateFeature` depends on, and NGX never signals its condvar.** kcdx is not on the stack
because its file work already finished; the perturbation persists after it. The MECHANISM is not
yet observed — per AP17 this does NOT close, and per results-driven (theories hopped 2+ times,
same wedge re-confirmed 4×) the next step is a fresh-frame, ground-truth probe of what the NGX
condvar waits on, NOT another theory or another stack dump.

- **P-F (next — fresh-frame designed): observe what NGX `UpdateFeature`'s condvar is waiting to be
  signalled BY, and which takeover side-effect breaks that signal.** Candidate instrumentation
  (the fresh-frame subagent designs the exact probe): trace every file/registry/D3D12-resource
  request NGX/FSR2 makes during init and diff vs. what kcdx served (a MISS kcdx returns where the
  original engine would have HIT — the KI-0027 class one layer over: an alias, a search-path
  order, or a `FindFirst` pattern the FSR2 init uses that kcdx enumeration doesn't satisfy). The
  fix, whatever it is, stays INSIDE kcdx's full-init ownership — no thunk-back (user-confirmed
  hard invariant).

## P-F — swap-suppression bisection (RAN 2026-06-20, kcdx-noswap marker, clean build)

Dropped a `<kcdx-engine>/kcdx-noswap` marker → the seating hook skipped ONLY
`SwapVtableOnObject` + the index build; every other kcdx init (ctor bracket, worker
threads, `g_kcdxReadyEvent`, overlay map) ran identically; the engine kept its own CCryPak
vtable. **Result: REACHED THE MENU (interactive).**

- **FACT — H4 (init timing/threading side-effects) FALSIFIED; H3 (FS-takeover dispatch) CONFIRMED.**
  With kcdx fully initialized (all threads/bracket/ready-event ran — `probe_f_swap_suppressed`
  logged) but the swap suppressed, boot reaches an interactive menu. The hang REQUIRES the
  FS-takeover dispatch to be live. The cause is what the swapped CCryPak serves/returns to the
  NGX/FSR2 init — NOT kcdx's added threads or reordered timing. (PROBE F)
- **FACT — kcdx served ZERO file ops this run** (`FS_BOOT_TRACE count=0`) yet booted fine →
  confirms the engine owned the filesystem and the menu came up without kcdx's dispatch. The
  swap being live is the single differentiator between hang and menu. (PROBE F)

## Reframe 4 — H3 sub-mechanism: the swap perturbs NGX WITHOUT NGX opening a file through kcdx

Tension to resolve: the live-swap runs showed NO NGX/DLSS/FSR-named file op routed through kcdx
(`FS_BOOT_TRACE` NGX-class count = 0), and at hang time no NGX thread is inside a kcdx FS frame —
yet P-F proves the swap is the cause. So the swap perturbs NGX through something OTHER than NGX
directly opening an NGX-named file via kcdx. Candidate sub-mechanisms (the next probe splits them):

- **H3a (opaque-handle straddle):** NGX/FSR2/Streamline/Steam does memory-mapped, OVERLAPPED-async,
  or `DuplicateHandle` I/O on a file the engine opened THROUGH the swapped CCryPak — getting back a
  kcdx OPAQUE handle-id (`src/fs_takeover/file_handle.cpp:45` `Encode=(id<<1)|1`), NOT a real OS
  HANDLE. Any Win32 API that treats that id as a real handle (CreateFileMapping, an async wait,
  DuplicateHandle) operates garbage → an async completion that never signals → the condvar wedge.
- **H3b (a non-NGX-named file NGX init depends on):** an engine file the FSR2/upscaler init reads
  via kcdx under a generic name (a shader/pipeline/D3D12 cache, a config) where kcdx's serve is
  subtly wrong on THIS path (a method/size/handle-semantics difference the `diffs=0` object-compare
  doesn't catch — `diffs=0` compares the CCryPak OBJECT bytes, not the returned handle's I/O semantics).
- **H3c (handle-type/return-value contract):** a slot kcdx owns returns a value with different
  semantics than the engine original (e.g. `FGetCachedFileData` returns a pointer into a kcdx
  `std::vector` valid only until Close — `read_slots.cpp:91`; or a handle the engine passes to an
  API expecting an OS fd) that an NGX-init code path consumes.

- **P-G (next): instrument the swapped slots to log EVERY file op whose RETURNED handle/pointer
  could be consumed as a real OS handle** — tag each open/read by the calling tid, and flag any op
  on a tid that is (or spawns) an NGX/FSR2/Steam worker, plus any `FGetCachedFileData`/mapping/
  duplicate path. Diff what kcdx returns vs the engine-original handle semantics for those ops.
  The decisive question: which file, opened through which kcdx slot, hands NGX a kcdx-opaque value
  it then uses as a real OS handle. The fix stays inside kcdx's full ownership (no thunk-back) —
  e.g. kcdx mints a REAL OS handle for ops whose consumer needs one, still owning the open.

## P-G.0 — read-only narrowing on the P-E live-swap log (no launch)

Before instrumenting, exhausted the captured P-E ground truth:

- **FACT — slot 40 `FGetCachedFileData` (the mmap/cached-data H3c suspect) was NEVER called**
  this boot (`FGetCachedFileData` count = 0). The cached-data/mmap-lifetime mechanism is
  FALSIFIED — NGX does not use it. (read-only, P-E dev log)
- **FACT — the `(NoCfg)`/`(NoCfgFound)` thread-name suffix is a RED HERRING.** `AudioThread(NoCfg)`
  carries the same suffix and audio works — it is an engine thread-naming convention, NOT a live
  this-boot config-miss signal. Do not read `SteamRequestThread(NoCfgFound)` as a config failure.
  (cdb thread-name dump)
- **FACT — the second-heaviest caller of kcdx's FS slots is the `RenderThread`.** Two threads
  dominate the kcdx FS ops: tid 46280 (27k ops, main/boot) and tid 12612 = `0x3144` = **`RenderThread`**
  (19k ops). The FSR2/NGX upscaler init runs on the render path — so NGX's dependency on kcdx is the
  RenderThread requesting files through the swapped CCryPak. Other graphics threads present:
  `ShaderCompile`, `PSOCompilationWorker_0/1`, `D3D Background Thread 0-3`, `Streaming File IO HDD/Optical/InMemory`.
  (P-E dev log tid census + cdb thread names)

So the H3 mechanism is: a file the RenderThread (FSR2/NGX init) opens/reads/stats through a kcdx
slot gets an answer whose SEMANTICS differ from the engine original (not content — `diffs=0` — but
handle type, return contract, or an existence/enumeration answer), and NGX's init wedges on it.

- **The render/shader path through kcdx (P-E live-swap log):** the RenderThread (tid 12612) +
  a shader worker (tid 40364) read shader `.cfxb` / PSO-cache files. Engine paks serve fine via
  kcdx (`%engine%/shaders/cache/d3d12/helper.cfxb` → `how=index-pak result=3`); the `%user%/shaders/
  cache/d3d12/*` loose reads MISS (`errno=2`, first-run cache not built — vanilla misses these too,
  NOT anomalous on their own). Only 5 FWrite ops total, none to shaders/cache → the shader cache is
  NOT being written this boot (consistent with a render init that wedges BEFORE the cache-write phase).
  The single differing op is not yet isolated from the log alone.

- **P-G (next — instrument the graphics-thread kcdx FS ops): log every kcdx slot call from the
  RenderThread / ShaderCompile / PSO / D3D / Streaming tids with vpath + slot + exact return
  (handle id, size, exists-bool, attr), AND capture the engine-original answer for the same op
  inline** (call the original alongside kcdx's and log both — a same-run A/B, since the P-F
  swap-suppressed run served nothing and can't be diffed op-by-op). The op whose kcdx return
  differs from the engine's on the render path is the cause. Fix stays inside kcdx ownership (kcdx
  returns the correct handle-type/contract the render path needs, still owning the open) — no thunk-back.

## P-H — boot-progress telemetry + auto-stackdump (DESIGNED, Gate A cleared 2026-06-20; build pending)

The logging-defect audit (per the user: "any unknown is a log defect at this point — prove
everything with logs, no eyeball") found the gap: at the wedge window kcdx logs its own update-tick
artifacts but ZERO engine-boot-phase markers, so "log goes silent at 20:29:43" is ambiguous and the
latency-vs-deadlock fork currently rests on the user's eyeball ("audio, no menu"). P-H closes that.

**The fork P-H resolves:** is the ~20:29:43 NGX wait PERMANENT (deadlock) or LATENT (takeover makes
NGX/FSR2 init take minutes)? The captured logs cannot tell (one 37s-late snapshot ≠ "never wakes").

**Three additions, all reusing machinery kcdx already has. Build status: ledger below.**

| # | Step | Status | Commit |
|---|------|--------|--------|
| H1 | Per-tick heartbeat in `HookedUpdate` — integer-second transition edge (NOT a timer). Cessation = the wedge signature. | DONE | 90d2ef0 |
| ~~H2~~ | ~~menu-pump marker on id 4~~ — **DROPPED** (user, 2026-06-20): requires a new engine-direct hook for a signal the architect rated a weak floor; H1's heartbeat already resolves the fork. | DROPPED | — |
| H3 | Watcher-thread auto-stackdump on heartbeat stall (N=10s), +30s second dump. Dedicated watcher, suspend→capture-raw→resume→log. | DONE | 90d2ef0 |

### P-H RESULT (RAN 2026-06-20 22:34–22:38, live cdb on the still-running process)

**The heartbeat NEVER stalled — the Main update tick is healthy. The wedge is NOT a deadlock and NOT a wedged Main thread. It is a non-progressing `SleepEx` POLL-LOOP inside `C_Game::CreateInstance` → FSR2 code.** (RAN — log + live dump)

PROVEN facts:
- **197 `BOOT_WATCH heartbeat` lines, tick 1→47539, advancing continuously for 3m16s** (`22:35:03`→`22:38:19`, the last log line = file mtime). **No gap >2s** the entire run; zero `BOOT_WATCH_STALL`, zero `BOOT_DUMP` — the watcher never fired because the tick never stalled. (PROVEN — `_research/probe-archive/ki0028-ph-boot_watch-heartbeat.txt`)
- **The Main thread (`90c4.adc0`, "Main") is in a SLEEP-RETRY LOOP, not an event wait.** Stack: `NtDelayExecution ← RtlDelayExecution ← KERNELBASE!SleepEx ← WHGame!ffxFsr2ResourceIsNull+0x36af90 ← C_Game::CreateInstance+0x2e8c63 ← +0x2e8d7d ← ffxFsr2ResourceIsNull+0x16cce2 ← …`. The top is `NtDelayExecution`/`SleepEx` (a TIMED sleep), NOT `SleepConditionVariableSRW` (the P-E capture's event wait). **Two samples 2s apart are BYTE-IDENTICAL** (same RVAs) → a non-progressing loop, not forward progress. (PROVEN — `ki0028-ph-main-renderthread-deep-22-38.txt` + the two A/B samples)
- **`CreateInstance` NEVER returns** → game instance construction never completes → menu never loads; Main owns the window message pump but is buried in the FSR2 sleep-loop → it never pumps messages → **Alt+F4 is ignored** (user-observed, mechanism-explained). (PROVEN — Main stack + user report)
- The heartbeat runs because the engine pumps `CGame::Update` (the hook source) on a DIFFERENT thread while Main is still in `CreateInstance`. So "heartbeat alive" ≠ "Main alive at the menu" — it proved Main is not HUNG (the tick advances), which correctly distinguished this sleep-LOOP from a lost-wakeup deadlock. (PROVEN — Main in CreateInstance ≠ CGame::Update)

**Verdict on the latency-vs-deadlock fork: NEITHER.** It is a busy SLEEP-poll loop on a condition FSR2 init checks that, under the FS takeover, never becomes true. FSR2 (`ffxFsr2ResourceIsNull` neighborhood) reads resources/files; the takeover serves what it reads. The loop polls for some resource/state to become ready that never does. This is a kcdx-served-content / resource-readiness problem on the FSR2 init path — back to an H3-class root cause (the swapped object serves FSR2 something it then waits on), now with the wait mechanism PINNED (a SleepEx retry-poll, not an SRW condvar).

**The earlier P-E "SleepConditionVariableSRW in NGX UpdateFeature" capture was a DIFFERENT wait** than this `SleepEx`/FSR2 loop — either a different point in the same stuck init, or the P-E capture caught a transient. The decisive, reproducible state is THIS one: the byte-identical SleepEx/FSR2 loop in CreateInstance.

### Static recon (`/research-disassembly`, 2026-06-20) — the loop is a WINDOW-FOCUS poll, NOT FSR2/filesystem

Disassembled the `SleepEx` frame (`ffxFsr2ResourceIsNull+0x36af90`, RVA 0x865fb4) — body-read, verified:
- **It is a window-activation poll, not an FSR2 resource wait.** `ffxFsr2ResourceIsNull`
  is just the nearest export symbol. The loop calls **`USER32!GetActiveWindow`**, compares
  the active window to an expected handle (`rsi`), and **`KERNEL32!Sleep(5)`s up to 5×**
  then returns — BOUNDED ~25ms, not the infinite hang itself. (FACT — resolved IAT slots:
  Sleep @ RVA 0x3a02738, GetActiveWindow @ RVA 0x3a03260; `_research/ki0028-fsr2-poll-loop-recon/`)
- The polled globals g1/g2 both resolve to one window/system-manager singleton at **RVA
  0x492b890** (a gEnv-family global, adjacent to gEnv id-11 base 0x492b800; NULL in the
  static image, runtime-populated). (FACT — rip-relative resolve)
- Single call site `0x667ddd` inside a larger frame/tick-step fn; no local back-edge. The
  infinite repetition is `CreateInstance`'s OUTER loop re-running this step — that outer
  body is NOT yet read (AP19: not asserting its exact exit condition). (FACT — caller scan)

**DIRECTION CHANGE:** KI-0028 is NOT "the FS takeover serves FSR2 wrong content." It is a
**window activation / focus handshake that never completes** — the game window never becomes
the active window, so the outer init loop never proceeds → no menu; the window never enters
its normal message loop → Alt+F4 ignored (mechanism now explained). The kcdx suspect surface
shifts from FS-content to **whether kcdx's init perturbs window creation / activation / focus
/ the window-manager singleton at 0x492b890.**

**Gate A corrections (architect-review, BINDING — the build MUST honor these):**
- **F1 (CRITICAL, source-confirmed):** every `LOG_*_KV` takes the stream mutex (`src/log.cpp:520/529/538`).
  Suspending threads from the tick callback and then logging their frames DEADLOCKS the game (a
  suspended thread mid-log holds the mutex the dumper needs → the probe becomes a second wedge,
  destroying the evidence). Fix: dump from a dedicated watcher thread; NO `LOG_*_KV` while ANY thread
  is suspended — snapshot raw CONTEXT+frame data inside the suspended window, resume all, THEN format
  and emit through the logger. Suspend → capture-raw → resume → log.
- **F2:** the native-unwinder `ReadProcessMemory(GetCurrentProcess(), Rsp, …)` of another thread's
  stack is valid ONLY while that thread is suspended — the walk runs strictly inside the per-thread
  suspended window.
- **F4:** H2's marker means "UI-pump path executed," not "menu interactive." Do not assert a menu-ready
  semantic (a real menu-ready edge — e.g. the `this->byte_at_0x2A2F` the pump writes — would need its
  own probe; not built now, results-driven).
- **F5:** N=10s (user-set) — trigger on HEARTBEAT STALL (main thread stopped ticking), not on a
  missing menu fire.
- **F6:** a single onset dump only re-shows the parked NGX waits we already saw (the 37s-snapshot
  flaw). Dump at onset AND +30s so "zero progress on any thread across the interval" is OBSERVED, not
  inferred. **Heartbeat RESUMING is the primary, decisive falsifier.**

**Outcome→meaning map (pre-committed, one primary variable = main-thread liveness):**
- Heartbeat RESUMES after the stall → **LATENCY** (NGX init is slow, not deadlocked). Decisive, single signal.
- Heartbeat NEVER resumes + the two dumps (onset, +30s) show IDENTICAL parked NGX/SRW waits, zero
  progress on every thread → **DEADLOCK** (high confidence).
- H2 marker NEVER fires → wedge is upstream of UI pumping (deadlock-before-UI), narrows the site.
- H2 marker fires, heartbeat then stalls → UI path reached, wedge is later (in/after NGX), consistent
  with the cdb capture.

P-H is a probe (no thunk-back, no coexistence-fix — architect F7 clean). On retirement it captures to
`_research/probe-archive/` then removes from live source (the heartbeat may graduate to a kept
boot-progress diagnostic like FS_BOOT_TRACE if the user wants it permanent — decide at retirement).

## Reframe 5 (2026-06-20 — P-G mined the captured logs; CORRECTED after a premature "just slow" claim)

P-G's data was ALREADY captured — the FS_BOOT_TRACE kept diagnostic logged every render-window op,
the dev log holds the full tick stream, and the P-E cdb capture holds the resolved thread stacks.
A first pass over-read this as "boot is progressing, just slow"; that was a JUMP. Re-mined strictly
against the logs, separating PROVEN from INFERRED:

**PROVEN (log/dump-cited):**
- **The P-E cdb capture was taken at `20:30:20.935`** (capture-file mtime), **37s AFTER the dev
  log's last line at `20:29:43.475`** (the log goes silent there). (PROVEN — file mtime + dev tail)
- **At capture time (`20:30:20`), RenderThread (`b1cc.3144` = tid 12612, cdb-named "RenderThread"),
  ShaderCompile (`b1cc.b2b0`), and the main thread are ALL parked in NGX/FSR2/CreateInstance
  waits.** RenderThread: `NtWaitForAlertByThreadId ← RtlSleepConditionVariableSRW ← _Cnd_wait ←
  WHGame!NVSDK_NGX_UpdateFeature+0x20139e ← ffxFsr2ResourceIsNull...`. ShaderCompile: `SleepEx ←
  C_Game::CreateInstance+0x46514`. Main: `SleepConditionVariableSRW ← NVSDK_NGX_UpdateFeature`.
  (PROVEN — `ki28_pe_allthreads.txt` stacks)
- **The update tick ran 30 SUMMARY emissions, first `20:29:02.380`, last `20:29:43.475`** — the
  `HookedUpdate` steady-state body executed many times, suite reached `passing=320`. (PROVEN — 30
  `[TEST] SUMMARY` lines in the P-E dev log)
- **The dev log STOPS at `20:29:43.475`** and is silent for the 37s up to the capture. (PROVEN)
- The recycled-handle-id corruption theory is FALSIFIED — zero `double_close`/`bad_handle` logged,
  though `Close()` logs both. (PROVEN — `grep -c` = 0)

**The reconciliation (what the timeline actually means):** boot is NOT "permanently wedged from the
start" (the tick loop ran 41s, suite hit 320) AND it is NOT "just slow / progressing fine" (the dump
shows three threads hard-parked in NGX). The precise, log-proven shape: **kcdx-on boot PROGRESSES for
~41s (ticks firing, suite climbing to 320), then the log goes SILENT at `20:29:43`, and the dump 37s
later shows RenderThread + main + ShaderCompile parked in `NVSDK_NGX_UpdateFeature` / `CreateInstance`
waits.** The wedge ONSET is `~20:29:43`, after a window of real progress — not at boot start, not
absent.

**INFERRED, NOT yet proven (must not be stated as fact):**
- That the wedge is PERMANENT (the dump is a single 37s-later snapshot; it proves "still parked at
  20:30:20", NOT "never wakes"). A longer wait or a second capture is owed to prove permanence.
- That "the engine got past `C_Game::CreateInstance`" — the tick loop firing does NOT prove
  `CreateInstance` returned; the update tick runs on a different path, and ShaderCompile is still
  INSIDE `CreateInstance` at capture. This earlier claim is WITHDRAWN as unproven.
- Whether the menu ever renders in the P-E (swap-on) run — the current `kcd.log` on disk is from the
  P-F (`20:39`, swap-OFF) run, which OVERWROTE the P-E engine log. There is NO `kcd.log` evidence
  for P-E's menu state. (PROVEN that the evidence is absent — `kcd.log` mtime `20:40` ≠ P-E `20:28`.)

So the P-G per-op A/B-trace plan stays the WRONG next probe (it hunts a differing return for a wedge,
but the wedge is an NGX condvar wait, and kcdx is on no NGX stack). But the prior "just slow"
reframe is ALSO withdrawn. The pinned-down question is now narrow and falsifiable (below).

## Open questions (for /debug — after P-C)

- **NEW (Reframe 5, log-proven): is the `~20:29:43` NGX wedge PERMANENT, or does it eventually wake?**
  The dump proves three threads parked in `NVSDK_NGX_UpdateFeature` at `20:30:20` (37s after the log
  went silent) — but a single snapshot cannot prove "never wakes." Decisive cheap observation: launch
  swap-ON and WAIT 2–3 min at the audio/no-menu state. Menu eventually renders → the NGX wait
  resolves and the bug is boot LATENCY (something the takeover does makes the NGX/FSR2 init take
  minutes); the next probe times the NGX-init window swap-on vs swap-off. Menu NEVER renders →
  confirmed permanent NGX-condvar wedge at `~20:29:43`, and the next probe instruments WHAT the
  RenderThread's NGX worker is waiting on (the condvar's signaller) — kcdx perturbed a state NGX's
  `UpdateFeature` depends on. Either branch keeps the fix inside kcdx ownership (no thunk-back). Run
  this single observation before any instrumentation — it splits latency-vs-deadlock, the one fork
  the captured logs cannot resolve.


The wedge is a deadlock INSIDE NGX's `UpdateFeature` (main waits a condvar; the NGX worker that
should signal it spins forever), with kcdx on no stack and no thread blocked on a file read. P-B
already proved it is kcdx-INTRODUCED (vanilla boots). So kcdx perturbs NGX init WITHOUT being on
the stack at hang time — it changed some STATE that NGX reads, earlier in boot. Two live causes:

- **H3 (takeover-served NGX input is wrong/incomplete):** NGX/FSR2 init read a file kcdx now
  serves (an NGX config / model snippet / pipeline-or-shader cache / the upscaler's own data)
  and got wrong-or-empty bytes — succeeded the read (so no I/O block now) but on bad content NGX
  enters a state where its worker spins forever. The FS takeover's serve for SOME NGX-needed path
  is subtly wrong (a length, an alias, a `%engine%`-class path, a bad enumeration result).
- **H4 (takeover changed init TIMING/threading):** the takeover reordered or re-threaded boot
  enough that NGX's `UpdateFeature` runs before a dependency it needs, or its job is dispatched
  onto a worker pool whose state kcdx altered — a lost-wakeup the takeover's reordering introduced,
  not a content problem.

Decisive next probe (the cheapest-most-falsifying — read-only, no relaunch needed YET):

- **P-D (read-only, on the captured dump + the dev log): enumerate every file kcdx served during
  this boot (the dev-log `read_entry` / `FindFirst` lines) and intersect with NGX/FSR2-needed
  paths (anything under an `engine`/`shaders`/`fsr`/`ngx`/`dlss`/`upscal` name, a `*.bin` model
  snippet, a pipeline cache).** A served NGX-class path with a wrong `usize`/`matched`/alias is
  the H3 smoking gun; none found → H4 (timing/threading) and the next probe bisects WHEN the
  takeover installs vs when NGX inits. This reads the existing `kcdx-dev_<ts>.log` from THIS run
  — no relaunch; the hung process + its log are the ground truth in hand.
- The H1/H2 split + P-B + the superseded DispatchPre-bypass premise, preserved below for the trail:

P-A localized the wedge to a `SleepEx` on the main thread inside WHGame's
`C_Game::CreateInstance`/FSR2 path, reached via our `HookedUpdate`. P-B confirmed **H1
(kcdx-introduced)**; P-C narrowed it to an NGX-internal deadlock with kcdx's per-frame body
innocent (above). [Historical: the remaining question was WHICH kcdx effect wedges the path —
the per-frame body (ruled out by P-C) or the boot-state the FS takeover changed (now H3/H4).]

- **P-C (next — bypass kcdx's per-frame body): for one launch, make `HookedUpdate` call ONLY
  `g_orig_update(p1,p2,p3)` — skip the ApplyZone drain, DrainQueue, and the test-report
  blocks (guard them behind a `// === DIAGNOSTIC (PROBE C)` early-jump).** One variable: kcdx's
  per-frame work, present vs absent. Wedge CLEARS (game boots past menu) → kcdx's per-frame
  body is the cause (next: bisect ApplyZone vs DrainQueue vs the report blocks). Wedge PERSISTS
  → `HookedUpdate`'s body is innocent; the cause is the boot-STATE the FS takeover changed
  (what `CreateInstance`/FSR2 reads), and the probe moves to the takeover's effect on init
  state, not the per-frame path. Cheap, one-site, falsifiable both ways.
- The original H1/H2 split + the (now-superseded) DispatchPre-bypass premise, preserved for
  the trail:

- **H1 (kcdx-caused):** something `HookedUpdate` does each frame — the original-`update`
  trampoline call OR the `hook_chain::DispatchPre` per-frame pump it drives — makes the
  game's `CreateInstance`/FSR2 path spin/sleep forever. The FS takeover changed boot enough
  that the game reaches this path in a state where it busy-waits.
- **H2 (engine/environment):** the game's own FSR2/DLSS upscaler init at first-menu spins
  here regardless of kcdx — `HookedUpdate` is just the innocent per-frame call path, and a
  vanilla boot stalls identically in `C_Game::CreateInstance`/FSR2 in this environment.

- **P-B (decisive control): does a VANILLA (no-kcdx) boot reach AND get past the main menu in
  this same environment?** Vanilla ALSO hangs in `C_Game::CreateInstance`/FSR2 -> H2 (engine/
  environment; kcdx innocent, the takeover merely exposed it). Vanilla boots to a working,
  interactive menu -> H1 (kcdx-introduced; probe `HookedUpdate`/`DispatchPre` + what the FS
  takeover changed about the state `CreateInstance` reads). Cheap, no instrumentation — boot
  unmodded.
- If H1: the next probe bypasses `hook_chain::DispatchPre` in `HookedUpdate` for one launch
  (does the wedge clear with the per-frame chain pump disabled?) — isolates trampoline-call
  vs. chain-dispatch as the cause.
- The suite's `pending=21` is the in-game/manual rows that need menu interaction the hang
  prevents — not a separate signal. (Resolved — not a probe.)
