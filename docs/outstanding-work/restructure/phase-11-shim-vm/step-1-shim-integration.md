# Phase 11a — FIX A shim integration

**Status: BLOCKED** on FIX A 100%. Ledger row: [`README.md`](README.md) → 11a.

## What

Wire the FIX A symbol shim: every `lua_*` / `luaL_*` call routes through a
function-pointer table resolved against WHGame.dll's compiled Lua.

## Depends on

FIX A symbol harvest at 100% (~110 RVAs verified, Address Library populated). FIX A
is independent work ([`../fix-a-drop-static-lua.md`](../../fix-a-drop-static-lua.md));
11a starts when FIX A reports done.

## Scope

- Add `src/lua_shim.cpp` symbol forwarders (already scaffolded at ~54 functions;
  expand to 110+).
- Populate the Address Library with all Lua RVAs (range 1100–1199).
- Add `kcdx::lua_shim::Resolve()` — runs after WHGame.dll is mapped; resolves every
  function pointer by `GetProcAddress` / Address Library Resolve.

## Test bar

A standalone test calls `kcdx::lua_shim::g_api.lua_pushinteger(L, 42)` through the
shim and verifies the value lands on the stack. PROBE Q canary stays silent. No
suite-count change — the shim COEXISTS with kcdx's compiled Lua at this point (the
static Lua is dropped only at 11d).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 11a".
