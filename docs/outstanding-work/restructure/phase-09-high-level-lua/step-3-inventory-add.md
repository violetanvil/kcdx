# Phase 9 step 3 — `kcdx.inventory.add(item_id, count)`

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

`kcdx.inventory.add(item_id, count)` — real, working, tested. Probably the
most-requested TC primitive.

## Scope

- RE the inventory-add path (the game function that grants an item to the player
  inventory + its ABI); Address Library entry by name; the name carries the
  verified signature (the engine, not the author, holds the ABI).
- Bind `kcdx.inventory.add`.
- `docs/lua/` entry in the same step.

## Disassembler test

`kcdx.inventory.add(item_id, count)` — the author supplies an item id and a count,
both game-domain values they already know. No hex. Clean.

## Test bar

`cap-XX-inventory-add`: calls `kcdx.inventory.add(<known item>, 1)` and asserts
the item count rose by 1 (read back via the inventory). Test mode `in-game`
(loaded save).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9" → "Ships real".
