# Phase 5 — drop static Lua (the hazard-killing step)

With the one VM running and adopted (Phase 3), drop kcdx's own compiled Lua entirely.
`vendor/lua/*.c` leaves the build; every `lua_*`/`luaL_*` forwards through the shim.
This is the step that makes the dual-Lua hazard impossible by construction — one
compiled Lua body remains. The `before_game` zone gate lifts here (Lua plugins can
declare it now that the VM is up at DllMain).

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — drop vendor/lua *.c, revert FIX C, kcdxLuaApi→shim, lift zone gate](step-1-drop-static-lua.md) | NOT STARTED | — |

## Phase verification gate

- **Build green** with `vendor/lua/*.c` dropped from the build (the `lua` static
  target gone; `vendor/lua/*.h` kept for struct defs + PROBE Q).
- The full suite stays green; the engine binary shrinks (no compiled Lua).
- **PROBE Q reads zero** across a full save-load cycle (the canonical dual-Lua repro)
  — the permanent canary, confirmed by the agent's dev-log read after the user's
  launch.
- FIX C's `vendor/lua/ltable.c::setnodevector` patch is reverted (no longer needed).
- A new `before_game`-zone Lua test plugin's hook fires before CryEngine init — the
  zone-gate restriction is lifted; a regression row self-reports the early fire.
