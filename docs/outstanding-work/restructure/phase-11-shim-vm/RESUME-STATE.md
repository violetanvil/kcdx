# RESUME STATE — Phase 11 (trimmed 2026-06-07)

The design gap, the ordered-init decision, and the Phase-5 plan are now LANDED in
the tree (the canonical source of truth — see the README ledger + the per-phase
step docs). What remains for this file to carry: the **Phase-4 foundation re-scope**
(not yet built). Delete this file once the P4 foundation is building.

## Phase 4 is FOUNDATION-ONLY (re-scoped — the P4 step docs carry forward-pointers)

The original `phase-04-early-slot-boot-swap` was re-scoped: P4 builds ONLY the
race-safety infrastructure the Phase-5 early surface reuses verbatim —
- **The cross-thread event gate** — worker signals (release) → the boot-open path
  (`asset_overlay.cpp` HOOK 1/HOOK 2, game-main) waits-and-blocks (acquire). A
  producer→consumer CROSS-THREAD DEPENDENCY made deterministic (NOT a race — the
  ~640ms–1.5s margin is the race-if-ungated, the gate is the guarantee;
  `concurrency.md`).
- **The `RegisterRuntimeOverlay` two-writer CAS** — `src/asset_namespace.{h,cpp}`
  RCU store is single-writer-main-thread today; the header (§"WRITER SERIALIZATION")
  names the fix (a CAS retry loop) for a worker writer + the game-main writer.
- **The cap-82 order-inversion regression** — FAILS if the boot open resolved the
  store BEFORE the slot signaled (and it wasn't the bounded-timeout degraded path).

The worker slot-RUNNER shape + the early-bind surface were DEFERRED out of P4 into
Phase 5 (now built there: P5 steps 5/7). The P4 step docs
(`phase-04-early-slot-boot-swap/step-1-early-lua-slot.md`,
`step-2-boot-asset-swap.md`) carry forward-pointers; their full slot/serve content
is superseded by the Phase-5 tree (P5 steps 5/6).

**The P4 step docs + the Phase-4 README still describe the OLD "early slot + boot
swap" scope** (the README ledger row was re-labeled to "the cross-thread
foundation", but the P4 dir's own docs were not re-authored — that re-scope is the
remaining P4 tree-cleanup, small, do it when P4 builds).

## The owed probe (P5 step 1 — now in the tree)

Before binding subsystem author-surfaces on the worker: confirm a worker
subsystem-bind + a `RegisterRuntimeOverlay`-shape store write on the worker VM is
GC-safe + PROBE-Q-silent. (PROBE INITORDER already proved the subsystem ORDERING +
console-movability; this confirms the BIND is hazard-free.) This is
`phase-05-startup-sequence-contract/step-1-worker-gc-safety-probe.md` — tracked in
the plan tree, not just here.

## Gate timeout (still owed at the P4/P5 build)

The bounded-timeout fallback for the event gate (worker never signals → boot-open
proceeds vanilla, fail-loud) — value + degraded behavior decided at build under
architect-review. Lean: 5000ms + vanilla-serve + `LOG_WARN_KV` (a crashed/hung
slot must not hang boot forever). Surfaced, not yet user-decided.

## Resume sequence (the design + plan are DONE; what's left)

1. Build the **P4 foundation** (gate + CAS + cap-82), gated behind the owed
   worker-GC-safety probe (P5 step 1 — runnable independently as the probe). The P4
   foundation is the dependency every Phase-5 step rests on.
2. Then `/execute` the Phase-5 steps in order (the
   `phase-05-startup-sequence-contract/` step docs are the Source work-items).
3. The small P4 tree-cleanup (re-author the P4 README + step docs to the
   foundation-only scope) — do it when P4 builds.
