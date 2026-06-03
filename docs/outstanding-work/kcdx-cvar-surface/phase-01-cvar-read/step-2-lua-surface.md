# Step 2 — Lua surface `kcdx.cvar.get_int / get_bool / get_float`

## What

Register the `kcdx.cvar.*` domain sub-table with three reader functions, each
calling the step-1 `cvar::` core:

- `kcdx.cvar.get_int(name)` → number (the CVar's int) or nil + a teaching error if
  the CVar does not exist / surface not ready.
- `kcdx.cvar.get_bool(name)` → boolean — `cvar::GetInt(name) != 0`.
- `kcdx.cvar.get_float(name)` → number (the CVar's float).

Raw Lua C API per `lua-bridge.md` (no static-const sentinels): one C thunk per
reader, `lua_pushcfunction` + `lua_setfield` into a `cvar` sub-table on the `kcdx`
global. Numeric push per `lua-precision.md` — the int path pushes exactly (document
the LUA_NUMBER=float limit for values above 2^24; CVar ints are small flags/modes
in practice), the float path via `lua_pushnumber`.

## Scope

`src/lua_bind_cvar.cpp` + its registration site (where the other `kcdx.*` surfaces
register into the global, e.g. the lua-binding init that sets up `kcdx.console`,
`kcdx.log`, etc.). Single-commit.

## Test bar

Exercised at step 4 (cap-71's Lua side reads a known CVar). Build green.

## Dependencies

Step 1 (the `cvar::` core the thunks call). Ordered after it.

## Design authority

`plan-spec.md` §"Settled design decisions" — the `kcdx.cvar.*` names (the user's
call), `get_bool = get_int != 0`. The surface shape obeys `lua-api-surface.md`
(domain sub-table, `kcdx.cvar.get_*`; required arg `name` positional; errors teach).
No new design call; an error-message wording or a nil-vs-error-on-missing-CVar
choice the spec leaves open → surface it.

## Disassembler-test / author-burden

The author writes `kcdx.cvar.get_int("sys_pakPriority")` — a name only, no
address/offset/ABI. The engine carries the resolution. Compliant by construction.

## Rules

`lua-api-surface.md` (the domain-sub-table surface shape + errors-teach),
`lua-bridge.md` (raw C API, no sentinels), `lua-precision.md` (numeric push),
`naming-namespaces.md` (the `kcdx.*` reserved root), `logging.md`,
`cornerstones.md`.
