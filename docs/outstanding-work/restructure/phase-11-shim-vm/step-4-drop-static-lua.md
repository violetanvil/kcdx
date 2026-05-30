# Phase 11d — lift Lua-in-before_game restriction, drop static Lua

**Status: BLOCKED** on 11a–c. Ledger row: [`README.md`](README.md) → 11d.

## What

With the VM running via the shim before CryEngine init, Lua plugins can declare
`zone = "before_game"`. Then drop kcdx's own compiled Lua entirely — the final
hazard-killing step.

## Scope

- Lua plugins can declare `zone = "before_game"`; their `plugin.lua` runs before
  CryEngine reaches its own `luaL_newstate`. Timing chain: before_game patches →
  WHGame DllMain → VM spun up via shim → before_game Lua plugins.
- Drop `vendor/lua/*.c` from the build; the `lua` static library leaves
  CMakeLists.txt. Keep `vendor/lua/*.h` (struct defs for PROBE Q + diagnostics).
- `src/lua_shim.cpp` defines every `LUA_API` / `LUALIB_API` symbol as a forwarder.
- Revert FIX C's `vendor/lua/ltable.c` setnodevector patch — no longer needed once
  there's no kcdx-side compiled Lua.
- PROBE Q stays as the permanent regression canary.
- The `kcdxLuaApi` plugin-DLL surface becomes a direct forwarder to the same shim —
  C++ DLL plugins get the same one-body Lua.

## Hazard retirement

Both directions of the dual-Lua sentinel hazard retire here: FIX C (kcdx→WHGame)
and KI-0001's reverse (kcdx's GC freeing WHGame's `.rdata` dummynode). One compiled
Lua body remains.

## Depends on

11a + 11b + 11c.

## Test bar

A `before_game`-zone Lua plugin runs before CryEngine init; suite stays green with
static Lua dropped; PROBE Q silent; the bugsplat fix could migrate to a Lua plugin
(the DLL form already works — no migration required).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 11" → "What changes in
the engine" + "Outcome".
