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

### HOPS 3-6 DONE (this session, all committed — latest `dbee85b`). See HOP3-FINDINGS.md + HOP4-FINDINGS.md.

The divergence walked cleanly UP the render-submission → enqueue chain:
- **HOP 3:** both submit-fn callers (pass A `FUN_1804ec3a0`, pass B `FUN_1805014a0`) share a
  3-condition gate; the 21011 swap-ON "draws" were the **Steam overlay**
  (gameoverlayrenderer64.dll+0x757A6), NOT the engine. Engine records ZERO draws swap-ON.
- **HOP 4/5:** pass A is the SOLE real submit driver (all 59913 indexed + 15100 instanced draws
  swap-OFF, via FUN_180501cb0). Its live caller is the dispatcher `FUN_180779534`; pass B's caller
  is the Bink video player (dropped). The dispatcher gates the pass A call on the render-item list
  `[obj+0x298..0x2a0]` being non-empty (obj = `[FUN_180779534.param_1 + 0x378]`).
- **HOP 6 (DECISIVE):** swap-ON the dispatcher FIRES 6722× (obj non-null) but the render-item list is
  EMPTY on 6722/6722 entries. The render-pass machinery is intact; **the geometry is never ENQUEUED.**

### HOP 7 DONE (committed `6b65d08`; see HOP7-FINDINGS.md)

Pass A's driver `FUN_18251bb1c` is a **render command-stream INTERPRETER** — walks a typed-opcode
byte buffer (cursor `[p2+0x8]`, len `[p2+0x10]`, base `[p2+0x18]`; switch on a u32 opcode, cases
1..0x1d). **Opcode 4 = the pass-A submit.** The item-build is other opcodes earlier in the SAME
stream; the whole stream is produced upstream (`FUN_18252a228` up) and is what the swap perturbs.

### The current frontier (HOP 8 — probe BUILT + DEPLOYED, needs BOTH arms)

**Is the render command stream EMPTY swap-ON, or the same stream missing the item-build opcodes?**
HOP-8 probe (`cmd_stream_probe.{h,cpp}`, wired into PROBE Z10, deployed hash-verified, dev mode on):
hooks the interpreter at entry, logs `cmd_stream` (len + first_op, first 6) + `cmd_stream_tally`
(invokes / len_zero / len_max / len_last every 3s). **CURRENTLY ARMED swap-ON (marker removed).**
Pre-committed map (in HOP7-FINDINGS §"Next probe"):
- len=0 every frame swap-ON → stream empty → frontier = the stream PRODUCER (scene→command-buffer).
- len>0 swap-ON → stream present → cross-check opcode-4 count (= `passa_dispatch` invokes) vs the
  build opcodes.

Run order: swap-ON first (unknown), then swap-OFF baseline (set `kcdx-noswap`). Diff `cmd_stream_tally`.

### Ghidra RUN RECIPE (two traps burned hours — do NOT repeat)
- A whole-module `getFunctions()` + decompiler scan HANGS headless (2 java procs stuck for hours,
  never wrote a byte). Use TARGETED single-function decomp only.
- The **bash tool mangles the spaced `analyzeHeadless.bat` path** (`'C:\Users\Michael\Documents\KCD2'
  is not recognized`). Run via PowerShell `Start-Process -FilePath <bat> -ArgumentList ... -NoNewWindow
  -PassThru` then `$p.WaitForExit(300000)` (kill on timeout), FOREGROUND — a hang trips the timeout
  instead of stalling on a background-completion notification that never fires.

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
