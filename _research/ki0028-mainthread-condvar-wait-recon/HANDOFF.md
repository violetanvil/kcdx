# KI-0028 HANDOFF (2026-07-03, pre-compact) — current state + what's next

Read this FIRST after compact. Everything below is committed (HEAD `52a37cf`); nothing is lost.

## THE BUG IN ONE PARAGRAPH (current, decisive)

kcdx's full FS takeover is live; the game boots to a BLACK screen but is ALIVE and PRESENTING (~310fps).
Root mechanism (Reframe 15, proven): the **MAIN thread is STUCK in a condition-variable wait mid per-frame
tick** (`KERNELBASE!SleepConditionVariableSRW` ← `WHGame 0x1c1e7e0`), proven stuck by two BYTE-IDENTICAL
invasive `~0 k` samples. The RENDER thread is separate and keeps presenting the last empty frame (why present
advances). World geometry IS created (PROBE Z8: `geo_buf=262`, 614MB) but never bound (`ia_set_ib=0`,
`draw_indexed=0`) because the main-thread tick that would traverse the scene + record indexed draws is frozen.
User saw a GREEN GLITCH then black = a few frames drew, then the tick froze.

## THE FRONTIER (the ONLY open question — the next step)

**What is the main thread waiting FOR at `WHGame 0x1c1e7e0`, and does the FS swap keep its producer from
signaling it?** This is a STATIC `/research-disassembly` question (NO launch needed):
1. Read `0x1c1e7e0`'s body: which condvar/SRW address it waits on, what predicate/loop-exit condition.
2. Find who SIGNALS that condvar (grep WHGame for writers of that address / WakeConditionVariable callers).
3. Determine whether the FS swap derails that producer (a worker thread, a GPU fence, a load-completion signal).
Candidate producers: the worker-thread pool parked at nearest-export `ffxFsr2GetUpscaleRatioFromQuality
Mode+0x152c749` → `+0x567a86` in `_invasive_allthreads_02-25.txt` — resolve their REAL RVAs (base 0x7ffdf45b0000).

## KEY ADDRESSES (WHGame.dll base 0x7ffdf45b0000, release_1_5_1164953_841, image base 0x180000000)

- `0x1c1e7e0` — the main-thread WAIT caller (calls SleepConditionVariableSRW). THE probe target.
- `0x667b24` — the per-frame tick dispatcher (already recon'd, `ki0028-tick-geometry-dispatch-recon/`).
- `0x667ed0` — the tick's render-dispatch gate (`cmp [0x492b908]`). `[0x492b908]` IS installed (Reframe 14).
- `0x66a163` — main-thread frame near the tick dispatcher.
- `0x869c39` — window/display-mode fn (runs identically both arms; PROBE M; NOT "entity-init").
- SYMBOL NOISE: `NVSDK_NGX_UpdateFeature+…` / `ffxFsr2ResourceIsNull+…` / `ffxFsr2GetUpscaleRatioFrom
  QualityMode+…` are NEAREST-EXPORT artifacts. ALWAYS resolve real RVA = VA − base; never read the export name.

## WHAT'S RULED OUT (6 overturned root causes — do NOT re-litigate)

1. 0xD2 / CET_PrepareLevel abort — GONE (Reframe 13; the bind-root fix cleared it).
2. resourcelist.txt miss — red herring (exists in no pak; benign both arms).
3. bind-root pak-key gap — FIXED + live (commit `83a9279`; FS serves correctly).
4. window-activation — falsified (fg_is_ours=1; window activates).
5. renderer-dispatch gate `[0x492b908]` null — falsified (Reframe 14/Z7; it IS installed).
6. IB-never-created (branch a) — falsified (Reframe 15/Z8; 262 geo buffers created).
FS is FULLY exonerated (3×: Z2.2, PROBE W, Z5). The bug is NOT a file-serve problem.

## PROBE STATE (no-residue bookkeeping)

- **Z7 (renderer-gate)** — RETIRED, source removed, recipe archived (`_research/probe-archive/
  render-gate-z7-singleton-read.md`). Lesson archived: read a gEnv-table pointer from a WATCHER THREAD, never
  hook the hot tick (MH_EnableHook fails on a hot function).
- **Z8 (resource-creation)** — an EXTENSION of `src/fs_takeover/drawcall_probe.cpp` (CreateCommittedResource
  slot 27 + CreatePlacedResource slot 29, SDK-pinned). Its branch-(a) question is ANSWERED (falsified).
  STILL UNCOMMITTED (per no-residue). OWED: retire it (remove the Z8 counters/detours/hooks from
  drawcall_probe.cpp, capture nothing new — finding is in Reframe 15) OR keep it for fix-verification later.
  The draw probe itself (draw_indexed/ia_set_ib) stays useful to confirm the eventual fix.
- **crt_reader_probe (Z4/Z5)** — already retired earlier this session.

## REINGESTION LIST (read these after compact, in order)

1. `docs/known-issues/KI-0028-fs-takeover-boot-hang-ui-render-init.md` — **Reframe 15 FIRST** (top of trail),
   then 14, 13 for the overturned-causes chain. (The doc is LONG — read only Reframes 13-15 + the frontmatter;
   older reframes are superseded history.)
2. `_research/ki0028-mainthread-condvar-wait-recon/FINDINGS.md` + this HANDOFF.md — the decisive capture.
3. `_research/ki0028-mainthread-condvar-wait-recon/_invasive_main_x2_identical.txt` — the stuck-proof (2
   identical samples); `_invasive_allthreads_02-25.txt` — all 256 threads (the worker pool = candidate producers).
4. `_research/ki0028-tick-geometry-dispatch-recon/FINDINGS.md` — the tick dispatcher + render-gate recon.
5. CLAUDE.md + `.claude/rules/results-driven.md` + `.claude/skills/debug/SKILL.md` — process (probe-first,
   agent-builds-user-launches, root-cause-mechanism-before-close, Gate A/B).

## PROCESS REMINDERS (kcdx-specific, load-bearing)

- Agent builds (`pwsh ./build.ps1`) + deploys (copy kcdx.dll to `<game-bin>/kcdx-engine/`, Get-FileHash verify)
  + reads `kcdx-dev_<ts>.log`. USER only launches. Dev mode = `engine.toml dev_mode=true` (on).
- Invasive cdb: `-p <pid> -c "...; qd"` — ALWAYS `qd`/`.detach`, NEVER `q` (q kills the debuggee).
- Stage by EXACT path, never `git add -A` (shared index, parallel chats). `git push` → private only.
- Reuse-first RE ladder: DB → `_research/` dumps → predecessor sigs → wiki → fresh Ghidra LAST.
- A design/fix-direction fork routes through Gate A (architect-review) before the user; a Resolution routes
  through Gate B (root-cause-verifier). AP17: close only with a mechanism paragraph, never "symptom stopped".
