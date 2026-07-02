# KI-0028 Y.6 — the WORKING (swap-OFF) menu DOES draw indexed geometry, after a ~27s draw_indexed=0 phase

**Date:** 2026-07-02 (run `kcdx-dev_2026-07-02_10-29-07.log`, swap-OFF arm: `probe_f_swap_suppressed` confirmed)
**Method:** DRAW_PROBE (existing armed probe) cumulative draw-call counts, swap-OFF (kcdx-noswap marker), user launched to menu + held ~1 min + clean quit. The `sys_PakSaveTotalResourceList` cvars in a game-root `user.cfg` produced NO output file (user.cfg not read from that root, or the dump needs a different trigger) — but the DRAW_PROBE answered Y.6 by ground truth more directly than a resource-list would.

## The result — Y.6 answered, backdrop-premise trap CONFIRMED real

**The working menu DOES load and draw level/backdrop geometry.** KCD2's main menu renders over a 3D backdrop scene — confirmed by ground truth, not inference. Terminal count (10:30:12): `draw_indexed=68024`, `ia_set_ib=62622`. The swap-ON black screen is `draw_indexed=0`. So `draw_indexed=0` is NOT normal-for-a-menu — the working path draws geometry, the black path draws none. **"No level/backdrop geometry loaded" is CONFIRMED as the swap-ON differentiator** (this is what Reframe 7/8 inferred from FS-trace absence; Y.6 confirms it from the draw side on the working arm).

## The NEW fact — the working path holds draw_indexed=0 for ~27s, THEN transitions

DRAW_PROBE is cumulative. The progression (verbatim, `Y6_draw_progression.txt`):

| time | draw_instanced | draw_indexed | ia_set_ib | phase |
|---|---|---|---|---|
| 10:29:12–10:29:39 (~27s) | 420→1348 (freezes at 1348) | **0** | **0** | UI/menu compositor only — SAME signature as swap-ON black |
| 10:29:42 | 2373 | **2832** ← jump | 2596 ← jump | **TRANSITION fires — backdrop geometry starts** |
| 10:29:42→10:30:12 | 2373→26445 | 2832→68024 | 2596→62622 | backdrop scene renders, climbs steadily |

**The working menu spends its first ~27s in EXACTLY the `draw_indexed=0` / `ia_set_ib=0` state the swap-ON black screen is stuck in — then a transition at ~10:29:42 fires and geometry drawing begins.** The swap-ON path never fires that transition.

This SHARPENS the differentiator from "swap-ON never loads geometry" to a specific, time-located event: **there is a menu-backdrop-load TRANSITION (~27s into a working boot) that the swap-ON path never reaches or never fires.** The pre-transition state (instanced UI draws, zero indexed, `ia_set_ib=0`) is IDENTICAL on both arms — the divergence is whether the transition fires.

## Cross-check — magnitude reconciles the on-record swap-OFF `96`

RECONCILE-render-vs-levelload records swap-OFF `draw_indexed=96` vs this run's `68024`. Not a contradiction: the `96` was captured EARLY (at/just-after the transition's first frames or during the 0-phase); this run held ~1 min so the cumulative count climbed to 68k. Both are non-zero swap-OFF (confirming geometry loads), differing only by how long the menu was held. The swap-ON `0` is the true zero.

## What this does NOT establish (honest boundary)

- WHAT the ~10:29:42 transition IS (a level-load trigger, a menu-state advance, a streaming gate) — not identified. That is the surviving un-exonerated axis (the level-load / menu-backdrop TRIGGER, Reframe 8).
- WHETHER swap-ON reaches-but-fails the transition vs never-reaches it — the reached-vs-fired partition the baseline named. Needs a probe ON that transition, which requires identifying it first — the user-directed reassessment's job (next in order after probe retirement).
- The `LEVELLOAD_PROBE load_calls=0` this run is the parallel chat's PROBE X (`CResourceList::Load`) — 0 even on the working arm, re-confirming Reframe 8 (it is not the transition trigger).

## Artifact
- `Y6_draw_progression.txt` — the verbatim 21-sample DRAW_PROBE progression.
