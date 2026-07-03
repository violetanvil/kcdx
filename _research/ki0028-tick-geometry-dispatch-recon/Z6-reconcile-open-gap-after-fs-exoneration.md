# KI-0028 — the reconciled OPEN GAP after FS exoneration (post-Z5, pre-Z6-probe)

**Date:** 2026-07-02. Status: reconciling prior evidence with the Z4/Z5 FS exoneration to aim the next probe. Fresh-frame subagent re-tasked with these facts; its probe design pending.

## The three directly-observed facts that bound the current gap (all verbatim from prior runs)

1. **Level-load IS reached swap-ON, and FAILS there.** A prior live invasive cdb attach on the black full-swap process (ROOT-CAUSE-existence-overreport.md ⚠CORRECTION, `kcdx-dev_2026-06-22_20-49-13.log`) showed the main thread in `MessageBoxA` under `WHGame!C_Game::CreateInstance` — the engine's level-load-FAILURE dialog, ZERO kcdx frames on 194 threads. So level-load is ENTERED and ABORTS/fails inside CreateInstance. (A level-pak coverage gap once caused a hard `RaiseException(0xD2)`; a recursive-pak-walk fix removed some of it, but the CreateInstance-level failure returned on at least one clean full swap. The exact CURRENT failure mode is not cleanly pinned — 3 prior root-cause claims were each overturned.)

2. **The draw side is already partitioned to `ia_set_ib=0`.** PROBE X (ROOT-CAUSE-existence-overreport.md §PROBE X, `kcdx-dev_2026-06-22_19-10-46.log`, swap-ON black): `draw_indexed=0`, `ia_set_ib=0` (IASetIndexBuffer NEVER called), `ia_set_vb == ia_set_topo == draw_instanced == 13649` (full IA setup + DrawInstanced for the non-indexed fullscreen/post/clear/sky passes). VERDICT: indexed-geometry mesh-draw is ABANDONED UPSTREAM of command-list recording — no IB ever bound. NOT "bound but skipped." Two remaining named branches, NOT discriminated:
   - (a) IB RESOURCES never CREATED (device CreateCommittedResource/CreatePlacedResource — are IB resources made swap-ON?), OR
   - (b) the scene/UI mesh render PASS is never ENTERED (higher-level engine decision — visibility / scene-graph / render-list population drops all real geometry before D3D12).

3. **FS is fully exonerated (Z4/Z5, this session).** Every file the engine reads under the swap returns correct bytes AND correct sizes (Z5: abstract-stream read succeeds, plausible sizes for all 40 assets). The engine reads through an abstract stream that succeeds; kcdx's read slots also serve `want==got result=ok`.

## The KEY TENSION (what makes this hard)

**Correct file serving + level-load reached + level-load fails at CreateInstance.** The files are served correctly, yet CreateInstance still aborts/MessageBoxes. So the CreateInstance failure is NOT a file-serve failure. Something ELSE the swap perturbs makes CreateInstance decide to fail — a non-file state, an object/pointer identity, a control-flow branch, or a downstream consumer of correctly-loaded data.

## Why the earlier fresh-frame probe target needed narrowing

The first fresh-frame probe (`CResourceList::Load @ 0x4dcb60` entry, partition never-called/called-empty/deeper) had leading-guess Outcome A = "level-load never called." Fact 1 FALSIFIES that (level-load is reached). And since files serve correctly (fact 3), `0x4dcb60` likely fires and does real work (Outcome C). So the entry-partition probe would mostly re-derive known ground. The re-task asks the subagent to pick between:
- (a) the CreateInstance abort/failure POINT itself (what condition inside CreateInstance decides to fail/MessageBox — swap-ON vs working arm), OR
- (b) the draw-side discriminator (IB-never-created vs render-pass-never-entered).

## Caution flags for whatever probe comes next (results-driven)

- This bug has had **3 overturned root causes** — every "confirmed fix" was premature. Assume no prior root-cause claim is correct.
- The working control (no-op/thunk-all swap) reaches the MAIN MENU but the menu does NOT load the kutnohorsko LEVEL — so the no-op-vs-full comparison is NOT a clean A/B for the level-load path (they fail at different stages). A clean A/B needs both arms to reach the same code path. The abstract-stream read succeeds on BOTH the menu assets AND (per Z5) the pak assets, so the FS-read A/B is clean; the LEVEL-LOAD A/B is not (only full-swap reaches it). Whatever probe comes next must state which arm actually exercises its target.

## Probe wiring status

The Z4/Z5 `crt_reader_probe.*` wiring stays armed but uncommitted; its FS question is answered. It retires-and-captures once the KI-0028 direction settles (do not commit it; capture the finding — done in Z4/Z5 docs — then remove on close).
