# Phase 9 — high-level Lua surface: 3 real capabilities + namespace structure

**Status: DEFERRED → [`TD-0005`](../../../tech-debt/TD-0005-high-level-lua-surface.md).**
Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9". Addresses
`docs/design-gaps.md` gap #16.

This phase is a **non-blocking leaf** (it consumes the Address Library from
Phase 9.1 + the existing Lua binders; nothing downstream consumes
`kcdx.player.*`), so it is carried as deliberate debt rather than held open on
the active restructure ledger. **This subdir stays the authoritative per-step
build spec** the closing build phase consumes — TD-0005 is the tracked-debt
handle that points HERE, it does not duplicate the spec. The step ledger below
reads `DEFERRED` (the work is specced + unbuilt, parked under TD-0005's single
named blocker: a dedicated high-level-Lua-surface build phase).

Commits to THREE real, working gameplay capabilities that prove the namespace
design end-to-end (Address Library lookup → pointer arithmetic → typed Lua
read/write), plus the namespace skeleton for everything else that lands
incrementally. The stubs alone would be the "ships 80%" anti-pattern — the 3 real
items are what make the architecture honest. One `/feature` cycle; independent of
the other live phases; pure RE + binder work.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.player.health` (:get/:set/:add)](step-1-player-health.md) | DEFERRED → TD-0005 | — |
| [2 — `kcdx.player.position` (:get; :set if RE-confirmed safe)](step-2-player-position.md) | DEFERRED → TD-0005 | — |
| [3 — `kcdx.inventory.add(item_id, count)`](step-3-inventory-add.md) | DEFERRED → TD-0005 | — |
| [4 — namespace stubs (`player.*`/`world.*`/`dialogue.*`/`quest.*`)](step-4-namespace-stubs.md) | DEFERRED → TD-0005 | — |
| [5 — test plugins (`cap-XX-player-*` / `cap-XX-inventory-*`)](step-5-test-plugins.md) | DEFERRED → TD-0005 | — |

Each real capability ships with its Address Library entries (added this phase if
absent), a test plugin, and full `docs/lua/` docs in the same step.

## Verification gate (whole phase)

A test plugin calls `kcdx.player.health:set(50); assert(kcdx.player.health:get()
== 50)` — failing the assert fails the test. Same for position and inventory.add.
A stub call logs "not yet implemented; tracking in design-gap #16" and returns
nil.
