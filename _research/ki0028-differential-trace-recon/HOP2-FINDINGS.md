# KI-0028 differential trace — HOP 2 RESULT: FUN_180501cb0 NEVER FIRES swap-ON (hypothesis FALSIFIED)

**RAN 2026-07-03.** swap-OFF `kcdx-dev_2026-07-03_11-07-05.log` · swap-ON `kcdx-dev_2026-07-03_11-09-29.log`.
Both arms `sites_armed=6` (the apply_draw IB-sampler installed on both).

## The diff — the static hypothesis is KILLED (good: the probe was falsifiable)

| | swap-OFF (menu) | swap-ON (black) |
|---|---|---|
| `FUN_180501cb0` apply_invokes | **52764** | **0 — NEVER FIRED** |
| items_seen | 1,087,100 | 0 |
| ib_set (indexed items) | 842,142 (77%) | 0 |
| ib_null (non-indexed items) | 244,958 | 0 |
| draw confirm | draw_indexed=27791 ia_set_ib=26056 | **draw_instanced=21011** draw_indexed=0 ia_set_ib=0 |

**The HOP-2 static hypothesis ("items built with null IBs → FUN_180501cb0 takes the non-indexed
branch") is FALSIFIED.** `FUN_180501cb0` — the per-item apply+submit fn that CAN bind index buffers —
is **NEVER ENTERED swap-ON** (apply_invokes=0). Yet `draw_instanced=21011` non-indexed draws still
happen — so swap-ON the engine issues its non-indexed draws through a **DIFFERENT submission path**,
NOT FUN_180501cb0. The item-list / null-IB theory never got to apply, because the function that reads
that list never runs.

## The sharpened divergence (cleaner than expected)

The divergence is now: **the entire `FUN_180501cb0` indexed-capable submission path is bypassed
swap-ON.** The working arm drives all its draws (indexed + non-indexed) through FUN_180501cb0 (52764
invocations); the black arm drives its 21011 non-indexed draws through some OTHER path and calls
FUN_180501cb0 zero times. So the question is NOT "why are the items non-indexed" — it is **"why is
FUN_180501cb0 never called swap-ON, and which OTHER draw path issues the 21011 non-indexed draws?"**

FUN_180501cb0's 2 callers (static): `FUN_1804ec3a0` (`0x4ec3a0`), `FUN_1805014a0` (`0x5014a0`) — one
of these render passes is skipped swap-ON. The next hop traces THOSE: do they fire swap-ON? If a
caller fires but doesn't reach FUN_180501cb0, the branch inside it that gates the call is the
divergence; if a caller never fires, walk one more up. AND: identify the swap-ON non-indexed draw
path (hook DrawInstanced's engine-side caller — a separate submit fn) to see what IS rendering.

---

# KI-0028 differential trace — Step 4 (hop 2, static): the IB field is per-item, gated at plVar6[4]

**Date:** 2026-07-03 · **Method:** static Ghidra decomp of `FUN_180501cb0` (`0x501cb0`), the swap-OFF
SetIndexBuffer caller named by the Step-3 trace. **Trust:** primary (body read). Script:
`Ki28IndexedCallerDecomp.java` → `_hop_indexed_caller_180501cb0.txt`.

## What FUN_180501cb0 is

`CDeviceGraphicsCommandInterface::ApplyAndDraw`-class — the per-draw state-apply + submit fn. It
walks a render-item list (`[param_1+0x298]`..`[param_1+0x2a0]`) and, PER ITEM, applies state then
issues the draw. It handles BOTH indexed and non-indexed items in ONE loop, branching on the item's
own fields. `param_2` = the command interface (`+0x2a` = the D3D12 device-command wrapper).

## The decisive branch — SetIndexBuffer is gated on the item's IB field `plVar6[4]`

```
line 95:  lVar13 = plVar6[4];                                   // the item's INDEX-BUFFER field (item+0x20)
line 96:  if ((lVar13 != 0) && (param_2[0x14] != lVar13)) {     // only if the item HAS an index buffer
line 97:      param_2[0x14] = lVar13;
line 98:      FUN_1805025b4(param_2);                            // -> engine SetIndexBuffer (the Step-3 leaf)
line 99:  }
...
line 186: iVar2 = (int)plVar6[0xd];                             // the draw count
line 188: if (plVar6[4] == 0) {                                 // NO index buffer ->
line 198:     (**(...+0x60))(..., uVar3, iVar2, ...);           //   NON-INDEXED draw (DrawInstanced) ✱
line 202: } else {                                              // HAS index buffer ->
line 213:     (**(...+0x68))(..., uVar4, iVar2, ...);           //   INDEXED draw (DrawIndexedInstanced)
line 216: }
```

So `FUN_180501cb0` **DOES run swap-ON** — it is what issues the 19447 non-indexed `draw_instanced`
calls the Step-3 confirm measured. The divergence is NOT "the submit fn isn't called". It is:
**every render item's index-buffer field `plVar6[4]` (item+0x20) is NULL swap-ON**, so every item
takes the `plVar6[4]==0` non-indexed branch (line 188 → vtable +0x60) and SetIndexBuffer (line 98)
is never reached. Swap-OFF the items carry a valid IB → the indexed branch (+0x68) runs.

## The sharpened frontier (one more hop UP, from submission to construction)

The divergence has moved from the SUBMISSION path (exonerated — it correctly branches on the item)
to the render-item CONSTRUCTION: **who builds these render items, and why is item+0x20 (the IB
pointer) null swap-ON but populated swap-OFF?** This is the CCompiledRenderObject the compile pass
(Step-1 `FUN_180429384`/`FUN_180429794`) produces — the compiled object carries the IB pointer the
draw item copies. A null IB on the item ⇒ the compiled object's IB was never set ⇒ CCRO::Compile
either failed (returned 0) or built a vertex-only object swap-ON.

## Next probe (runtime CONFIRM — do not theorize the null; observe it)

Extend the render trace: hook `FUN_180501cb0` and, per item in its loop, sample `plVar6[4]` (the IB
field) — tally null vs non-null, swap-ON vs swap-OFF. Pre-committed map:
- **IB field uniformly NULL swap-ON, populated swap-OFF** → CONFIRMED: the render items are built
  without index buffers swap-ON. Frontier → the render-item/CCRO builder that sets item+0x20 (walk
  from CCRO::Compile's output). This is the expected result given draw_instanced=19447 / ia_set_ib=0.
- **IB field populated swap-ON too, but SetIndexBuffer still not called** → the gate `param_2[0x14]
  != lVar13` (line 96, the redundant-bind skip) is wrongly true, or the loop exits early swap-ON →
  re-read the loop bound `[param_1+0x298..0x2a0]` (is the item LIST empty/short swap-ON?).
- **FUN_180501cb0 fires FEWER times / different item count swap-ON** → the render-item list itself is
  built differently (fewer/no indexed items enqueued) → walk who fills `[param_1+0x298]`.

Callers of FUN_180501cb0 (the render pass that owns the item list): `FUN_1804ec3a0` (`0x4ec3a0`),
`FUN_1805014a0` (`0x5014a0`).
