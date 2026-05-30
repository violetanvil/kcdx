# Phase 9 step 1 — `kcdx.player.health`

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

`kcdx.player.health` with `:get()`, `:set(n)`, `:add(n)` — real, working, tested.
The first of the three end-to-end capabilities that prove the namespace
architecture (Address Library lookup → pointer arithmetic → typed Lua
read/write).

## Scope

- RE the player-health read/write path (player struct + field offset); land the
  Address Library entry(ies) by name per the reuse-first evidence ladder
  (`/research-disassembly` if the offset isn't already resolved).
- Bind `kcdx.player.health` in `src/lua_bind*.cpp` with the three methods.
- `docs/lua/` entry in the same step (docs-discipline).

## Disassembler test

The author writes `kcdx.player.health:set(50)` — a named gameplay value, no hex.
The engine carries the address + the typed access. Clean.

## Test bar

`cap-XX-player-health` (step 5 owns the plugin, or this step self-checks): asserts
`set(50)` then `get() == 50`. Test mode `in-game` (needs a loaded save with a
player). Declare the gesture + observable.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9" → "Ships real".
