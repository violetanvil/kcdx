// === DIAGNOSTIC (PROBE Z10 / KI-0028 HOP 8) — render command-stream interpreter ===
#pragma once

// WHY (KI-0028 differential trace, HOP 8): HOP 6 proved the pass-A render-item list is
// empty every frame swap-ON while the render machinery runs. HOP 7 (static, _hop7c) found
// the driver is FUN_18251bb1c, a typed-opcode COMMAND-STREAM interpreter (cursor [p2+0x8],
// length [p2+0x10], base [p2+0x18]; switches on a u32 opcode, cases 1..0x1d; opcode 4 =
// the pass-A submit). This probe hooks it at entry and logs the stream length + first
// opcode, both arms — decomposing "no items" into empty-stream (the producer upstream) vs
// same-stream-missing-build-opcodes. Its own unit (one-file-one-concern). Consumed by the
// PROBE Z10 arming in render_trace_probe.cpp. NO-RESIDUE on retire.

namespace kcdx::fs_takeover {

// Arm the HOP-8 command-stream interpreter hook at whgameBase + 0x251bb1c. Returns 1 if
// armed, 0 on failure (logged). MinHook must already be initialized (Z10 does it).
int CmdStreamProbeArm(unsigned long long whgameBase);

// Emit the cumulative stream tally (invokes / len==0 count / max / last) — called from the
// Z10 watcher every few seconds (event-driven, off the hot path).
void CmdStreamProbeEmitTally();

}  // namespace kcdx::fs_takeover
