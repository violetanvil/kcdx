# KI-0028 differential trace — HANDOFF (2026-07-03, resume point)

## Where we are

The METHOD RESET (stop spot-checking → build an ordered differential trace) is WORKING and has walked
the divergence down to the ENQUEUE frontier (HOP 8 done). HOP 1-7 committed on `main`; HOP 8 is the
newest work (committing now). Resume point: **HOP 9 — the item-append leaf (see §"current frontier").**

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

### HOP 7 DONE (committed `6b65d08`; see HOP7-FINDINGS.md) — but its interpreter lead is REFUTED by HOP 8

HOP 7 (static) named pass A's driver `FUN_18251bb1c` as a render command-stream INTERPRETER (typed-opcode
byte buffer; opcode 4 = pass-A submit). **HOP 8 (live, both arms) KILLED this:** the interpreter fired
invokes=0 on BOTH arms — it is NOT the hot render-command driver. Do NOT build on the interpreter model.
The static edge (interpreter → opcode-4 → dispatcher) was an unread LIVE edge; the run refuted it.

### HOP 8 DONE (this session, NOT yet committed) — see HOP8-FINDINGS.md. The frontier is now CLEAN.

The HOP-8 probe (cmd_stream, interpreter arm) fired invokes=0 both arms → REFUTED HOP 7's interpreter.
But the SAME-run dispatcher cross-check nailed the whole KI-0028 divergence in one row:

| `passa_dispatch_tally` (dispatcher `FUN_180779534`) | swap-ON | swap-OFF |
|---|---|---|
| invokes | 13350 | 1592 |
| list_empty | 13350 (ALL) | 0 (list FILLED) |
| would_call (pass A called) | 0 | 1592 |

**The renderer + dispatcher are INTACT both arms; the FS-takeover swap breaks the ENQUEUE of items into
the dispatcher's render-item list `[obj+0x308(begin)..0x310(end)]`** (obj = `[dispatcher.param_1+0x378]`).
Filled swap-OFF → menu renders; empty swap-ON → black. The interpreter detour is CLOSED.

The HOP-8 cmd_stream probe is RETIRED (dead lead): archived to
`_research/probe-archive/ki0028-hop8-cmd_stream_probe.{cpp,h}` (with verdict header), removed from
`src/fs_takeover/` + CMake + the 3 Z10 wiring points; build green + deployed (hash-verified). The Z10
tracer (render_trace_probe.cpp — HOP 2-6 arms) STAYS LIVE; HOP 9 extends it.

### The current frontier (HOP 9 — the item-enqueue leaf that fills [obj+0x308..0x310])

**What appends items to `[obj+0x308..0x310]` swap-OFF, and why does the FS swap starve it swap-ON?**
The dispatcher CLEARS the vector inline and READS begin/end to walk; the APPEND (push-back writing
`+0x310` end-ptr) is a SEPARATE leaf, called earlier in the frame on `obj`. HOP 7's 7b module-wide
append-scan HUNG (whole-module decompile trap) so it was never named. HOP 9:
1. **Reuse-first:** re-read `_hop7b_append_scan.txt` + `_hop_caller_up_1805029f0.txt` for any partial.
2. **Targeted decomp ONLY** (single-function; NEVER whole-module): find the fn writing `[obj+0x310]`
   (end-ptr advance) near reads of `[obj+0x308]` = the push-back into the pass-item vector.
3. **Live arm** on that leaf, both arms: fires+fills swap-OFF, absent/early-returns swap-ON → confirmed;
   then walk ONE up to the swap-perturbed scene/visibility producer.

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
5. **`_research/ki0028-differential-trace-recon/HOP8-FINDINGS.md` — the newest RESULT (the enqueue-list
   differential + the refuted interpreter + the HOP-9 plan). READ THIS FIRST on resume.**
6. `src/fs_takeover/render_trace_probe.{h,cpp}` — PROBE Z10 (the live tracer; HOP 2-6 arms; extend for HOP 9).
7. `_research/ki0028-differential-trace-recon/_hop7b_append_scan.txt` + `_hop_caller_up_1805029f0.txt` —
   the STALLED append-leaf scan HOP 9 resumes (reuse-first before re-running Ghidra).
8. `docs/known-issues/KI-0028-...md` CORRECTION 8 — the trail entry for the HOP 6-8 result.

## Live state

- PROBE Z10 (render_trace_probe, HOP 2-6 arms) is DEPLOYED (kcdx.dll hash `859F24BC…A030D314`, verified in
  the live install), dev mode ON. The HOP-8 cmd_stream arm is RETIRED (removed from source + CMake, archived).
- Currently **swap-ON arm (kcdx-noswap marker ABSENT — neutral/default).** To run the control arm, set the
  marker; to run the repro, remove it. Agent sets up the marker + build/deploy; user only launches.
- Reuse-first: the Ghidra project is at `third-party-ghidra/ghidra_project/KCD2.rep`; the decomp-script
  pattern is `third-party-ghidra/ghidra_scripts/Ki28*Decomp.java` (run headless via analyzeHeadless.bat,
  write output to a file — Ghidra's logger collapses multi-line println).
- Retired+archived probes: Z9 (`ki0028-probeZ9-*`), HOP-8 cmd_stream (`ki0028-hop8-cmd_stream_probe.{cpp,h}`).

## Method reminders (the traps this investigation already hit)

- Per-frame sites sampled twice read as "stuck" (the Z9/PROBE-M trap). The tracer latches first-N fires/site.
- String anchors on D3D12 command names hit the DEBUG/DRED path, not the hot draw (AP19). The hot draw is
  an indirect vtable call with no string.
- A falsifiable probe that KILLS its hypothesis (like HOP-2) is a WIN — it narrows the frontier cleanly.
  Do not theorize the next step; observe it (results-driven). AP17 mechanism paragraph owed before closure.
