# P6 step 1 — drop vendor/lua *.c, revert FIX C, kcdxLuaApi→shim, lift zone gate

## What

The hazard-killing step. With the one VM running and adopted (P3) and the startup
contract + early slot proven (P5), drop kcdx's own compiled Lua entirely — every
`lua_*`/`luaL_*` forwards
through the shim. One compiled Lua body remains; the dual-Lua sentinel hazard is
impossible by construction. Lift the `before_game` Lua zone gate (the VM is up at
DllMain now).

## Scope

- Drop `add_library(lua STATIC ...)` from `CMakeLists.txt`; remove `lua` from
  `target_link_libraries`. Keep `target_include_directories(... vendor/lua)` (headers
  for struct defs + PROBE Q).
- `src/lua_shim.cpp` defines every `LUA_API`/`LUALIB_API` symbol as a forwarder
  through `g_api` (the linker now resolves the Lua symbols to the shim, not vendored
  `.c`).
- Revert FIX C's `vendor/lua/ltable.c::setnodevector` patch (unneeded — no kcdx-side
  compiled Lua).
- `kcdxLuaApi` plugin-DLL surface becomes a direct forwarder to the same shim (C++
  DLL plugins get the one-body Lua).
- Lift the zone-gate manifest error for Lua `zone = "before_game"`.
- PROBE Q stays (permanent canary, unchanged).
- **Survivor sweep** (`.claude/rules/deletion-hygiene.md`): dropping vendored Lua
  compilation + FIX C — sweep `docs/`, `.claude/rules/lua-bridge.md`, `CLAUDE.md` for
  prescriptive references to FIX C / static-linked Lua as the current state; repoint
  to FIX A shipped.

## Test bar

The full suite stays green with static Lua dropped. **PROBE Q reads ZERO** across a
full save-load cycle (the canonical dual-Lua repro — the falsifiable hazard-death
claim). A new `test-plugins/cap-NN-lua-before-game/` declares `zone = "before_game"`
+ a `plugin.lua` calling `kcdx.hook`, and self-reports the hook fired BEFORE
CryEngine init. The engine binary shrinks (no compiled Lua). Confirmed by the user's
launch + the agent's dev-log read.

## Dependencies

P3 step 3 (the adopted VM must work before kcdx's own Lua can be dropped — dropping
static Lua before adoption works leaves no Lua at all), P2 (the shim must forward
every symbol the dropped `.c` provided), P5 (the startup contract + the early
slot/swap are verified on the coexisting build before the drop changes linkage).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §6.3 (the drop + FIX C revert + forwarder
+ zone-gate lift) + [`../../fix-a-drop-static-lua.md`](../../../fix-a-drop-static-lua.md)
§"Files that need to change" (the exact CMake + file edits).

## RE / author-burden note

No author hex. The zone-gate lift lets an author declare `zone = "before_game"` by
name. No new DB rows.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E14, E15, E19; design §6.3.
