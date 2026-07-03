# KI-0028 differential trace — RESULT (Step 3): the FIRST divergence is the engine SetIndexBuffer

**RAN 2026-07-03.** swap-OFF (menu) `kcdx-dev_2026-07-03_10-51-56.log` · swap-ON (black)
`kcdx-dev_2026-07-03_10-56-06.log`. Both arms: PROBE Z10 `render_trace_armed sites_armed=5` (all 5
MinHooks installed, zero hook_create/enable failures on either arm). **The trace is DECISIVE.**

## The diff — the mechanical first divergence (no theory)

| Site (ordered) | swap-OFF (menu, GOOD) | swap-ON (black, BAD) |
|---|---|---|
| `set_index_buffer` (FUN_1805025b4 = engine SetIndexBuffer / IASetIndexBuffer leaf) | **fired 6× (capped)**, caller RVA `0x501ebe` (in FUN_180501cb0) | **0× — NEVER FIRED** |
| stage_sequencer / compile_pass / ccro_compile / render_flush | 0× | 0× |
| drawcall confirm (D3D12 boundary) | `ia_set_ib=26056 draw_indexed=27791 draw_instanced=3606` | `ia_set_ib=0 draw_indexed=0 draw_instanced=19447` |

**FIRST DIVERGENCE: the engine's own `SetIndexBuffer` (FUN_1805025b4) fires on the menu arm and is
ENTIRELY ABSENT on the black arm.** The D3D12 confirm cross-validates perfectly: swap-OFF
`ia_set_ib=26056` matches the engine SetIndexBuffer firing; swap-ON `ia_set_ib=0` matches it never
firing. The engine never calls its own SetIndexBuffer swap-ON → never reaches D3D12 IASetIndexBuffer
→ `draw_indexed=0` → black. This is the mechanical answer the METHOD RESET set out to find.

## The sharpened signature (NEW ground truth — not idle)

The black arm is NOT stalled/idle in the render loop — it is heavily RENDERING, just vertex-only:
- swap-ON `draw_instanced=19447` (MORE than menu's 3606), `ia_set_vb=19447`, `ia_set_topo=19447`,
  `geo_buf=264` (geometry buffers ARE created, matching Z8).
- swap-ON `ia_set_ib=0`, `draw_indexed=0`: **zero index-buffer binds, zero indexed draws.**

So the precise mechanism is: **swap-ON the engine binds vertex buffers + sets topology + issues
non-indexed (DrawInstanced) draws en masse, but NEVER binds an index buffer or issues an indexed
draw.** The world/menu geometry is indexed-mesh geometry; the swap-ON path renders only the
non-indexed subset (particles/fullscreen/UI-quad class) → the indexed scene composites to nothing →
black. This kills any "render loop never runs" framing (it runs hard) and localizes to: **why is the
indexed-draw path (which calls FUN_180501cb0 → engine SetIndexBuffer) never taken swap-ON, while the
non-indexed DrawInstanced path runs 19447×?**

## The next frontier (NOT this doc's step — the follow-up probe)

The 4 upstream sites firing 0× on BOTH arms means they are NOT on the live per-frame draw path in
these runs (compile-context / different call context) — only `set_index_buffer` carried the signal.
The next probe instruments the ONE swap-OFF caller `FUN_180501cb0` (RVA `0x501cb0`, containing the
menu-arm callsite `0x501eb9`): does IT fire swap-ON (reached but skips the bind) or never fire (its
own caller gates it out)? That walks one edge UP the indexed-draw path toward the branch that swap-ON
does not take — the same differential method, one hop up. Static: read FUN_180501cb0's body + its
callers (the render-pass that decides indexed vs non-indexed submission).

---

# KI-0028 differential trace — Step 1 (static): the render-submission edge list to instrument

**Date:** 2026-07-03 · **Method:** static Ghidra (KCD2.rep, WHGame.dll release_1_5_1164953_841,
image base 0x180000000). No launch. **Trust:** primary — every fn is a body read at the cited RVA.
**Scripts (co-located):** `Ki28RenderSubmitAnchors.java` → `_render_submit_edges.txt` (string-anchor
+ caller graph); `Ki28DrawRecordDecomp.java` → `_dr_*.txt` (body decomp + indirect-edge disasm).

## Goal (DESIGN.md step 1)

Name the REAL adjacent render-submission edges between "geometry created" (Z8: geo_buf=262) and the
indexed draw (drawcall_probe: ia_set_ib=0 / draw_indexed=0 swap-ON) — so the differential tracer
instruments real engine functions, not guesses.

## The AP19 trap the anchors walked into (recorded so the next agent does not re-hit it)

The `DRAWINDEXEDINSTANCED` string anchor is a **debug-marker name table, NOT the draw call**:
- `FUN_1825381d0` = a `D3D12_COMMAND_TYPE`→PIX-string lookup (`param_1==4 → "DRAWINDEXEDINSTANCED"`).
- Its only caller `FUN_182538c80` = the **DRED breadcrumb dumper** (`DX12GpuDebug.cpp`, "D3D12 Device
  Removed Extended Data") — a device-removed post-mortem, stringifying the breadcrumb command history.
  NOT the live per-frame draw path.

**Lesson (load-bearing for Step 2):** the hot D3D12 draw call (`DrawIndexedInstanced`, COM slot 13)
has NO string and is an INDIRECT vtable call — statically unfindable by xref, exactly why the
drawcall_probe hooks the D3D12 COM boundary directly. **The tracer does NOT chase the D3D12 leaf
statically.** It instruments the ENGINE-side render-submission stages that DO have named functions,
and brackets them with the drawcall_probe's D3D12 counters (already the confirm signal).

## The named render-submission stages (verified bodies — the tracer's candidate instrument-set)

Ordered geometry-build → draw-record. RVAs are module-relative (VA − 0x180000000).

| Stage | Fn (RVA) | What it is (body-read) | Upstream caller (edge to arm) |
|---|---|---|---|
| **Per-frame render-object COMPILE pass** | `FUN_180429384` @ `0x429384` | Iterates render objects; per object calls `FUN_180429794` (`CCompiledRenderObject::Compile`, the "Compile failed, PSO creation failed" fn). Produces the compiled objects the draw loop submits. | `FUN_18086b574` @ `0x86b574` (callsite `0x86b5ad`) — the per-frame compile entry |
| **CCRO::Compile (per-object PSO build)** | `FUN_180429794` @ `0x429794` | Builds/validates one compiled render object's PSO; returns 0 on fail. draw_indexed=0 is consistent with this returning 0 (no compiled object → nothing indexed to draw). | `FUN_180429384` (3 callsites: `0x429531/5b6/6f9`) |
| **★ ENGINE SetIndexBuffer (the IASetIndexBuffer leaf)** | `FUN_1805025b4` @ `0x5025b4` | **DECISIVE.** `CDeviceGraphicsCommandInterfaceImpl::SetIndexBuffer` — body holds `"Trying to set invalid index buffer"` (`DeviceCommandListCommon_D3D12.cpp:0x233`) and ends in the indirect call `(*(cmdlist_vtbl+0x158))(cmdlist, &ibview)` = **the D3D12 `IASetIndexBuffer` (slot 43) the drawcall_probe hooks.** This is the exact engine fn one edge above the D3D12 boundary. `ia_set_ib=0` swap-ON ⇒ its 6 callers never reach the bind. | **6 callers = the instrument-set** (below) |
| Engine SetVertexBuffers (sibling leaf) | `FUN_1805026c0` @ `0x5026c0` | The paired VB-bind op, same file, 6 callers (mostly the same). | same caller cluster |
| **Render-stage sequencer** | `FUN_18086b574` @ `0x86b574` | Per-frame render-STAGE state machine (stage id at `[this+0x1974]`); **stage 4** runs `(*vtbl+0x28)()` → `FUN_180429384` (compile pass) → `FUN_180f8b734` (**next stage = likely the draw-record loop**). Runs only on stage TRANSITION. Called INDIRECTLY (vtable) from the render tick. | indirect (render tick) |

The **6 callers of `FUN_1805025b4`** (the engine SetIndexBuffer) — the render-submission edges that
DECIDE to bind an index buffer, i.e. the tracer's core instrument-set:
`0x5029f0`, `0x501cb0`, `0xc125cc`, `0xc129dc`, `0x24a50e4`, `0x24a54e8` (callsites
`0x502e89/501eb9/c12801/c12aaf/24a544e/24a58cd`). One of these is the scene-pass draw loop.

Anchors that pinned but are OFF the hot draw path (do NOT instrument): the DRED breadcrumb dumper
cluster (`FUN_182538c80` = `DX12GpuDebug.cpp`, `FUN_1825381d0` = command-name string table — the
`DRAWINDEXEDINSTANCED` string was a PIX label, NOT the draw call — AP19 trap, see above),
`CRenderMeshUtils::RayIntersectionImpl` (0x35040a0, CPU raycast), RenderMesh shutdown/leak fns.

## The instrument-set is NAMED — Step 1 complete

The DESIGN.md deliverable (name the real adjacent render-submission edges) is done. The tracer arms,
in dependency order:

1. **`FUN_18086b574`** (render-stage sequencer) — logs `(stage_id, [this+0x1974])` each transition.
   Diff shows if swap-ON ever reaches stage 4 (compile) or the draw stage.
2. **`FUN_180429384`** (compile pass) + **`FUN_180429794`** return (CCRO::Compile) — did it produce a
   compiled render object, or return 0 (PSO fail)?
3. **★ `FUN_1805025b4`** (engine SetIndexBuffer) — the decisive leaf. If swap-OFF reaches it and
   swap-ON does not, the divergence is in ITS 6 callers → instrument whichever caller ran swap-OFF.
   Log the caller-return-address so the diff names WHICH of the 6 fired.
4. Bracket with the live drawcall_probe D3D12 counters (`ia_set_ib`/`draw_indexed`) as the confirm.

The FIRST armed site that fires swap-OFF but not swap-ON (or fires but takes the "invalid index
buffer" skip) is the mechanical divergence point — the answer, no theory.

**Runtime cross-check (from Measurement 2):** `0x492b908` (renderer singleton the tick gates on) is
read at site 1 so the diff distinguishes "stage skipped: singleton null" from "stage ran: empty".
