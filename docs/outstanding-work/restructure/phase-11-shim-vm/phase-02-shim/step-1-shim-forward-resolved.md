# P2 step 1 — forward the 93 resolved fns + Resolve() + gating

## What

Create `src/lua_shim.{h,cpp}`: the `kcdx::lua_shim::LuaApi` function-pointer struct
sized for all 117 `LUA_API`/`LUALIB_API` symbols, and `kcdx::lua_shim::Resolve()`
that populates it. This step wires the 93 RESOLVED functions (forward through
`address_library::Resolve(id)`, the 1100-range ids), the bail-loud-on-required-miss
behavior, and the internal-only gating. The ~24 stubs are the next step.

## Scope

- `src/lua_shim.h` — declare `LuaApi` (the fn-ptr struct) + `Resolve()` + the
  `g_api` accessor.
- `src/lua_shim.cpp` — for the 93 resolved fns, populate `g_api.<fn>` from
  `address_library::Resolve(<id>)`. `Resolve()` returns false + logs loud if any
  REQUIRED symbol fails (a known-stripped fn without a stub is not yet wired — that
  is step 2).
- Internal-only gating: `lua_close`/`lua_newstate`/`lua_setallocf`/`lua_atpanic`
  resolvable internally but NOT exposed through `kcdxLuaApi`.
- Coexists with the static-linked Lua at this point (no `vendor/lua` change here).

## Test bar

A permanent `test-plugins/cap-NN-lua-shim-forward/` regression: a shim call through
`g_api` (e.g. `g_api.lua_pushinteger(L, 42)` then read back) lands the value on the
stack, self-reporting the canonical acceptance signal (`ACCEPT-RESULT` /
`ACCEPT-SUITE`, `test-suite.md`). A negative row forces a required-symbol miss and
asserts `Resolve()` bails loud. Runnable at this step (the shim coexists with static
Lua — a live `lua_State` exists to call into). PROBE Q silent.

## Dependencies

P1 step 1 (the probe confirms the resolution targets behave as the harvest says —
the shim's resolved-fn list rests on the harvest, which the probe's single-VM
observation corroborates). The Address Library 1100-range seeds already exist.

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §6.1 (the shim structure, bail-loud,
internal-only gating). The harvest doc
[`../../fix-a-drop-static-lua.md`](../../../fix-a-drop-static-lua.md) §"Files that
need to change" is the load-bearing spec for the fn list + signatures.

## RE / author-burden note

Every Lua symbol resolves by NAME/id through the Address Library, never a hardcoded
RVA (AP1, `.claude/rules/no-hardcoded-addresses.md`). The 1100-range ids are already
seeded; this step adds NO new DB rows (AP18 not triggered).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E5, E8; design §6.1.
