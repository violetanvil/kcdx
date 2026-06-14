# Step 3.6 — subsume the asset_overlay.cpp two-hook seam

**What.** Remove the now-redundant `asset_overlay.cpp` two-hook seam — HOOK 1
(`AdjustFileNameResolver`), HOOK 2 (`FOpenLooseOverlay`), their `AddCEngine`
installs, and the overlay-map globals — because their behavior is now carried by
kcdx's vtable slots (slot 1 = the index lookup from step 3.2; slot 36 = the kcdx
open from step 3.2; the read serve from step 3.3). The overlay-map logic the index
build (step 2.4) carried forward is the replacement; the two hooks are dead once
the slots are live. This is the seam subsumption (design §11, §1) — done ONLY now,
after the replacement exists, so there is no window where neither serves.

**Scope.** Delete HOOK 1 + HOOK 2 + the overlay-map globals + the two `AddCEngine`
calls from `src/asset_overlay.cpp` (the file may reduce to nothing, or retain only
the overlay-map-build helpers the index reuses — whichever the step-2.4 ingestion
left depending on it). Sweep for any prescriptive reference to the removed hooks
(`.claude/rules/deletion-hygiene.md`). One commit. Build green; the asset-serving
behavior is unchanged (the kcdx slots carry it).

**Test bar.** The existing asset-overlay regression rows STILL PASS after the seam
removal — a loose override still wins, a vanilla asset still serves — but now
through the kcdx slots, not the hooks (the same falsifiable matrix rows, now
proving the slots carry the behavior). A launch confirms asset serving unchanged
(agent-read, `kcdx-dev.log`). Build green. The deletion-hygiene sweep finds no
stale prescriptive reference to HOOK 1/HOOK 2.

**Dependencies.** Steps 3.2 (the slot-1/open replacement) + 3.3 (the read serve) —
the seam is removed only after its full replacement is live. This is the last
real-slot step's natural successor.

**Reference.** [`../plan-spec.md`](../plan-spec.md); design §11 (the seam
subsumed), §1 (subsumes the two-hook overlay seam); [`src/asset_overlay.cpp`](../../../../src/asset_overlay.cpp)
(the seam removed); `.claude/rules/deletion-hygiene.md` (the survivor sweep).

**Disassembler-test / author-burden.** N/A — removing engine-internal hooks; the
author-facing contract is unchanged (design §7).
