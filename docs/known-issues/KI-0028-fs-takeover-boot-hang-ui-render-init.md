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

## Open questions (for /debug — narrowed)

The axis is settled: the menu rendered and the FS serve succeeded, so this is NOT a kcdx
FS read-loop or a "menu never renders" failure. The remaining unknown is what `tid=46452`
is blocked ON after the last successful serve — and the only way to read that is to observe
the **wedged process's thread stacks live** (the static logs are exhausted).

- **P-A (decisive): capture the wedged process's thread stacks.** On the next launch, let
  it reach the hung menu, then attach `cdb`/procdump to the LIVE process (or take a full
  dump of the hung process) and read `~* k` — WHICH thread(s) are blocked and on what, and
  **is any kcdx symbol (a `kcdx.dll` frame — a file slot, the pool lock `g_poolLock`, a
  hook trampoline) on the wedged stack?** This is a hang, so a live/hung-process dump — NOT
  a crash dump — is the ground truth. Outcome map: kcdx frame on the blocked stack → kcdx
  slot/lock is the wedge (probe that slot next); no kcdx frame, pure engine/driver stack →
  the takeover unblocked the boot far enough to expose an engine/environment stall (P-B).
- **P-B (control): does a VANILLA (no-kcdx) boot reach AND get past this menu in this same
  environment?** Disambiguates kcdx-introduced vs. a pre-existing environment/menu issue the
  takeover merely let the boot reach. Cheap, no instrumentation — just launch unmodded.
- The suite's `pending=21` is the in-game/manual rows that need menu interaction the hang
  prevents — not a separate signal. (Resolved — not a probe.)
