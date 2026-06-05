# P2 step 1 — forward the 93 resolved fns + Resolve() + gating

## What

Create `src/lua_shim.{h,cpp}`: the `kcdx::lua_shim::LuaApi` function-pointer struct
sized for all 117 `LUA_API`/`LUALIB_API` symbols, and `kcdx::lua_shim::Resolve()`
that populates it. This step wires the RESOLVED functions (forward each by resolving
its CANONICAL NAME through `refdb::ResolveAddrByName("<lua_fn>")` — the same by-name
resolution the keystone probe used; NOT a hardcoded id range), the
bail-loud-on-required-miss behavior, and the internal-only gating. The ~24 stubs are
the next step.

> **Seed reality (verified 2026-06-05, supersedes the design's stale "1100-range"
> note):** the Lua API surface is seeded at LOW ids (~1–130: `lua_pcall`=1,
> `luaL_loadfile`=3, `luaL_ref`=125, `luaC_barrierf`=127, the internal helpers
> 128–130) — NOT a "1100-range" (that was the pre-2026-05-28 single-CSV numbering,
> renumbered in the three-file seed split per `lua-bridge.md`). 111 lua-named rows
> exist. **Resolve by NAME, never by a baked id range** — it is robust against exactly
> this renumbering and is the established `refdb` pattern.

## Scope

- `src/lua_shim.h` — declare `LuaApi` (the fn-ptr struct) + `Resolve()` + the
  `g_api` accessor.
- `src/lua_shim.cpp` — for each resolved fn, populate `g_api.<fn>` from
  `refdb::ResolveAddrByName("<canonical lua name>")`. `Resolve()` returns false + logs
  loud if any REQUIRED symbol fails (a known-stripped fn without a stub is not yet
  wired — that is step 2).
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
observation corroborates). The Address Library Lua-API seed rows already exist (low
ids ~1–130, resolved by name).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §6.1 (the shim structure, bail-loud,
internal-only gating). The harvest doc
[`../../fix-a-drop-static-lua.md`](../../../fix-a-drop-static-lua.md) §"Files that
need to change" is the load-bearing spec for the fn list + signatures.

## RE / author-burden note

Every Lua symbol resolves by canonical NAME through the Address Library
(`refdb::ResolveAddrByName`), never a hardcoded RVA (AP1,
`.claude/rules/no-hardcoded-addresses.md`) and never a baked id (robust against the
id renumbering that already happened). The Lua-API rows are already seeded (~ids
1–130); this step adds NO new DB rows (AP18 not triggered).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E5, E8; design §6.1.
