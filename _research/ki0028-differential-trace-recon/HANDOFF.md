# KI-0028 differential trace — HANDOFF (2026-07-03, resume point)

## Where we are

The METHOD RESET (stop spot-checking → build an ordered differential trace) is WORKING and has walked
the divergence down TWO hops. All committed on `main` (latest `3347613`).

### Proven so far (the trace's mechanical findings — do NOT re-litigate)

1. **Step-1 static (Ghidra):** `FUN_1805025b4 @ 0x5025b4` IS the engine `SetIndexBuffer` (its body holds
   "Trying to set invalid index buffer" + the indirect D3D12 IASetIndexBuffer call, cmdlist vtbl +0x158
   = the slot-43 the drawcall_probe hooks). AP19 trap avoided: the `DRAWINDEXEDINSTANCED` string is a
   PIX-name table (FUN_1825381d0) in the DRED debug dumper, NOT the draw.
2. **Step-3 trace (PROBE Z10):** engine SetIndexBuffer fires 6× swap-OFF (caller 0x501ebe), **0× swap-ON**.
   D3D12 confirm: `ia_set_ib=26056` swap-OFF vs `0` swap-ON. NEW: the black arm is NOT idle — it renders
   VERTEX-ONLY hard (draw_instanced ~19-21k > menu's ~3.6k, geo_buf=264) but binds zero index buffers.
3. **HOP-2 (this session, DECISIVE):** `FUN_180501cb0` (0x501cb0, the swap-OFF SetIndexBuffer caller =
   the per-item apply+submit fn) fires **52764× swap-OFF (ib_set=842142, 77% indexed items) but 0× swap-ON.**
   The null-IB hypothesis was FALSIFIED — the whole FUN_180501cb0 indexed-capable submit path is BYPASSED
   swap-ON; the 21011 non-indexed draws come through a DIFFERENT path.

### The current frontier (the next hop — NOT yet done)

**Why is `FUN_180501cb0` never called swap-ON, and which OTHER path issues the 21011 non-indexed draws?**
Its 2 static callers (from `_hop_indexed_caller_180501cb0.txt`): `FUN_1804ec3a0` (0x4ec3a0),
`FUN_1805014a0` (0x5014a0) — one render pass is skipped swap-ON. Next hop:
- Trace those 2 callers swap-ON vs swap-OFF: does a caller fire but not reach FUN_180501cb0 (branch
  inside gates it), or never fire (walk one more up)?
- Separately, identify the swap-ON non-indexed draw path (hook DrawInstanced's engine-side caller — a
  distinct submit fn) to see what IS rendering.
Same differential method: static-decompile the callers → extend render_trace_probe with hooks on them →
run both arms → diff.

## Files to re-read on resume (in order)

1. `_research/ki0028-differential-trace-recon/DESIGN.md` — the trace spec + why spot-checking failed.
2. `_research/ki0028-differential-trace-recon/HOP2-FINDINGS.md` — the latest RESULT (top) + the
   FUN_180501cb0 static read (bottom) + the next-probe candidates.
3. `_research/ki0028-differential-trace-recon/FINDINGS.md` — Step-1 static (the named edge list) +
   Step-3 trace RESULT.
4. `_research/ki0028-differential-trace-recon/_hop_indexed_caller_180501cb0.txt` — FUN_180501cb0's body
   + its 2 callers (the next-hop targets) + `_hop_caller_up_1805029f0.txt`.
5. `src/fs_takeover/render_trace_probe.{h,cpp}` — PROBE Z10 (the live tracer; extend it for the next hop).
6. `docs/known-issues/KI-0028-...md` Correction 7 — the trail entry for the Step-3 divergence.

## Live state

- PROBE Z10 (render_trace_probe) is DEPLOYED (kcdx.dll hash-verified in the live install), dev mode ON.
- Currently swap-ON arm (kcdx-noswap marker ABSENT). To run the control arm, set the marker; to run the
  repro, remove it. Agent sets up the marker + build/deploy; user only launches (agent-builds-and-deploys).
- Reuse-first: the Ghidra project is at `third-party-ghidra/ghidra_project/KCD2.rep`; the decomp-script
  pattern is `third-party-ghidra/ghidra_scripts/Ki28*Decomp.java` (run headless via analyzeHeadless.bat,
  write output to a file — Ghidra's logger collapses multi-line println).
- Z9 (producer_ready_probe) is RETIRED + archived to `_research/probe-archive/ki0028-probeZ9-*`.

## Method reminders (the traps this investigation already hit)

- Per-frame sites sampled twice read as "stuck" (the Z9/PROBE-M trap). The tracer latches first-N fires/site.
- String anchors on D3D12 command names hit the DEBUG/DRED path, not the hot draw (AP19). The hot draw is
  an indirect vtable call with no string.
- A falsifiable probe that KILLS its hypothesis (like HOP-2) is a WIN — it narrows the frontier cleanly.
  Do not theorize the next step; observe it (results-driven). AP17 mechanism paragraph owed before closure.
