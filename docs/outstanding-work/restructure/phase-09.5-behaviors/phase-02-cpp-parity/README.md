# Phase 9.5 / P2 — C++ parity + the command queue

`kcdxBehaviorInterface` mirrors the proven Lua surface (the in-phase parity the
design settled), then the command-query thread contract's queued half. Design:
[`../behavior-design.md`](../behavior-design.md) §8 + §5.4.

## Step ledger (step-grain)

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdxBehaviorInterface` + the value-handle model](step-1-interface-handles.md) | DONE | (landed) |
| [2 — `Invoke` + the off-thread command queue](step-2-invoke-queue.md) | NOT STARTED | — |

## Phase verification gate

Build green + the C++ fixture legs GREEN in a live launch: all four verbs from
C++ main-stop surfaces; cross-language set both directions at main stops; the
handle model (coercion, traversal, generation-staleness, builders); `Invoke` on
a callable value; the queued off-thread set end-to-end (FIFO, attribution,
`get()` flips at execution, table payload); the query thread wall; the C++
early-stop out-of-window fixture replacing step 4's harness stub. Parity is
tested, not assumed (`.claude/rules/lua-api-surface.md` §"FULL FEATURE PARITY").
