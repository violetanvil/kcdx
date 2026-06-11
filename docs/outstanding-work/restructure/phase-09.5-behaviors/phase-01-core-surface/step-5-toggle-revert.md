# P1 step 5 — revert / post-load toggle contract

**What.** Runtime toggling for `revert` declarers; the loud walls for everyone
else. Main-thread inline path only (the off-thread command queue is P2 s2).

**Scope.** Post-load `set` legality: with `revert` → `revert(old)` then
`implementation(new)` then record, never-applied gate (skip revert, never handed
an uncreated state); without `revert` → teaching error, record unchanged. Both
failure dispositions (revert-succeeds-implementation-raises → value +
applied-flag cleared; revert-raises → record unchanged; both attributed to the
declarer). Post-load `declare` → teaching error. `get()` truthfulness across
every path. Doc increment: the toggle/revert section of `docs/lua/behavior.md`.

**Test bar.** Cap fixtures: US-5 toggle (revert+implementation order, `get`
tracks); revert-less post-load set → error, value unchanged; never-applied
toggle → implementation only; both failure dispositions; post-load declare →
error.

**Dependencies.** Steps 2–3 (registry, set, boundary, applied-flag).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §5.4
(minus the queued-command path), §4 (`revert` field), §10 (toggle rows).

**Disassembler-test / author-burden.** N/A beyond the existing surface — no new
input shapes.
