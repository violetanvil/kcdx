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

## Open questions (for /debug — after P-C)

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
