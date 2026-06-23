# KI-0028 — POST-BIND-ROOT-FIX live capture (2026-06-22 22:41 run)

**Verdict:** the bind-root keying fix is CORRECT and CLEARED the documented level-load abort.
A black screen PERSISTS, but it is a DIFFERENT, downstream failure (NO-PRESENT / render), not the
FS-miss → empty-record → `RaiseException(0xD2)` abort KI-0028 documented. Ground-truth, theory-independent.

## What the fix achieved (verified, not inferred)

- **cap-112 (c) PASS** (dev log 22:41:16): `Levels/kutnohorsko/leveldata.xml` resolves under the bind-root
  prefixed key; bare `leveldata.xml` does NOT. The index keys each nested pak by its mount point.
- **Index healthy:** `asset_index_built entries=516363 roots=2 paks=77 pak_entries=516549 loose=4`.
- **FS layer serving:** `FS_BOOT_TRACE` tally — 4435 `index-pak-serve`, 13134 `index-pak` opens,
  2508 `miss-original` (the misses are `kcd.log`/`logbackups` — benign engine-log writes kcdx correctly
  declines, NOT level resources). NO level-resource miss in the trace. The bind-root gap is closed.

## The remaining symptom is NOT the KI-0028 abort

Live invasive cdb capture (PID 44280, Responding=True, 197 threads, 2.3 GB) — `_cdb_capture1.txt`:

- **Main thread ("Main", tid 69f4): healthy message pump** — `win32u!NtUserPeekMessage` ← `USER32!PeekMessageW`
  ← `WHGame+0x3d2509` ← `WHGame!…+0x36af48` (Steam `gameoverlayrenderer64` hooked in). The main thread is
  idle-pumping Windows messages, NOT blocked, NOT in an abort. The documented KI-0028 abort path
  (`CET_PrepareLevel` empty-record gate → `MessageBoxA` + `RaiseException(0xD2)`) would park the main thread in
  the exception/messagebox — it is NOT there. **The abort did not fire.**
- **`C_Game::CreateInstance` on 3-4 worker threads** (a real WHGame export, the level-creation entry the
  VANILLA-MAP traced). The engine REACHED and ENTERED level creation — exactly the stage that previously
  aborted. It is now progressing INTO it, not bailing.
- **Thread-state tally:** 127 `NtWaitForSingleObject`, 97+97 nvidia `nvwgf2umx!OpenAdapter12` waits, D3D12
  `BackgroundTaskScheduler::TaskThread` alive, 31 `SleepConditionVariableSRW`. A running, GPU-driver-busy,
  non-presenting process — the "NO-PRESENT, not a deadlock" signature (prior memory
  `project_kcdx_ki0028_no_present_not_deadlock`). NOT a kcdx-lock deadlock, NOT an FS hang.
- **`ffxFsr2*` / `NVSDK_NGX_*` frames are nearest-export NOISE** (WHGame has no PDB) — not trusted as labels.
  The trusted frames are the real exports: `PeekMessageW` (main pump), `C_Game::CreateInstance` (level create).

## Honest reframe

KI-0028 as documented = "a full FS-takeover swap aborts the level load → black screen; swap-off → menu." The
bind-root fix removes the FS-miss that drove the abort. The abort no longer fires (main thread pumps, no
`RaiseException`). What remains is a SEPARATE downstream gate: the engine enters `CreateInstance`, works in the
GPU driver, and never presents a frame. This is a present/render/level-init question, not the FS-resolution
question KI-0028's root cause named — a NEW symptom uncovered by clearing the FS gate, not the same one.

## Process note

The capture used `cdb -p <pid> -c '~* k 6; q'` — the `q` KILLED the live game (invasive detach). Next attach
MUST end with `qd` (detach-and-leave-running) to keep the debuggee alive for multi-pass capture. Memory
`feedback_cdb_qd_not_q_on_invasive_attach` had this; it was not applied. Raw capture: `_cdb_capture1.txt`.
