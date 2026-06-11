# P1 step 6 — edge persistence + launch-time recognition

**What.** The in-memory edges (step 4) become a persisted, self-invalidating
store; known conflicts surface at the NEXT launch before any plugin runs.

**Scope.** The engine-data edge file (path fixed at build, e.g.
`kcdx-engine/data/behavior_edges.toml`) inside the `load_order` unit (design
§11): rebuilt from each launch's observed sets; edges whose consumer or declarer
is absent from the discovered set pruned/ignored. The pre-plugin-execution
re-check: a persisted edge violated by the current order logs the recognized
conflict up front (the §10 stale-edge warn). Second-launch error upgrades (the
persisted edge lets branch-1/bare-name errors name the behavior confidently).
Doc increment: the edges paragraph in `docs/lua/behavior.md` + `docs/load-order.md`
cross-reference.

**Test bar.** Cap fixtures: a mis-ordered run records the edge → next launch
(suite harness restarts within the run where possible, else the two-launch
matrix row is marked for the phase's launch pass) emits the up-front warn; an
uninstalled-declarer edge is pruned (no warn); a consumer updated to not-set
drops its edge.

**Dependencies.** Step 4 (in-memory edges + the error branches the upgrades
touch).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §6
(edges persist; the store self-invalidates), §10 (stale-edge warn).

**Disassembler-test / author-burden.** N/A — engine-internal store; no author
input.
