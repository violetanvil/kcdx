#pragma once

// === DIAGNOSTIC (PROBE P) — KI-0028 shader-blob -> PSO consumption ground truth ===
//
// WHY (KI-0028, after the shader-alias fix landed PARTIAL + the swap-off baseline
// reached the menu): the screen is STILL black on swap-on, yet by direct
// measurement the render pipeline RUNS and PRESENTS (PROBE K: 120fps, GPU
// scanout) and the served shader BYTES are complete + correct (scaleform4.ext et
// al. read want==got; kcdx's DEFLATE inflater is proven by cap-109). So the
// frames are presented BLACK = a render-CONTENT failure, and it is NOT corrupt
// served bytes. The remaining question is on the CONSUMPTION side, which the
// fresh-frame probe-design isolated: do the shader blobs the engine reads become
// valid GPU pipeline state objects, or does PSO creation fail / never happen for
// the render-critical (incl. UI/Scaleform) shaders?
//
// THE BLIND-SPOT ESCAPE: kcdx's FS trace fires only on kcdx's slots, so it is
// blind swap-off (the engine uses its own CCryPak there) — a file-op diff is
// impossible by construction. This probe instruments the CONSUMPTION side
// (ID3D12Device::CreateGraphicsPipelineState), which runs IDENTICALLY swap-on AND
// swap-off. The same instrument reads both modes; the only difference is the
// VALUES flowing through it.
//
//   P1 — capture the ID3D12Device* one-shot: MinHook d3d12!D3D12CreateDevice;
//        on the first successful device creation record ppDevice and patch the
//        device's vtable slot 10 (CreateGraphicsPipelineState, VERIFIED 0-based
//        slot from d3d12.h ID3D12DeviceVtbl) + slot 11 (CreateComputePipelineState).
//   P2 — the detour logs, per PSO-creation call: the returned HRESULT, whether
//        the returned PSO ptr is null, and from the GRAPHICS_PIPELINE_STATE_DESC
//        the VS+PS bytecode length + the first-4-bytes container magic (DXBC =
//        0x43425844). It then calls the original and returns its result unchanged
//        (no behavior change — a pure read-and-pass-through, honors no-thunk).
//        Counters aggregate; a per-call DEBUG line is rate-bounded (first N + any
//        failure) so the hot PSO-creation path is not flooded (logging.md).
//
// Outcome -> meaning (pre-committed, flat; each non-O1 FALSIFIES "served content wrong"):
//   O1  a blob arrives len=0 / null / non-DXBC magic        => the engine received
//        malformed shader bytes -> kcdx served wrong bytes for THAT asset (still
//        the content framing). Next: dump that asset's served byte range from the
//        pak (index has offset+len) and diff vs disk. (Weak prior: bytes proven OK.)
//   O2  blobs well-formed BUT CreateGraphicsPipelineState     => bytes are FINE; PSO
//        returns a FAILED hr / null PSO                        assembly fails. FALSIFIES
//        "kcdx serves wrong content." Next: log the full desc; the failure is in
//        root-sig / input-layout / a non-shader resource, not the served file.
//   O3  every PSO call succeeds (hr ok, non-null), blobs OK    => shaders + PSOs are
//        entirely fine; black is DOWNSTREAM of PSO. FALSIFIES the content framing
//        hardest. Next: move the instrument to draw submission (DrawIndexed count
//        per frame + OMSetRenderTargets handle): 0 draws => empty scene (level/
//        entity data, NOT shaders); null RT => binding bug.
//   O4  blobs + hr identical swap-on AND swap-off (run both)   => the render pipeline
//        receives byte-identical, structurally-identical inputs both ways. STRONGEST
//        falsification: the differentiator is NOT a value the render pipeline
//        consumes -> a side effect elsewhere (a global the swap perturbs, init
//        order, a thread the swap changes). Abandon the render-content axis.
//   O5  a render-critical PSO (Scaleform/UI) is NEVER created   => the engine never
//        swap-on (but its create fires swap-off)                even REQUESTS that
//        pipeline under takeover -> kcdx's serving diverted the engine's shader-
//        resolution path upstream of PSO (consistent with the 36 nonexistent
//        data/gameshaders/*.ext probes seen ONLY swap-on). Next: hook the shader
//        name->resolution path and compare the path taken.
//   P*  device never captured                                  => the engine creates
//        its device by a path this hook misses -> widen the capture point.
//
// RUN PLAN: arm beside PROBE K (swap-on + swap-off both — P fires identically in
// both, which is what makes O4 the parity check). Read the PSO_PROBE summary line
// + any per-call failure lines from kcdx-dev.log.
//
// NO-RESIDUE: on retirement capture the finding + wiring to
// _research/probe-archive/ then REMOVE from live source (working-artifacts.md).
// Greppable tag: "PSO_PROBE".

namespace kcdx::fs_takeover {

// P1 — arm the one-shot D3D12 device-capture + PSO-creation hook. Call once,
// early (from the FS-takeover seating, beside PresentProbeStart). Idempotent.
// Installs a MinHook on d3d12!D3D12CreateDevice; on the first device creation it
// patches the device vtable's PSO-creation slots and starts logging per-call. If
// the engine already created its device before this arms, the hook never fires
// and the summary logs "device never captured" (an outcome, not a failure).
void PsoProbeStart();

}  // namespace kcdx::fs_takeover
