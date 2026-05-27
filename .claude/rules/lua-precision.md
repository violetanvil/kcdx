---
paths:
  - "src/lua_bind*.cpp"
  - "src/scripting.*"
  - "include/kcdx/Interfaces.h"
  - "src/rom_borrowed/runtime_func_t.*"
---

# Lua numeric precision — pointers must not go through lua_Number

CryEngine's Lua 5.1 is built with `LUA_NUMBER=float`. `sizeof(lua_Number)=4`, `sizeof(lua_Integer)=8`. `lua_pushinteger(L, 0x7FFD46781D00)` followed by `lua_tointeger(L, -1)` returns `0x7FFD46800000` — silently, with no error. Integers below 2^24 round-trip exactly; values around 2^31 round to the nearest 256; pointer-magnitude values (~2^47) round to a 16 MB grid.

## Rules

- **Returning a pointer/VA from C to Lua**: use `kcdx::lua_bind_helpers::PushPointer(L, pointer(addr))` (exact, gives plugins a `kcdx.memory.pointer` userdata) or `lua_pushlightuserdata` (exact, no methods). **Never `lua_pushinteger` for a pointer.**
- **Receiving a pointer/VA from Lua to C**: accept pointer userdata or lightuserdata. If a binding also accepts integer for ergonomics, document the precision limit and static-assert the integer path is for testing only.
- **`kcdxLuaApi::PushInteger`/`PushNumber` exist** for plugin DLL parity with SKSE shape. Their docstrings in `include/kcdx/Interfaces.h` carry the warning. Cannot be removed.
- **`value_wrapper_t::push_value`'s `integer_` case is lossy.** Proper fix: new `type_info_t::ptr_` variant routing through `PushPointer`. Tracked as Phase 5g follow-up.

## Probe

`kcdx.lua._probe_numbers()` (live-confirmed 2026-05-19). Repro: drop `lua-memory-verify/` pak into game with dev mode on, grep `kcdx-dev.log` for `NUMBER_PROBE`.

Full data: `docs/lua-number-precision.md`.
