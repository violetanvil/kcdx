# Phase 9 step 4 — namespace stubs

**Status: DEFERRED → [`TD-0005`](../../../tech-debt/TD-0005-high-level-lua-surface.md)** (item 4). Ledger row: [`README.md`](README.md) → step 4.

## What

Register the namespace tables for everything that lands incrementally:
`kcdx.player.*` (other accessors), `kcdx.world.*`, `kcdx.dialogue.*`,
`kcdx.quest.*`. Each has documented expected functions that log "not yet
implemented; tracking in design-gap #16" and return nil.

## Why

Stubs let plugin authors write code referencing the future API and have it fail
loudly + meaningfully when it calls something not yet done — instead of a nil-index
crash. This is the namespace skeleton; the 3 real items (steps 1–3) are what keep
this from being a ship-80% stub-only phase.

## Scope

- Register the stub tables + their documented function names.
- Each stub: structured-KV log line naming the function + the design-gap, returns
  nil.
- `docs/lua/` documents the namespace + the "stub, not yet implemented" status of
  each entry.

## Test bar

`cap-XX` sub-test: a stub call logs the NYI line and returns nil (asserts the
failure is loud + meaningful, not a crash).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9" → "Ships namespace
stubs".
