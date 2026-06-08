# Phase 5 — the startup-sequence author contract (control · visibility · docs)

kcdx's startup sequence becomes a single AUTHOR-FACING contract: the internal phase
model ([`src/init_phase.h`](../../../../../src/init_phase.h)) is promoted to one
documented timeline the engine ORDERS by (control), authors OBSERVE + REACT to
(visibility), and authors LEARN from (docs). Folds in the ordered-init correction
(console + cvar move to the worker — PROBE INITORDER-proven) and the before_game
early slot + boot swap (KI-0005). Built on the Phase-4 foundation (the cross-thread
event gate + the `RegisterRuntimeOverlay` two-writer CAS).

**Settled design:** [`bring-forward-design.md`](bring-forward-design.md) (v2,
committed `ee9c744`, design-fidelity gated PROCEED). Coverage map:
[`../plan-spec.md`](../plan-spec.md) §"Phase 5 — the startup-sequence author contract".

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — worker GC-safety probe (a worker subsystem-bind + store-write is PROBE-Q-silent)](step-1-worker-gc-safety-probe.md) | NOT STARTED | — |
| [2 — move console + cvar to the worker; add the kcdx-subsystems-ready phase](step-2-console-cvar-worker-subsystems-ready.md) | NOT STARTED | — |
| [3 — promote the phase model: lifecycle event per phase + reconcile existing messages](step-3-phase-events-reconcile.md) | NOT STARTED | — |
| [4 — the `kcdx.startup.*` query API (phase / at_least + C++ accessor)](step-4-startup-query-api.md) | NOT STARTED | — |
| [5 — the `lua_before` early slot + the worker before-game runner](step-5-lua-before-runner.md) | NOT STARTED | — |
| [6 — the before_game apply-driver (queued hook/bytes ACTUALLY INSTALLS early)](step-6-before-game-apply-driver.md) | NOT STARTED | — |
| [7 — boot-asset serve via the early slot (KI-0005) + the AP14 warn decision](step-7-boot-asset-serve.md) | NOT STARTED | — |
| [8 — the kcdx-driven C++ before-game entry](step-8-cpp-before-game-entry.md) | NOT STARTED | — |
| [9 — the author startup-sequence doc (the timeline)](step-9-startup-doc.md) | NOT STARTED | — |

## Phase verification gate

- **Build green + the contract is live + author-confirmed.** Every author-reachable
  phase fires its lifecycle event at the right phase on the right thread; the
  `kcdx.startup.*` query agrees with `g_phase`; a `lua_before` plugin runs on the
  worker pre-boot-open; an out-of-window call fails loud. Confirmed by the user's
  launch + the agent's `kcdx-dev.log` read.
- **The ordered-init move holds** (PROBE INITORDER-proven): `console::Init` +
  `cvar::Init` run on the worker before the boot open; a worker-registered console
  command dispatches; the kcdx-subsystems-ready phase advances on the worker
  pre-boot-open. PROBE Q stays silent across the moves + the worker binds.
- **Full before_game CONTROL** (the apply-driver, design §7.5/§8.7): a before_game
  `kcdx.hook` declared in the early slot ACTUALLY INSTALLS — it FIRES when the engine
  calls the target during init (proving `ApplyZone(BeforeGame)` drained the slice, not
  just that the slot ran). Closes `docs/init.md`'s STUBBED before_game apply path.
- **The boot-asset swap serves** (KI-0005, user-facing acceptance per
  `.claude/rules/ux-first-class.md`): the user sees a boot asset render REPLACED via
  the early-slot runtime path; the agent confirms `rt=HIT` from the dev log; the
  order-inversion regression (Phase-4 gate) holds.
- **Full Lua+C++ parity** (`.claude/rules/lua-api-surface.md`): every phase event,
  the query API, and the early entries are reachable + tested from BOTH surfaces.
- **The startup sequence is documented** (`.claude/rules/docs-discipline.md`): the
  author startup-sequence doc + the per-call entries (`docs/lua/`, `docs/cpp/`) +
  the glossary terms land; `docs/init.md` cross-references the author doc.
