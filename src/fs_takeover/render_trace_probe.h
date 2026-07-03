// === DIAGNOSTIC (PROBE Z10) — KI-0028 ordered differential render-submission trace ===
#pragma once

// WHY (KI-0028 METHOD RESET — _research/ki0028-differential-trace-recon/DESIGN.md):
// ~50 spot-check probes each asked "is it THIS gate?" and dead-ended; the sequence was
// theory-hopping. The decisive artifact is the ORDERED CALL SEQUENCE on the render-
// submission path, captured on BOTH arms (swap-OFF menu = GOOD, swap-ON black = BAD),
// diffed to name the FIRST divergence. The divergence IS the answer, no theory.
//
// Step 1 (static Ghidra, _research/ki0028-differential-trace-recon/FINDINGS.md) named
// the real adjacent edges. This probe instruments them, in dependency order, each
// after-hook emitting a SEQUENCED record (RENDER_TRACE seq=N site=<id> ...) so an
// offline diff aligns the two arms and names the first (seq,site) that differs.
//
// The 5 ordered sites (scratch RVAs — diagnostic observation of THIS boot's ground
// truth, NOT resolved-by-name targets; same scratch-RVA shape as PROBE Z9/Y/S, not an
// AP1 hardcoded resolution). Image base 0x180000000; RVAs are module-relative:
//   1. 0x86b574  render-STAGE sequencer (FUN_18086b574) — arg2 = the stage id; stage 4
//                runs the compile pass. Names whether swap-ON reaches the compile/draw
//                stages at all, or the stage machine stops earlier.
//   2. 0x429384  per-frame CCRO COMPILE pass (FUN_180429384) — the render-object
//                compile loop that produces the compiled objects the draw loop submits.
//   3. 0x429794  CCRO::Compile (FUN_180429794) — per-object PSO build; RETURN captured
//                (0 = "Compile failed, PSO creation failed" → no compiled object).
//   4. 0x5025b4  ★ engine SetIndexBuffer (FUN_1805025b4) — the DECISIVE leaf: its body
//                holds "Trying to set invalid index buffer" and ends in the indirect
//                D3D12 IASetIndexBuffer (cmdlist vtbl +0x158, = the slot-43 the drawcall
//                probe hooks). _ReturnAddress() captured → names WHICH of its 6 callers
//                fired. ia_set_ib=0 swap-ON ⇒ this site never reached, or its caller
//                never fired.
//   5. 0x777f6c  render-thread command FLUSH (FUN_180777f6c) — top of the submit path.
//   6. 0x501cb0  HOP-2: the per-item apply+submit fn (IB-field sampler).
//   7. 0x4ec3a0  HOP-3: pass caller A of the apply+submit fn — submit-gate sampler.
//   8. 0x5014a0  HOP-3: pass caller B (same gate + a divert route to 0x25400d4).
//
// PER-FRAME-SAFE (the Z9 lesson — a per-frame site sampled twice reads as "stuck"):
// each site latches its FIRST kMaxPerSite fires only, so 14k per-frame repeats do not
// bury the first-frame divergence. The sequence number is global + monotonic across
// sites so the offline diff sees the true interleaved order.
//
// OUTCOME → MEANING (theory-independent — the sequence is ground truth; A/B diff):
//   a site fires swap-OFF but is ABSENT swap-ON            → the divergence is AT that
//                                                            site's gate (walk its caller).
//   site 4 (SetIndexBuffer) fires swap-OFF, absent swap-ON → confirm the ia_set_ib=0
//                                                            symptom at the leaf; the
//                                                            _ReturnAddress diff names the
//                                                            caller that stopped firing.
//   all sites fire on BOTH arms, same order                → the divergence is DEEPER than
//                                                            these 5 edges (inside a callee /
//                                                            in the arg values) — read the
//                                                            captured arg summaries.
//   site 3 (Compile) RETURNS 0 swap-ON, non-0 swap-OFF     → CCRO::Compile fails swap-ON
//                                                            (no compiled object → nothing to
//                                                            draw); walk why Compile returns 0.
//
// Armed in seating_hook.cpp BEFORE the kcdx-noswap early-return (the PROBE W/K/S/Y A/B
// pattern) so swap-ON and swap-OFF instrument the same phase. Brackets the live
// drawcall_probe (ia_set_ib / draw_indexed) as the confirm signal.
//
// NO-RESIDUE: on retirement capture the finding + this wiring to _research/probe-archive/
// then REMOVE from live source (file + seating arm + CMakeLists). Greppable tag:
// "RENDER_TRACE".

namespace kcdx::fs_takeover {

// Arm PROBE Z10 once. Idempotent. Installs MinHook after-hooks at the 5 render-
// submission RVAs above and emits sequenced RENDER_TRACE records (first kMaxPerSite
// fires per site) to the dev log for the offline swap-ON vs swap-OFF diff.
void RenderTraceProbeStart();

}  // namespace kcdx::fs_takeover
