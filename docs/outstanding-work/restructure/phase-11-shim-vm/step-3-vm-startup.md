# Phase 11c — Lua VM startup via shim, hook game's `luaL_newstate`

**Status: BLOCKED** on 11a. Ledger row: [`README.md`](README.md) → 11c.

## What

Spin up the Lua VM via WHGame's compiled `luaL_newstate` (through the shim), then
hook CryEngine's own later `luaL_newstate` call to return our pre-allocated state —
so there is exactly one VM in the process and it is WHGame's.

## Scope

- Call WHGame's `luaL_newstate` via `kcdx::lua_shim` — the `lua_State*` is allocated
  by WHGame's compiled Lua, with WHGame's sentinels.
- Register the `kcdx.*` Lua tables in the new state.
- Hook CryEngine's startup `luaL_newstate` to return our state — CryEngine never
  creates its own; it operates on ours, using its own compiled Lua (the same body
  the shim resolved).

## Depends on

11a (shim) + 11b (WHGame mapped).

## Test bar

The VM spins up via the shim; `kcdx.*` tables are present in the state; CryEngine's
`luaL_newstate` is intercepted (it does not allocate a second state — verifiable by
a single-state assertion / sentinel-set check). PROBE Q stays silent.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 11" mechanism steps 8–11.
