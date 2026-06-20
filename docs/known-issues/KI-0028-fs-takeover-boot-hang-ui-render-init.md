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

## Open questions (for /debug — after P-A)

P-A localizes the wedge to a `SleepEx` on the main thread inside WHGame's
`C_Game::CreateInstance`/FSR2 path, reached via our `HookedUpdate`. Two causes remain, and
they are genuinely different — P-B (the vanilla control) is the falsifying test between them:

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
