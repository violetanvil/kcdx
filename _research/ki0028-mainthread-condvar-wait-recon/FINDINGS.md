# KI-0028 — the MAIN thread is STUCK in a condvar wait mid-tick (invasive, 2 identical samples)

> **⚠ SUPERSEDED HEADLINE (PROBE Z9, 2026-07-03 run `kcdx-dev_2026-07-03_10-16-32.log`) — this doc's core
> "Main is STUCK in the CShaderMan condvar wait" claim is FALSIFIED. Z9 hooked the wait-loop `0x9ace14` and
> the ready-flag `[wait_obj+0x50]` FLIPPED to non-zero at wall_ms=7140 (shader system became ready), and the
> loop ran `wait_enters=13981` times over ~2 min. So `0x9ace14` is a PER-FRAME wait that completes every frame,
> NOT a one-shot boot gate that hangs. The two byte-identical invasive samples caught a RECURRING per-frame wait
> mid-block — the same "per-frame trap" the FULL-HANDOFF §13 warns about (like PROBE M's `0x869c39`), NOT a
> freeze. Reframe 15's "Main is stuck" + Reframe 16's "producer never signals ready" are BOTH overturned by Z9.
> The terminal stall is DOWNSTREAM of / different from this wait — it was never located. See §"PROBE Z9" below.**

**Date:** 2026-07-03 · **Method:** invasive cdb (`-p 42364`, `qd`-detached — game left running) on the live
black-screen process + PROBE Z8 resource-creation counts (run `kcdx-dev_2026-07-03_02-25-43.log`).
**Trust:** primary evidence — two `~0 k` samples byte-identical (same Child-SP), RVAs resolved against WHGame
base `0x7ffdf45b0000`. **See the SUPERSEDED-HEADLINE banner above — the "stuck" reading is falsified by Z9.**

## The decisive facts

1. **Geometry IS created, never bound (PROBE Z8).** `buf_created=339 geo_buf=262 geo_buf_bytes=643699136
   tex_created=7230` with `ia_set_ib=0 draw_indexed=0`. 262 DEFAULT-heap geometry-class buffers (614 MB)
   created via `CreateCommittedResource`/`CreatePlacedResource`, NONE bound (`IASetIndexBuffer` never called).
   → branch (a) "IB never created" FALSIFIED; the drop is between creation and command-list binding.

2. **The MAIN thread is STUCK (not transient).** `~0 k` sampled twice, seconds apart, BYTE-IDENTICAL
   (`_invasive_main_x2_identical.txt` — same Child-SP `908fdc80`/`908fdcc0`/…). "Main" thread `a57c.cf68`
   parked in `KERNELBASE!SleepConditionVariableSRW`.

3. **The wait is mid per-frame tick.** Resolved RVAs (VA − 0x7ffdf45b0000):
   - `0x1c1e7e0` — the wait caller (calls SleepConditionVariableSRW).
   - `0x66a163` — near the tick dispatcher `0x667b24` (per-frame tick region).
   - `0x869c39` — the window/display-mode/fullscreen fn (KI-0028 doc CORRECTION: NOT "entity-init"; PROBE M
     found it runs identically swap-on/off). `0x86b017` above it.
   Nearest-export symbols in the raw dump (`NVSDK_NGX_UpdateFeature+…`, `ffxFsr2ResourceIsNull+…`) are NOISE —
   use the resolved RVAs, not the export names (per the KI-0028 CORRECTION).

## The reconciliation (fits every prior fact)

The RENDER thread is separate and keeps PRESENTING (~310fps — `present_count` climbs). The MAIN thread stopped
advancing the game/scene tick — frozen in this condvar wait. Present-advancing ≠ main-thread-ticking. Geometry
was created during the boot phase that ran; then the main-thread tick that would traverse the scene and RECORD
indexed draws froze → no `IASetIndexBuffer` → the render thread re-presents the last (empty/sky) frame → black.

Does NOT contradict `ki0028_no_present_not_deadlock` (every thread runs, present advances) — the RENDER thread
still runs. NEW: the MAIN thread specifically is stuck (2 identical samples = stuck, not sampled-mid-transient).

**User observation this run:** a GREEN GLITCH in the video THEN black — a few frames rendered (geometry bound
while the tick ran), then the main thread hit this wait and froze → indexed draws stopped → black. The
rendering→frozen transition IS the green-glitch-then-black.

## The wait is a std::condition_variable predicate-wait on a "ready" flag (STATIC, decisive)

**Method:** static disasm of the wait chain from the 2 invasive samples' return addresses
(`disasm_condvar_wait.py` → `_condvar_wait.txt` / `_condvar_callers.txt`, WHGame image base 0x180000000).
**Trust:** primary — every claim is a body read with the cited site.

The chain Main is parked in, bottom-up:

- **`0x1c1e78c` = MSVC-STL `condition_variable` timed-wait primitive** (`_Wait_until` internal). Proven by the
  STL runtime IAT thunk table right above it (`_Mtx_lock`/`_Mtx_unlock`/`_Cnd_wait`/`_Xtime_get_ticks`/
  `_Thrd_id`, `0x1c1e743`–`0x1c1e785`). Body: `dec [rdi+0x4c]` (recursion), `lea rdx,[rdi+0x10]` (the SRWLOCK),
  `lea rcx,[r15+8]` (the CONDITION_VARIABLE), `mov [rdi+0x48],-1` (clear owner-tid), `SleepConditionVariableSRW`;
  on wake re-checks the `GetTickCount64-rsi` vs `r14` timeout, restores owner-tid. `rdi`=`std::mutex`(_Mtx_t),
  `r15`=`std::condition_variable`. It is TIMED (`wait_for`), so it wakes periodically and re-checks a predicate.
- **NOT the `std::call_once` guard `0x1c1e988`** (condvar 0x50c5fa8 / lock 0x50c5fb0). DIFFERENT object, condvar,
  lock — a genuine `std::condition_variable`. Concern "same-thread once-guard, not really stuck" is FALSIFIED
  by the body. Main is genuinely blocked in a real cv wait whose predicate stays false.
- **`0x1de92a0` = `condition_variable::wait(lock, pred)`** — the predicate loop. `0x1de92d0`:
  `movzx r8d, byte ptr [rbx+0x50]; test r8b,r8b; je done` — **the predicate is `*(bool*)(rbx+0x50)`** (a
  "ready/done" flag). Main loops: timed-wait → re-load `[rbx+0x50]` → still 0 → wait again. The 2 byte-identical
  samples = caught mid-`wait_for` with the flag still false.
- **`0x9ace14` = the OWNER routine — a "pump the shader system until ready" wait-loop** (`_condvar_owner.txt`).
  `rbx` = the object (cv at `[rbx+0xa8]` = `rsi`, ready-flag at `[rbx+0x50]`). `[rbp+0x20] = 0xC8` = a **200 ms**
  `wait_for` timeout. The loop (`0x9acea5 → 0x9aceba`, back-edge `je 0x9ace55`):
  ```
  0x9ace65  mov rcx,[0x547bb50]; call [rax+0x108]   ; poll the SHADER-MANAGER singleton for a work item -> rax
  0x9ace7a  if rax != 0: mov rcx,[0x492b8c8]; call [[rcx]+0x720] (dl=1)   ; KICK producer 0x492b8c8 with the item
  0x9acea0  call 0x9acf24                            ; re-acquire the _Mtx lock
  0x9aceb3  call 0x1de92a0                            ; condition_variable::wait_for(lock, 200ms, pred=[rbx+0x50])
  0x9aceb8  test al,al; je 0x9ace55                   ; predicate still false -> loop (unlock, re-poll, re-kick, re-wait)
  ```
  When the flag flips true → `0x9acecc: lea rcx,[rbx+0x140]; call 0xbb2830` (advance). Entry/exit read a clock
  singleton `[0x5166af8]` (slot `+0x30`) → this loop is TIMED/watchdogged. **The ready-flag never flips → Main
  pumps this loop forever** (200ms wait → re-poll → re-kick → re-wait), which is exactly why the 2 invasive
  samples are byte-identical (steady-state inside the same wait).

## DECISIVE: Main is blocked waiting for the SHADER SYSTEM to become ready

`[0x547bb50]` (the singleton Main polls each iteration, slot `+0x108`) is the **`CShaderMan` / shader-manager
singleton** — VERIFIED cross-reference: `ki0028-cshaderman-pso-consumer-recon/` established `DAT_18547bb50` as the
shader manager (it holds `"Shaders/Cache/D3D12/"` cache-path strings at `+0x1108`, cache-lookup state at
`+0xf00`/`+0xf8`). So the wait is:

> **Main (the boot sequencer) is stuck in a 200ms `condition_variable::wait_for` loop that polls the shader
> manager `[0x547bb50]` for a compile/PSO work item, kicks the async producer `[0x492b8c8]` (vtable `+0x720`) to
> build it, and waits on a ready-flag `[rbx+0x50]` that the producer never sets.**

This is THE mechanism behind the FULL-HANDOFF §9.4 static localization (`gfx_calls=1`, compile workers idle, engine
reaches the shader cache-version check and stops) and the §12.B "boot-phase sequencer stall" reframe — now
concrete: the sequencer is a shader-system-ready wait, and `[0x492b8c8]` never signals ready. `draw_indexed=0` /
black are downstream of THIS stall (Main never advances past shader readiness to build scene pipelines).

## The wait lives in CShaderMan code, coordinating with the renderer singleton (STATIC, verified)

Exact xref work (`disasm_condvar_wait.py` opcode-scan, not a heuristic):
- **The owner wait-loop `0x9ace14` is called from exactly ONE site: `0x9accd7`**, inside fn `0x9acc9c`.
- `0x9acc9c` is a **CShaderMan method**: its else-arm (`0x9accbf`) does `mov rcx,[0x547bb50]` (CShaderMan) +
  `mov [rcx+0x29c0], [0x492b9ac]`; its taken-arm (`cmp [rcx],0; jne 0x9accd7`) enters the wait. So the wait is
  reached from CShaderMan lifecycle code — Main blocks HERE waiting for shader-system readiness.
- **The sibling fn `0x9acc5c` (same cluster) dereferences the RENDERER singleton `[0x492b908]`** (the exact gate
  from Reframe 14 / the tick recon): `mov rcx,[0x492b908]; mov rax,[rcx]; call [rax+0x860]`. So this
  `0x9acbxx–0x9acexx` cluster is CShaderMan coordinating with BOTH the renderer `[0x492b908]` AND the producer
  `[0x492b8c8]`. This ties the condvar wait directly to the renderer-gate axis.
- `[0x492b8c8]` has **39 exact `mov r64,[rip]->0x492b8c8` reads** across .text with a large recurring vtable
  (slots +0x108/+0x210/+0x240/+0x2a0/+0x2e8/+0x3f0/+0x428/+0x430/+0x558/+0x690/+0x720) — a major engine
  interface, a sibling of the renderer `[0x492b908]` (both expose +0x108, the readiness-poll slot). No direct
  .text writer (gEnv-table install, like 0x492b908).

## PROBE Z9 (RUNTIME, DECISIVE) — the ready-flag FLIPS; this wait is per-frame, NOT the stall

Run `kcdx-dev_2026-07-03_10-16-32.log`, swap-ON (black arm, no `kcdx-noswap`). `producer_ready_probe.cpp` hooked
the wait-loop `0x9ace14`, captured the wait object, and a watcher polled `[wait_obj+0x50]` + `[0x492b8c8]`. The
static consumer-side theory (Reframe 16: "the producer never sets the ready-flag") was the thing under test.

**Result — the theory is FALSIFIED:**
- `PRODUCER_INSTALLED producer=0x...` — `[0x492b8c8]` non-null (producer IS installed).
- `READY_WAIT_ENTERED wait_obj=0x...` — the wait loop ran.
- **`READY_FLAG_SET wall_ms=7140`** — the ready-flag `[wait_obj+0x50]` **FLIPPED to non-zero 7.1 s in.** The
  shader system BECAME READY on the black arm.
- `READY_PROBE_VERDICT wait_entered=1 flag_ever_set=1 producer_installed=1 wait_enters=13981` — the loop was
  entered **13,981 times** over ~2 min ≈ per-frame cadence.

**What it means (pre-committed map: `entered + flag_set => shader-ready DOES fire; the stall is DOWNSTREAM`):**
- `0x9ace14` is a **PER-FRAME** shader-coordination wait that COMPLETES every frame (14k enters, flag flips) —
  NOT a one-shot boot gate that hangs. Reframe 15's "Main is STUCK" and Reframe 16's "producer never signals
  ready" are BOTH overturned.
- The two byte-identical invasive `~0 k` samples caught this RECURRING per-frame wait mid-block — the exact
  "per-frame trap" the FULL-HANDOFF §13 warns about (a fast per-frame Sleep/wait sampled twice reads as "stuck";
  PROBE M killed the same illusion for `0x869c39`). Two identical samples are NOT proof-of-stuck for a per-frame
  frame — they are proof the frame recurs at the sampled depth.
- **The CShaderMan condvar wait is EXONERATED as the wedge.** The shader system reaches ready; `draw_indexed=0`/
  black is downstream of (or unrelated to) this wait — and was never actually localized. This recon dir's whole
  "stuck-in-condvar" thesis was a sampling-artifact misread.

## The corrected frontier — the stall was NOT located; re-observe, do not theory-hop

Per results-driven (on disconfirmation, RE-OBSERVE ground truth — do not hop to the next theory):
- **What is genuinely still PROVEN** (from prior probes, unaffected by Z9): geometry buffers ARE created (Z8:
  `geo_buf=262`) but `draw_indexed=0`/`ia_set_ib=0`; present advances at ~310fps (frames presented black);
  the swap IS the differentiator (P-F). These stand.
- **What is now FALSE:** "Main is stuck in a condvar wait." Main is NOT stuck there — that wait completes per-frame.
- **The open question returns to its Reframe-13 form:** on a LIVE, PRESENTING game with geometry created, why is
  no index buffer ever BOUND (`ia_set_ib=0`)? The `0x1c1e7e0`/`0x9ace14` condvar axis is a dead end (per-frame,
  completes). The next observation must catch WHERE the indexed-draw recording is abandoned — NOT another wedged-
  stack frame (they recur per-frame). Candidate: instrument the render-item/draw-submission leaf that SHOULD call
  `IASetIndexBuffer`, swap-ON vs swap-OFF, to see whether the geometry is culled/skipped before binding vs the
  bind call itself is never reached. The drawcall_probe (`ia_set_ib`/`draw_indexed` counters) is the confirm signal.

## Files (co-located)
- `producer_ready_probe.{h,cpp}` (in `src/fs_takeover/`, UNCOMMITTED per no-residue) — PROBE Z9; falsified the
  condvar-stall thesis. Its finding is captured here; retire it (remove from source/seating/CMake) on next cleanup.
- `disasm_condvar_wait.py` → `_condvar_wait.txt` / `_condvar_callers.txt` / `_condvar_owner.txt` — the (correct,
  but now-exonerated) static read of the per-frame CShaderMan wait.
- `_invasive_allthreads_02-25.txt` / `_invasive_main_x2_identical.txt` — the 2 samples that were MISREAD as
  "stuck" (per-frame trap).
