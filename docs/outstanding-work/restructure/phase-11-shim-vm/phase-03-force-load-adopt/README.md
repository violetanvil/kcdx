# Phase 3 — force-load WHGame + VM build + Init adoption

The keystone build. kcdx force-loads WHGame.dll from DllMain, builds the one Lua VM
via the shim, and the engine ADOPTS it through the Phase-1-decided interception. Still
coexists with static Lua (dropped in Phase 5) — this phase proves the single-state
adoption works while the static Lua is present, so a regression isolates the
adoption.

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — relocate/generalize the early-install primitive → src/early_hook](step-1-early-hook-relocate.md) | NOT STARTED | — |
| [2 — force-load WHGame.dll from DllMain + LDR before_game apply](step-2-force-load-whgame.md) | NOT STARTED | — |
| [3 — kcdx builds the one state; engine adopts it (Init interception)](step-3-vm-build-and-adopt.md) | NOT STARTED | — |

## Phase verification gate

- **Build green.** The game boots with WHGame force-loaded from DllMain (a bad
  force-load AVs — boot is the falsifiable observable). LDR notification fires per
  mapped module; before_game targets apply at the right mapping.
- The single-state assertion holds: exactly one `lua_State` (no second
  `lua_newstate`), `[L->l_G+0xB0]==L` (mainthread self-pointer). PROBE Q silent.
- `kcdx.*` tables are present in the adopted state; CryEngine's own scripts
  (`System.LogAlways` etc.) still run — they execute on kcdx's state via WHGame's
  compiled Lua.
- Loader-lock budget logged (kcdx.dll DllMain start → worker-thread spawn) — <200ms
  target, <500ms hard cap.
- A permanent regression row self-reports the single-state + adoption invariants.
