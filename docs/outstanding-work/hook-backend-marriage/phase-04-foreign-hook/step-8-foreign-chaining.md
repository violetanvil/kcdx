# Step 8 — foreign-hook chaining + comp-NN fixture

**What.** When Step 7's classifier flags a foreign hook, chain onto it: follow the
foreign jump, capture the foreign detour as kcdx's "original," and install kcdx's
hook so the call chain becomes `game → kcdx hook → foreign hook → real original` —
both mods' hooks fire. Ship the `comp-NN` two-mod fixture that proves it. Covers
E14, E20 (`../context.md`).

**Scope (commit-grain).**
- The chaining mechanism (design §6.2): kcdx's backend captures the CURRENT
  prologue (which jumps to the foreign detour) as kcdx's trampoline-original, so
  calling kcdx's "original" runs the foreign mod's detour, which runs the real
  function. kcdx's chain dispatch fires first, then delegates to the foreign hook
  via the normal call-original path. This is what safetyhook's thread-safe,
  IP-fixing install makes safe to do (patching a prologue another mod is also in)
  — but the chaining logic is kcdx's, above the patcher.
- The v1 contracts (design §6.3 — stated, not silently chosen):
  - **Load order:** kcdx runs first (`game → kcdx → foreign → original`) —
    load-order-by-install-time, the same model the kcdx chain already uses.
  - **Foreign-unhooks-later + foreign-installs-after-kcdx are OUT OF SCOPE for v1**
    (design §6.3, §10) — documented limitations, not v1 guarantees. v1 covers a
    foreign hook present at kcdx's install time, living for the session. Do NOT
    build handling for the out-of-scope cases; do log a clear note if the foreign
    target's shape is one v1 can't safely chain (surface, don't silently mishandle
    — `anti-patterns.md` AP14).
- Chain-always policy (design §6.4): kcdx chains onto a detected foreign hook; NO
  configurable policy (that's design-settled out-of-scope, §10).
- The `comp-NN` fixture: a two-plugin (or one-plugin-installs-both) fixture that
  installs a SYNTHETIC foreign E9 on a target, then kcdx hooks the same target;
  both detours' fire is observable + ordered. (`comp-NN` is the conflict/interaction
  matrix id per `test-suite.md`; pick the next free `comp-NN`.)

**Test bar.** The `comp-NN` two-mod fixture run live: a synthetic foreign E9 is
installed on a target, kcdx hooks it, and BOTH the foreign detour and kcdx's chain
fire, in the defined order (`game → kcdx → foreign → original`). A FALSIFIABLE
claim: the row FAILS if EITHER detour does not fire, or if they fire out of order.
The fixture self-reports via `kcdx.test.report` / `ReportTestResult`; agent builds
+ deploys + enables dev mode, user launches once, agent reads `kcdx-dev.log`
(`agent-builds-and-deploys.md`). This is a permanent regression row
(`test-suite.md`) — foreign-chaining can never silently regress.

**Dependencies.** Step 7 (the classifier must flag foreign before chaining onto
it). Phase 2 step 4 (`SafetyhookBackend` — the safe-patch foundation the chaining
rests on, design §6.2). Phase 2 step 5 (routing).

**Design authority.** [`hook-backend-marriage.md §6.2, §6.3, §6.4`](../../../design/hook-backend-marriage.md)
+ US-4 — the chaining mechanism + the load-order contract + the v1 scope
boundaries are built to the design, not this summary.

**Disassembler-test / author-burden note.** None — foreign-chaining is fully
engine-internal; an author runs kcdx alongside another mod and gets coexistence
for free, with no new declaration.

**Reference.** [`../context.md`](../context.md) E14/E20 + the §6.3/§10
out-of-scope boundaries.
