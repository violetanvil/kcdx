# P1 step 4 — window law + resolution errors + in-memory edges

**What.** The full teaching-error story for a `set` that cannot resolve, the
out-of-window wall, and in-memory edge recording (persistence is step 6).

**Scope.** The five discriminating branches per design §6 (prefixed-later with
first-launch-calibrated wording; loaded-no-such-name with failed-declarer
discrimination; absent/disabled/engine-rejected; bare-name; the out-of-window
error for an early-stop set on a plugin-tier behavior — C++ caller path; catalog
names pass from any stop). In-memory consumer→declarer edge recording on every
resolved/failed set (feeds step 6/7). Update the
`src/lua_plugin_loader.cpp:170-176` rationale comment (behaviors are the first
cross-entrypoint dependency; the model is errors + edges + auto-order, not
auto-topo) — same change as the mechanism that makes it stale
(`.claude/rules/deletion-hygiene.md` footing). Doc increment: the error/ordering
section of `docs/lua/behavior.md`.

**Test bar.** Cap fixtures: reorder branch; missing branch; typo branch
(loaded, no such name); failed-declarer variant; disabled-owner variant;
bare-name branch; early-stop C++ set on a plugin-tier behavior → out-of-window
error; early-stop set on a catalog name → resolves (catalog tier arrives P3 —
this fixture uses a stub engine-declared name registered by the test harness,
replaced by the real catalog row in P3 s1). The Lua early-stop leg is the
user-approved trigger deferral (P11 P5 `lua_before`) — not in this step; the
deferral takes effect when this step lands, so this step FILES its bucket-2
tech-debt entry naming that trigger (`.claude/rules/test-discipline.md`
§"Bucket 2"), same change.

**Dependencies.** Steps 1–3 (registry, set, boundary; the C++ stop positions
observed in step 1). The C++ early-stop fixture needs only `kcdxPlugin_Load` +
a minimal C `Set` entry — if P2 s1's interface is not yet built, this fixture's
C++ leg lands as a thin direct-engine-call harness this step replaces in P2 s1
(noted so the step stays verifiable at its position).

**Reference.** [`../plan-spec.md`](../plan-spec.md).

**Design authority.** [`../behavior-design.md`](../behavior-design.md) §6
(window law + branches), §10 (error catalog), §11 (comment update).

**Disassembler-test / author-burden.** Errors teach names and ordering — never
ask the author for hex; the `list()` pointer in the typo branch keeps discovery
name-based.
