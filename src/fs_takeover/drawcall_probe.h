// === DIAGNOSTIC (PROBE S) — KI-0028 command-list DRAW recording =================
#pragma once

#include <cstdint>

namespace kcdx::fs_takeover {

// Arm PROBE S: the D3D12 command-list draw-recording tracer. Idempotent.
//
// WHY (KI-0028, the premise-overturn — _research/ki0028-cshaderman-pso-consumer-
// recon/FINDINGS.md): the whole shader/PSO axis is EXONERATED — PSO creation is
// IDENTICAL swap-ON vs swap-OFF (PROBE P: gfx_calls=1 on the WORKING menu AND the
// black screen), and present succeeds both (PROBE K: 120fps, GPU scanout). So the
// black-vs-menu divergence is NEITHER PSO-create NOR present — it is WHAT IS
// RECORDED INTO THE FRAME between them: the draw calls / the bound render targets.
// The frame is presented but EMPTY.
//
// PROBE S hooks the D3D12 command list (captured via ID3D12Device::CreateCommandList,
// device vtable slot 12) and counts, per frame-ish window:
//   - DrawInstanced (cmdlist slot 12) + DrawIndexedInstanced (slot 13) — the REAL
//     scene/UI draws. black = far fewer / zero real draws recorded.
//   - OMSetRenderTargets (slot 47) — is a render target bound at all (the
//     null-RT-bound / wrong-target shape).
// Canonical D3D12 COM slots (Microsoft SDK d3d12.h, fixed by spec — NOT a KCD2
// vtable, so AP3 does not apply; cross-checked against the documented interface
// order). Armed beside PROBE P in seating_hook.cpp, before the swap decision, so
// the counts are read swap-ON vs swap-OFF (the A/B).
//
// PRE-COMMITTED OUTCOME->MEANING MAP (theory-independent — draw counts are ground
// truth):
//   draws HIGH swap-OFF, ~ZERO swap-ON  -> the render loop records NO scene/UI
//     draws swap-ON: the wedge is UPSTREAM in render-submission (the draw-issuing
//     path early-exits / the scene+UI render passes never run). The content
//     question PROBE K pointed at. Walk up from the draw site to what gates it.
//   draws ~EQUAL both, screen still black swap-ON -> the draws ARE recorded but
//     composite black: a RESOURCE/STATE problem (the bound RT is wrong/null, a
//     descriptor/constant is wrong, the UI surface is unbound). Read OMSetRender
//     Targets args swap-ON vs swap-OFF.
//   CreateCommandList never captured -> the engine reuses a pre-existing list /
//     a bundle path the hook misses; widen the capture point.
//
// agent-builds-and-deploys. NO-RESIDUE: remove on KI-0028 closure (file + seating
// arm + CMakeLists), capturing finding+wiring to _research/probe-archive first.
void DrawcallProbeStart();

// MinHook allows ONE hook per target, so PROBE S must NOT re-hook the shared
// d3d12!D3D12CreateDevice export PROBE P already owns. Instead PROBE P calls this
// from its device-capture, handing PROBE S the live device to patch CreateCommand
// List on. Idempotent (first device wins). NO-RESIDUE: remove the PROBE P call
// site too on retire.
void DrawcallProbeOnDeviceCaptured(void* device);

// PROBE Y read accessor — the LIVE cumulative DrawIndexedInstanced count. Reads
// the raw atomic (incremented in the D3D12 hook), NOT the bounded SummaryMain
// watcher's cache, so it stays valid the whole process life. PROBE Y's stall
// trigger reads this to detect "geometry never requested" (draw_indexed==0). 0
// before the device/cmd-list hook lands (reads as "no draws yet", correct).
uint64_t DrawcallProbeIndexedCount();

}  // namespace kcdx::fs_takeover
