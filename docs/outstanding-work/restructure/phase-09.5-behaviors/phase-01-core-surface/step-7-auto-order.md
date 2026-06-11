# P1 step 7 — the auto-order method

**What.** The passive, callable order-correction method: compute an order
satisfying the known edges, apply it through a new `load_order` write-back path.
No UI, no console trigger — the programmatic seam and the test plugin are the
only callers until the future pre-launch button.

**Scope.** The computation (consumer below declarer; minimal displacement of
unrelated rows; a cycle is REPORTED, never silently broken) + the `load_order`
order write-back (the unit is read/resolve-only today — this is the §11
"extended" work) + the engine-internal seam the test plugin drives
(`.claude/rules/headless-testable.md`). Doc increment: the auto-order paragraph
in `docs/load-order.md` (user-facing: what the engine can fix and when it takes
effect — next launch).

**Test bar.** Cap fixtures: a deliberately mis-ordered fixture's persisted
edges → the method yields + applies a corrected order (verified by re-reading
the written order); an unrelated row's position is preserved; a cycle fixture →
reported, order unchanged.

**Dependencies.** Step 6 (the persisted edge store the method consumes).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §6 (the
auto-order method), §12 (no console trigger — the session's order is consumed
by console-time), §11 (`load_order` extension).

**Disassembler-test / author-burden.** N/A — end-user-facing relief (the engine
fixes ordering); no author input.
