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

- Game launched with the KI-0027-fixed engine (`4befc07`): **audio loads** (sound
  audible), but **no video/render output** appears and **input is unresponsive**. The
  main menu never renders. The process does not crash or self-terminate — it hangs
  indefinitely until killed.
- No crash dump (a hang, not a crash — killed via Task Manager).

## Facts

- The table-DB load SUCCEEDS — zero "Database system error" / `err_id=259` in
  `kcd.log` this run (the KI-0027 fix holds). (FACT — `kcdx_2026-06-20_15-58-02.log`)
- The dev log's LAST line is `[15:58:50.093][PAK_READER] read_entry pak="Engine.pak"
  name="%engine%/engineassets/textures/cursor_green.dds"` — the boot wedged right at
  (or after) loading the main-menu cursor texture. The test-suite SUMMARY
  (`passing=320 reported=322 pending=21 total=343`) logged ~3.5s earlier at
  15:58:46.584. (FACT — `kcdx-dev_2026-06-20_15-58-02.log`, line 83562, the final line)
- The file-system-takeover directory enumeration is functioning: 381 `FindFirst`
  fires, the table `__*` globs return correct (small) match counts, the find-data ABI
  is served correctly. The `FindClose fires: 0` in the trace is a LOGGING gap
  (`kcdx_FindClose` emits no `TraceEnum` line — the close logic itself is correct:
  FindNext returns -1 at exhaustion, the consumer's `while(-1<ret)` exits, FindClose
  is called), NOT a handle leak. (FACT — same log + `src/fs_takeover/find_slots.cpp`
  FindNext/FindClose bodies read)
- `cursor_green.dds` is served from `Engine.pak` by kcdx's pak reader correctly
  (`method=8 usize=4232` — a normal DEFLATE entry read). The hang is AFTER the bytes
  are served, in whatever consumes them (the UI/render init), not in the FS serve.
  (FACT — same log)
- The boot reached deep init: trampoline pool, LUA_SHIM passes, FOREIGN_HOOK selftest,
  320 suite tests passing — so the engine, hooks, Lua VM, and FS are all up. Only the
  render/UI frontend bring-up is wedged. (FACT — `kcd.log` tail)

## Open questions (for /debug)

- WHERE exactly does the render/UI thread block — is it (a) a kcdx file slot
  mis-serving an engine asset the render device needs (a metadata/size/enum answer the
  graphics path consumes, the KI-0026-adjacent "kcdx slot answer-divergence" axis), (b)
  a genuine engine render-device init that always stalls in this environment
  (windowed/fullscreen/adapter), or (c) a deadlock between a kcdx slot's pool lock and
  the render thread? Needs a probe that observes the render-init thread's last action
  + whether any kcdx slot is on its stack. The dev log ending mid-asset-load (no fatal,
  no further FS activity) suggests the render/UI consumer thread is blocked, not a kcdx
  FS loop. (`/debug KI-0028` — get ground truth: which thread is wedged and on what.)
- Does a non-kcdx (vanilla) boot reach the menu in this same environment? (Establishes
  whether this is kcdx-introduced or a pre-existing environment issue the takeover
  merely exposed.)
- Is the suite's `pending=21` (21 tests never reported) significant, or just the
  in-game/manual rows that need a menu the boot never reached?
