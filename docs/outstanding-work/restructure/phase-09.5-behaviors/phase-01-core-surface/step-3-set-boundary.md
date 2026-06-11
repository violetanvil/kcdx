# P1 step 3 — `set` + the apply boundary (worklist drain)

**What.** The write side: load-time `set` records, the boundary applies once via
the worklist drain. The behavior surface becomes end-to-end usable Lua-side.

**Scope.** `kcdx.behavior.set(name, value)` (main-stop, resolution against the
registry — full error branches come in step 4; this step ships the
resolves-or-basic-error path): record + last-wins + the one conflict warn naming
both plugins and both values; `set(name, nil)` teaching error; the apply
boundary at the probed point (step 1's finding): worklist drain, at-most-once
per boundary, never-set skipped, boundary-drain set rules (pending update /
toggle-rules branch), boundary-raise disposition (value + applied-flag cleared,
attributed, drain continues). Doc increment: `set` section + the apply-boundary
model in `docs/lua/behavior.md`.

**Test bar.** Cap rows: US-1/US-2 end-to-end (declarer plugin + two-line
consumer; implementation fires once at the boundary with the final value);
conflict fixture (two setters → last wins, warn names both); nil fixture;
boundary-raise fixture (error attributed, remaining behaviors apply, `get`
reads default); boundary-drain set fixtures (both branches); behavior-only
consumer loads with no version declaration (§9 consumer leg); the US-2
declarer's implementation reaches a hash-checked verb
(`kcdx.statement.replace_with`) and the per-version enforcement is observed
firing at that call site (§9 declarer leg — the §14 version-story row's second
half).

**Dependencies.** Step 1 (boundary placement + ApplyZone landing — observed),
step 2 (registry + declare).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §5 (all
of it except §5.4's post-load paths), §4 (nil rule), §9 (consumer fixture).

**Disassembler-test / author-burden.** `set` is name + value; zero hex.
