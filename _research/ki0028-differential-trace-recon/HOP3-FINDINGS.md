# KI-0028 differential trace — HOP 3: the submit gate + the swap-ON draw-path attribution

**Date:** 2026-07-03 · **Status:** static DONE, runtime PENDING (both arms to run).
**Method:** static Ghidra decomp of FUN_180501cb0's 2 callers (`Ki28Hop3CallersDecomp.java` →
`_hop3_caller_a_1804ec3a0.txt`, `_hop3_caller_b_1805014a0.txt`) + PROBE Z10/S extensions.
**Trust:** primary (body reads).

## Static finding — BOTH callers gate the apply+submit call on the SAME three conditions

**Caller A — `FUN_1804ec3a0` (0x4ec3a0, 355 bytes, `void(ctx)`)** and
**Caller B — `FUN_1805014a0` (0x5014a0, 520 bytes, `u64(ctx)`)** both call
`FUN_180501cb0(ctx, DAT_1852b85c0)` only when ALL hold (read in both bodies):

```
[ctx+0x298] != [ctx+0x2a0]                              // render-item list NON-EMPTY
&& [ctx+0x178] != 0                                     // technique/pass obj present
&& *(char*)([ctx+0x178]+0x28) != 0                      // its +0x28 ready flag set
```

The item list `[ctx+0x298..0x2a0]` is the SAME list FUN_180501cb0 walks (HOP 2). Before the
submit, both build an 8-slot array from `[tech+0x30..]` (count at `[tech+0x50]`, aux at
`[tech+0x58]`) and call `FUN_1804ff8bc([DAT_1852b85c0+0x150], count, array, aux)` — a
bind-technique/set-state step — then `FUN_180501cb0`.

**The divert route (caller B, read in its body):** when `[ctx+0x2b0] != 0` AND
`*(int*)([[ctx+0x2b0]+0x1a8]+0x18f4) != 0` AND `*(char*)([ctx+0x2b0]+0x1a0) != 0`, caller B
calls `FUN_1825400d4((char*)([ctx+0x2b0]+0x1a0), ctx)` INSTEAD of the bind+submit pair — a
whole alternate submission route that never reaches FUN_180501cb0. Caller A has the analogous
early-out: `FUN_1804ec9d4([ctx+0x2b0]+0x1a0) != 0` → `FUN_1821f90f4()` → return.

Callers-of (next-hop targets if a pass never fires; full lists in the `_hop3_*` dumps):
- A: FUN_180779534, FUN_180779e44, FUN_18054fe28, FUN_180551bbc, FUN_180eab2d0, … (24 sites)
- B: FUN_1804ea7a0 (3 sites), FUN_1804eb2c8, FUN_1804ed320, FUN_1805507e0, … (24 sites)

## The HOP-3 probes (built + deployed, kcdx.dll hash-verified)

1. **PROBE Z10 extension — `pass_gate` samplers** at 0x4ec3a0 + 0x5014a0
   (`render_trace_probe.cpp`): at each pass entry, SEH-guarded reads of the gate variables —
   item count (`[0x298..0x2a0]`), tech obj (`[0x178]`), tech ready flag (`+0x28`), divert flag
   (`[0x2b0]+0x1a0`). First-6 per site logged (`pass_gate`); cumulative per-condition tallies
   every 3s (`pass_gate_tally`: invokes / list_empty / tech_null / tech_not_ready /
   divert_flag / gate_pass).
2. **PROBE S extension — Draw* caller attribution** (`drawcall_probe.cpp` +
   `draw_caller_tally.{h,cpp}`): `_ReturnAddress()` at the D3D12 DrawInstanced +
   DrawIndexedInstanced hooks → bounded unique-caller tables (module-relative RVAs).
   First sighting of each caller logs `draw_caller_first_seen`; per-caller counts dump
   every 10th watcher iteration + at watcher end (`draw_caller`). This names the engine fn
   issuing the 21011 swap-ON non-indexed draws MECHANICALLY (no static guessing).

## Pre-committed outcome map (flat — every outcome equally real)

Per arm, per pass caller (A and B independently):

- `invokes=0` swap-ON, `>0` swap-OFF → the pass itself never runs swap-ON → walk one more up
  (the `_hop3_*` caller lists are already on disk).
- `invokes>0` + `list_empty≈invokes` → the render-item list is never FILLED swap-ON →
  frontier moves to the enqueue side (who appends to `[ctx+0x298]`).
- `invokes>0` + `tech_null`/`tech_not_ready` dominant → the technique/pass object is
  missing/not-ready swap-ON → frontier: who sets `[ctx+0x178]` / its `+0x28` flag.
- `invokes>0` + `divert_flag` dominant (B) → the alternate route (FUN_1825400d4) is taken
  swap-ON → decompile that route next.
- `gate_pass>0` swap-ON yet `apply_invokes=0` → contradiction with HOP 2 → re-read
  (a mid-body branch between the gate and the call, or ctx mismatch).
- `draw_caller` swap-ON: the DrawInstanced caller-RVA set names the OTHER submit path —
  decompile the dominant caller next; diff vs the swap-OFF set (which should show the
  FUN_180501cb0 +0x60/+0x68 sites at ~0x501e90/0x501ed3-region RVAs if HOP 2 holds).

## RESULT — swap-ON arm (RAN 2026-07-03; logs `11-40-36` + module-attributed rerun `11-48-03`)

**The engine issues ZERO draws of any kind swap-ON. The "vertex-only rendering" (HOP 2's
draw_instanced≈21k) is the STEAM OVERLAY, not the engine — REFRAMED.**

| swap-ON | value | meaning |
|---|---|---|
| pass A (0x4ec3a0) invokes | **0** | pass A never runs |
| pass B (0x5014a0) invokes | 3887 | runs per frame |
| pass B list_empty | **3887/3887** | the render-item list `[ctx+0x298]` is NEVER populated |
| pass B tech_not_ready | 3886 (tech ptr non-null after inv 0) | technique `+0x28` flag never set |
| pass B divert_flag / gate_pass | 0 / 0 | alt route never taken; submit never allowed |
| draw_instanced total | 22476 | ALL from ONE caller |
| that caller | **gameoverlayrenderer64.dll + 0x757A6** (mod_off 481190) | the Steam overlay's own per-frame UI pass |
| engine draws (any kind) | **0** | draw_indexed=0, ia_set_ib=0, no WHGame Draw* caller ever seen |

So the black screen is literally an empty backbuffer presented at ~310fps with the overlay
composited on top. The divergence is fully UPSTREAM of submission: **swap-ON, no render item
is ever ENQUEUED into `[ctx+0x298..0x2a0]` (and the pass technique `[ctx+0x178]+0x28` is
never marked ready).** CCRO compile-pass sites fired on both arms in Step 3 — compiled
objects exist; they are never turned into enqueued render items for this pass.

## RESULT — swap-OFF arm (RAN 2026-07-03, log `12-02-52`) + the HOP-3 DIFF

| | swap-OFF (menu) | swap-ON (black) |
|---|---|---|
| pass A (0x4ec3a0) invokes / gate_pass | **3952 / 3952 (100%)** | **0 / 0 — NEVER CALLED** |
| pass B (0x5014a0) invokes | 1976 — blocked (tech_not_ready 1975/1976) | 3887 — blocked (list_empty 3887/3887) |
| engine indexed draws | 59913 — ALL from WHGame+0x5023BC | 0 |
| engine instanced draws | 15100 — ALL from WHGame+0x502420 | 0 |
| overlay draws (gameoverlayrenderer64.dll+0x757A6) | 6580 | 22476 (the ONLY draws) |

Both WHGame draw return-sites (0x5023BC / 0x502420) are INSIDE FUN_180501cb0 — runtime
confirmation of the HOP-2 static read: every engine draw (indexed AND non-indexed) is issued
by the ONE apply+submit fn, driven by pass A.

**HOP-3 conclusion (mechanical, both-arm):**
1. **Pass A (FUN_1804ec3a0) is the sole real submit driver** — ~1/frame swap-OFF, gate
   passes 100%; **swap-ON it is NEVER INVOKED.** The divergence is who-calls-pass-A.
2. **Pass B is a red herring** — its technique is never ready even on the WORKING arm; it
   submits nothing on either arm.
3. **Swap-ON the engine records ZERO draws.** The HOP-2 "vertex-only rendering" framing is
   dead: the ~21k draw_instanced were the STEAM OVERLAY's per-frame pass.

## Next hop (HOP 4)

Pass A has 24 static call sites (list above / `_hop3_caller_a_1804ec3a0.txt`). Which drives
it live, and why it stops swap-ON: `_ReturnAddress` capture at pass A/B entry (reuse
draw_caller_tally) → swap-OFF names the live caller site(s) → decompile that caller, probe
ITS gate on both arms. (No caller inference from the static list — AP19; capture, then read.)
