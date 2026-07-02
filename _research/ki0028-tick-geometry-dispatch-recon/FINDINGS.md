# KI-0028 Measurement 2 — the tick's per-frame render-dispatch gate (static, AP19)

**Date:** 2026-07-02
**Method:** static disassembly of WHGame.dll (release_1_5_1164953_841), image base 0x180000000. No launch.
**Trust:** primary evidence — every "gate at 0x… tests [global]" claim is a body read with the cited site. Runtime null-ness swap-on/off is explicitly NOT static — marked as the /debug probe target.
**Reuse:** extends `_research/ki0028-window-exit-gate-recon/` (which exonerated the window loop + focus poll and named 0x667b24 the tick dispatcher) and PROBE Y's captured Main stack (frames 0x532FB5→0x6678A0→0x667DE2 inside/above the tick).

## TL;DR

1. **The tick dispatcher fn 0x667b24 runs the per-frame work as a flat sequence of conditional vtable dispatches** on the CSystem `this` (`rsi`) and on `.data` singletons. PROBE Y captured Main at 0x667DE2 — right after the focus-poll call (0x667ddd), mid-tick. Confirms Main runs the full per-frame tick (not parked), as PROBE M / the window-exit recon already established.

2. **The tick's ONE per-frame RENDER-dispatch gate is `0x667ed0`:** `cmp qword ptr [0x492b908], r14 ; je 0x667f84` (r14 = 0). When `0x492b908` is null, the tick JUMPS OVER the block 0x667ed0..0x667f84. The block's 5 gated calls split by receiver (gated-verified): 3 dispatch through the **singleton object** `[[0x492b908]]` — `[+0x240]` (0x667eff), `[+0x250]` (0x667f12), `[+0x248]` (0x667f24); 2 dispatch through the CSystem `this` (`rsi`) — `[rsi-obj+0x430]` (0x667ee3), `[rsi-obj+0x428]` (0x667f75). With `ucomiss` float compares + a resolution/viewport-change path (fn 0x5392fc @ 0x667f66). So the gate is "if the renderer singleton exists, run this per-frame render/present block (touching both the singleton and CSystem)." The slot spread + float/resolution args characterize a **renderer / 3DEngine interface** (IRenderer / I3DEngine-class), not a config object. [Gated body-read verifier: PROCEED — cmp reads 0x492b908, je→0x667f84, all 5 offsets present, zero direct .text writers (738 refs, all reads/cmps).]

3. **`0x492b908` is a runtime gEnv-table singleton — NO direct .text writer** (`identify_492b908.py`: scanned every `mov qword [rip->0x492b908], reg` in .text = 0 hits). It is installed via a base+offset pointer table (the same gEnv-style mechanism the window-exit recon found for the guard singletons 0x492b8xx). This is the SAME statically-unresolvable wall FINDINGS §Boundary hit: static cannot read whether the pointer is null at runtime.

4. **The other tick gates are cvars, not object pointers.** The `0x4927xxx` gates (0x667c80/0x667d7d/0x668460/0x6685a6/0x66867d/0x668843/0x668963/0x668abb/0x668ca4) are `dword` reads (config values). The `0x56632xx` gates (0x667fc0/0x667fcd) sit on the input/key path (adjacent to GetKeyState). Only `0x492b908` is the per-frame renderer-singleton gate.

## The Measurement-2 chain link this settles vs. leaves open

- **SETTLED (static):** WHERE the tick decides to dispatch per-frame renderer work = the `0x667ed0` gate on `[0x492b908]`. If `0x492b908` is null swap-ON, the entire renderer-dispatch block is skipped every frame — a mechanism consistent with present-advances-but-draw_indexed=0 (the compositor/UI present path is elsewhere; the scene/renderer submission is gated here).
- **OPEN (runtime — the /debug probe, chain link 2 "the wrong value"):** is `[0x492b908]` actually null swap-ON and non-null swap-OFF? Static cannot answer (gEnv-table pointer). This is the next probe: read `[0x492b908]` swap-on vs swap-off (an in-process read-only probe at tick entry, or a cdb `dq 0x492b908` on both arms). Outcome map:
  - null swap-ON, non-null swap-OFF → the renderer singleton was never installed swap-ON → walk to WHO installs 0x492b908 (link 4) and why the swap derails it (link 5).
  - non-null in BOTH → this gate is NOT the differentiator; the block runs swap-ON too and draw_indexed=0 originates DEEPER (inside one of the gated slot calls, or the geometry command is built further down). Re-frame to the slot callee.
  - This is a discriminating probe (both outcomes inform), theory-independent (reads the raw pointer), one variable.

## Boundary (results-driven §4)

Static identified the tick's render-dispatch gate + its singleton + classified it renderer-class + proved it has no static writer (gEnv-table). What static CANNOT settle: the runtime null-ness of `0x492b908` swap-on/off, and (if non-null both) which gated slot call omits the geometry command. Both are the live probe now owed. This does NOT claim `0x492b908` is null swap-ON — it names it the one gate whose null-ness would produce the measured symptom, to be confirmed or killed by the probe, not asserted.

## Worker scripts (co-located)
- `disasm_tick_frames.py` → `_tick_frames.txt` (full tick 0x667b24 + PROBE Y frame chain 0x6678a0 / 0x532fb5)
- `identify_492b908.py` → the no-static-writer result (gEnv-table install)
