# Phase 9.5 / P1 — core surface (Lua, main-stop)

The probe that discharges the design's marked assumptions, then the registry, the
apply boundary, the window law + teaching errors, the toggle contract, persisted
edges, and the auto-order method. Design: [`../behavior-design.md`](../behavior-design.md).

## Step ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| [1 — startup-ordering probe + static-evidence reads](step-1-startup-probe.md) | DONE | 736481d |
| [2 — behavior_registry + declare/get/list](step-2-registry-declare.md) | DONE | 7080dd0 |
| [3 — set + the apply boundary (worklist drain)](step-3-set-boundary.md) | DONE | 08e2f2a |
| [4 — window law + resolution errors + in-memory edges](step-4-window-law-errors.md) | DONE | 5295397 |
| [5 — revert / post-load toggle contract](step-5-toggle-revert.md) | DONE | 1176742 |
| [6 — edge persistence + launch-time recognition](step-6-edge-persistence.md) | NOT STARTED | — |
| [7 — the auto-order method](step-7-auto-order.md) | NOT STARTED | — |

## Phase verification gate

Build green + the P1 fixture set GREEN in a live launch (the agent reads the
suite line per `.claude/rules/test-suite.md`): declare/get/list + validation
errors; the cross-plugin set story end-to-end at the boundary; all five
resolution branches + the out-of-window error; the toggle contract incl. both
failure dispositions; the launch-time stale-edge warn; the auto-order method
correcting a mis-ordered fixture + reporting a cycle. The §14 rows this phase
owns are recorded in the matrix.
