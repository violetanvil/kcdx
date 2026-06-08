# P5 step 6 — the before_game apply-driver (queued intent ACTUALLY INSTALLS early)

## What

Close the "full control" gap: wire `ApplyZone(BeforeGame)` so a `kcdx.hook` /
`kcdx.bytes` declared in the early slot (step 5) is INSTALLED before the engine's init
call, not merely queued. Today the ONE registry apply-driver
(`src/lua_registry.cpp::ApplyZone`) is invoked ONLY for the `AfterGame` slice
(`src/hooks.cpp` first-tick) — there is NO `ApplyZone(BeforeGame)` call site, so a
before_game-zoned hook/patch declared through the registry applies NOTHING
(`docs/init.md` §"STUBBED"; `ldr_notify` walks an unpopulated `patch::g_patches`).
This step wires the before_game INVOCATION of the existing one driver — `docs/init.md`
migration step 3 ("the one apply-driver unification"), scoped to the before_game zone.

## Scope

- Add the `ApplyZone(BeforeGame)` call site: the worker, after the early slot runs (its
  `kcdx.hook`/`kcdx.bytes` calls have queued their Kind::Hook/Kind::Bytes registry
  entries — step 5) and before the engine reaches the targeted init call, invokes the
  SAME `lua_registry::ApplyZone` with the `before_game` slice of the resolved
  load-order list — every Kind routes through the one driver, in load-order order,
  exactly as the `AfterGame` slice does.
- This is the before_game INVOCATION of the existing driver — NOT a redesign of the
  after_game path (live + correct, untouched) and NOT a separate before_game apply
  logic. The zones are two invocation points of ONE driver (`docs/init.md` §"The ONE
  apply-in-load-order flow").
- The Kind::Hook/Kind::Bytes deferred-apply handlers are already registered
  (`RegisterHandlers`, ctx-B) — the before_game invocation reuses them.
- Ordering: the before_game apply completing before the engine's targeted init call is
  the cross-thread dependency the Phase-4 event gate orders (the same gate shape the
  boot-asset serve uses). A before_game hook whose target the engine calls BEFORE the
  worker reaches the apply point (the BugSplat-class LDR-time case) stays the
  self-registration hatch's job (design §7.2) — NOT this driver.
- Survivor sweep (`.claude/rules/deletion-hygiene.md`): `docs/init.md` §"STUBBED" + the
  migration-step-3 "PENDING" note are repointed to "wired (before_game zone)".

## Test bar

A `test-plugins/cap-NN-before-game-apply/` (suite-gated): a before_game-zoned plugin's
`lua_before` queues a `kcdx.hook` on a function the engine calls during init; the hook
FIRES when the engine calls the target during init (a self-reporting row that FAILS if
the hook never fired — proving the apply-driver DRAINED the before_game slice, NOT just
that the slot ran). NOT a tautology: the row reads the actual hook-fire, not that the
slot executed. The probe for §8 claim 7 (does the engine call the chosen target AFTER
the apply point?) is resolved at this step's head — pick a before_game target the
engine calls after the worker apply point, or surface if none is reachable. PROBE Q
silent. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

P5 step 5 (the early slot must QUEUE before_game entries for the apply-driver to
drain — the slot runs first, this installs what it queued). The Kind::Hook/Kind::Bytes
handlers (already registered, ctx-B). The Phase-4 foundation (the event gate orders
the apply vs the targeted init call).

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §7.5 (the before_game apply-driver
— the before_game invocation of the one driver) + §6 US-8 (the install-early
acceptance) + §8 claim 7 (the targeted-init-call ordering, provisional — probe the
chosen target). `docs/init.md` §"The ONE apply-in-load-order flow" + migration step 3
(the one-driver model this realizes). Build to §7.5, not this summary.

## RE / author-burden note

No author hex. The author declares `kcdx.hook{ target = "<name>" }` by name (the
engine carries the address + ABI — `.claude/rules/cornerstones.md`); the apply-driver
owns the timing. The §8-claim-7 probe reads the chosen target's call timing against the
worker apply point (a checkable runtime fact, not a guess). No new DB rows unless the
chosen before_game target is not yet seeded (then its resolution is an earlier
`/research-disassembly` + AP18-gated seed, surfaced).

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" row "The before_game apply-driver
(ApplyZone(BeforeGame))"; design §7.5, §6 US-8, §8 claim 7.
