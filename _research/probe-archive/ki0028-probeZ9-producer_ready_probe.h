// === DIAGNOSTIC (PROBE Z9) — KI-0028 shader-system ready-flag ground truth =====
#pragma once

// WHY (KI-0028 Reframe 16): the 2 byte-identical invasive samples proved Main is
// parked in a std::condition_variable::wait_for at WHGame 0x1c1e7e0. Static disasm
// (_research/ki0028-mainthread-condvar-wait-recon/) read the CONSUMER side fully:
//   - the owner pump-loop 0x9ace14 polls CShaderMan [0x547bb50] for a work item,
//     kicks producer [0x492b8c8] (vtable +0x720), and waits 200ms on a ready-flag
//     pred=*(bool*)(wait_obj+0x50), looping until the flag flips.
//   - the flag never flips swap-ON -> Main pumps forever -> the shader system never
//     becomes ready -> scene pipelines never build -> draw_indexed=0 / black.
// The PRODUCER side (does the kick fire? does the flag ever get set? is the producer
// even installed?) is a RUNTIME fact static cannot settle (gEnv-table vtable). This
// probe observes that ground truth directly.
//
// OBSERVATION (theory-independent, one variable = "does the shader-ready flag flip
// on this arm?"): hook the wait-loop entry 0x9ace14 (a clean function prologue,
// MinHook-safe — NOT the hot tick). On first entry capture rcx = the wait object.
// A watcher thread (PROBE K/S/Y Sleep-cadence shape, no hot hook) then reads:
//   - [0x492b8c8]  -> producer singleton installed? (null vs non-null)
//   - [wait_obj+0x50] -> the ready-flag, polled until it flips or the run ends.
//
// OUTCOME -> MEANING (swap-ON black arm; A/B against swap-OFF menu arm):
//   never-entered            -> Main's stuck wait is a DIFFERENT instance; re-attribute.
//   entered, flag FLIPS      -> shader-ready DOES fire; the stall is DOWNSTREAM of
//                               this wait (not this producer). Move the frontier past it.
//   entered, flag stays 0,
//     producer NON-NULL      -> CONFIRMED: producer installed but never signals ready;
//                               the swap derails its COMPLETION. Probe the +0x720 kick
//                               / the +0x50 writer next.
//   entered, flag stays 0,
//     producer NULL          -> the producer singleton itself was never installed
//                               swap-ON; walk who installs [0x492b8c8].
// A/B: on swap-OFF (kcdx-noswap / menu) the flag SHOULD flip (menu reaches ready) —
// that arm confirms the probe reads the right flag and the wait is real.
//
// Armed in seating_hook.cpp BEFORE the kcdx-noswap early-return (the PROBE W/K/P A/B
// pattern) so swap-ON and swap-OFF instrument the same phase.
//
// NO-RESIDUE: on retirement capture the finding + this wiring to
// _research/probe-archive/ then REMOVE from live source (file + seating arm +
// CMakeLists). Greppable tag: "READY_PROBE".

namespace kcdx::fs_takeover {

// Arm PROBE Z9 once. Idempotent. Installs a MinHook at WHGame 0x9ace14 (the
// CShaderMan ready-wait loop entry) to capture the wait object, then starts one
// Sleep-cadence watcher thread that samples the producer singleton + the ready-flag
// and logs a terminal verdict (flag flipped / stayed 0 + producer null-ness), then
// self-exits. A boot whose wait never runs logs a distinguishable never-entered line.
void ProducerReadyProbeStart();

}  // namespace kcdx::fs_takeover
