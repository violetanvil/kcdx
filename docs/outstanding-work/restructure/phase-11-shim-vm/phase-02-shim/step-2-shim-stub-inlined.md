# P2 step 2 — kcdx-side stubs for the ~24 inlined/stripped (GC-barrier-safe)

## What

Add the kcdx-side stubs for the ~24 functions confirmed inlined-by-PGO or
linker-stripped in WHGame.dll (they have no callable RVA). Each stub reimplements the
function using the verified layout constants + already-resolved primitives, per the
harvest doc's per-function strategy table. GC-pointer-writing stubs call
`luaC_barrierf` — the non-negotiable safety constraint.

## Scope

- Implement the stubs per
  [`../../fix-a-drop-static-lua.md`](../../../fix-a-drop-static-lua.md) "Stripped or
  inlined functions" table: `lua_gettop` → `(int)(L->top - L->base)`;
  `lua_pushnil/boolean/number/integer/lightuserdata/thread` → direct TValue write +
  `L->top++`; `luaL_register`/`luaI_openlib` → reimplement the inlined body using
  resolved primitives; `lua_status`/`lua_storedebuginfo` → the documented byte reads;
  etc.
- **GC-barrier safety (hard):** any stub writing a GC pointer (`lua_pushthread`,
  `lua_replace`, …) calls `luaC_barrierf` (`0x3997070`) — without it the incremental
  GC can free live objects.
- **Layout validation at init:** stubs reading WHGame struct fields validate the
  mainthread self-pointer invariant (`[L+0x20]+0xB0 == L`) so a future game-update
  struct shift fails LOUD, not silent.
- `Resolve()` now treats the known-stripped set as stub-backed (not a required-miss).

## Test bar

Extend `cap-NN-lua-shim-forward` (or a sibling `cap-NN-lua-shim-stubs/`): each stub
class is exercised through `g_api` and self-reports correctness (e.g.
`lua_pushnil`+`lua_type`==nil; `luaL_register` installs a table). **A per-stub
GC-barrier test row** asserts a GC-pointer-writing stub invoked the barrier (the
GC-barrier-safety invariant — `plan-spec.md`). Runnable at this step (coexists with
static Lua). PROBE Q silent.

## Dependencies

P2 step 1 (the stubs populate the same `LuaApi`/`g_api` the forwards do; the resolved
primitives several stubs reuse are wired there).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §6.1 (the stub set + GC-barrier
constraint + layout-const validation). The harvest doc's "Stripped or inlined
functions" + "What NOT to do" tables are the load-bearing per-function spec.

## RE / author-burden note

The stubs rest on harvested layout constants + RVAs resolved by id (e.g.
`luaC_barrierf` `0x3997070`), not hardcoded source RVAs — a constant used by a stub
resolves through the Address Library where it is an address, and the layout offsets
are verified facts from the harvest, not author-supplied. No new DB rows (AP18 not
triggered).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E6, E7; design §6.1.
