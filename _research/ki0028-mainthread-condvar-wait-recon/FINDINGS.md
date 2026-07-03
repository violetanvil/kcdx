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

## The frontier (next probe)

What is the main thread waiting FOR at `WHGame 0x1c1e7e0`? Read its body (`/research-disassembly`): which
condvar/SRW address, what predicate. Then find who SIGNALS that condvar and whether the FS swap perturbs them.
Candidate producers: the worker-thread pool (many threads parked at nearest-export
`ffxFsr2GetUpscaleRatioFromQualityMode+0x152c749` → `+0x567a86` — resolve real RVAs from
`_invasive_allthreads_02-25.txt`), a GPU fence, or a load-completion signal.

## Files (co-located)
- `_invasive_allthreads_02-25.txt` — full `~* k 30` all-thread capture (256 threads) + WHGame ModLoad base.
- `_invasive_main_x2_identical.txt` — the two byte-identical `~0 k` main-thread samples (the stuck proof).
