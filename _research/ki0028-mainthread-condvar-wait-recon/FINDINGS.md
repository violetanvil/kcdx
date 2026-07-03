# KI-0028 — the MAIN thread is STUCK in a condvar wait mid-tick (invasive, 2 identical samples)

**Date:** 2026-07-03 · **Method:** invasive cdb (`-p 42364`, `qd`-detached — game left running) on the live
black-screen process + PROBE Z8 resource-creation counts (run `kcdx-dev_2026-07-03_02-25-43.log`).
**Trust:** primary evidence — two `~0 k` samples byte-identical (same Child-SP), RVAs resolved against WHGame
base `0x7ffdf45b0000`.

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

## The frontier (next step) — why does producer 0x492b8c8 never signal ready swap-ON?

**Static has taken this as far as it productively goes** (results-driven §4): the CONSUMER side is fully read
(CShaderMan `condition_variable::wait_for` on `[rbx+0x50]`, kicking `[0x492b8c8]+0x720`). Whether the producer's
job runs/completes swap-ON vs swap-OFF is a RUNTIME fact — a `/debug` A/B probe, not a static read. The open
question narrows to the PRODUCER side:
1. Identify subsystem `[0x492b8c8]` (gEnv-table, same `0x492bxxx` family as renderer `0x492b908`) — likely the
   async shader-compile / render-job dispatcher. Read its vtable-`+0x720` callee (the kick) + who sets `[rbx+0x50]`.
2. Determine what the FS swap perturbs such that the kick is issued but the ready-flag never flips: does the kick
   enqueue a job that a worker never runs, does a shader-cache read the producer needs return differently swap-ON,
   or is `[rbx+0x50]`'s producer path gated on state the swap leaves unset?
3. This is now closer to a runtime `/debug` probe than a static read: after-hook `[0x492b8c8]+0x720` (kick fires?)
   + watch `[rbx+0x50]` writers, swap-ON vs swap-OFF. Static owes: name `0x492b8c8`'s type + read the `+0x720`
   body + the `+0x50` writer.
Candidate producers: the worker-thread pool (parked at nearest-export
`ffxFsr2GetUpscaleRatioFromQualityMode+0x152c749` → `+0x567a86` — resolve real RVAs from
`_invasive_allthreads_02-25.txt`), a GPU fence, or a shader-compile-completion signal.

## Files (co-located)
- `disasm_condvar_wait.py` → `_condvar_wait.txt` (the wait routine `0x1c1e78c`), `_condvar_callers.txt` (the L1/L2/L3
  caller chain), `_condvar_owner.txt` (the owner pump-loop `0x9ace14`).
- `_invasive_allthreads_02-25.txt` — full `~* k 30` all-thread capture (256 threads) + WHGame ModLoad base.
- `_invasive_main_x2_identical.txt` — the two byte-identical `~0 k` main-thread samples (the stuck proof).
