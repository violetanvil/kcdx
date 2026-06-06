# Phase 3 — worker builds the one VM + Init adoption (no force-load)

The keystone build. kcdx's worker thread waits for the game to map WHGame
(`WaitForGameDll`, already built), then builds the one Lua VM via the shim and the
engine ADOPTS it through the P1-settled `lua_newstate`-callee interception. **No
force-load** — PROBE P3 (2026-06-05) verified a kcdx DllMain force-load of WHGame is
impossible (CREATE_SUSPENDED + loader lock) and unnecessary (the game maps WHGame;
the worker waits). The prior "force-load from DllMain" framing was a design defect,
corrected in `lua-vm-design.md` §6.2/§6.4; the old force-load step is folded into the
VM-build step (it described building a force-load that can't exist + an LDR apply pass
that already does). Still coexists with static Lua (dropped in Phase 5) — this phase
proves the single-state adoption works while static Lua is present, so a regression
isolates the adoption.

## Step ledger (step-grain)

Status: `NOT STARTED` · `BLOCKED` · `DONE` · `NEEDS REWORK`. Commit = short hash when
`DONE`, `—` otherwise.

| Step | Status | Commit |
|---|---|---|
| [1 — relocate/generalize the early-install primitive → src/early_hook](step-1-early-hook-relocate.md) | DONE | 18c0ac5 |
| [2 — worker builds the one state; engine adopts it (Init interception, no force-load)](step-2-vm-build-and-adopt.md) | DONE | 3b99fea |

## Phase verification gate

- **Build green + the game BOOTS** with the worker-built VM adopted (a bad adoption
  AVs — boot is the falsifiable observable). The WHGame-mapping pipeline
  (`WaitForGameDll` + the existing LDR before_game apply) fires: the LDR notification
  applies before_game targets per mapped module at the right mapping (confirmed, not
  rebuilt).
- The single-state assertion holds: exactly one `lua_State` (no second
  `lua_newstate`), `[L->l_G+0xB0]==L` (mainthread self-pointer). PROBE Q silent.
- `kcdx.*` tables are present in the adopted state; CryEngine's own scripts
  (`System.LogAlways` etc.) still run — they execute on kcdx's state via WHGame's
  compiled Lua.
- The cross-thread adoption is PUBLISHED, not timed: the worker publishes the VM +
  `kcdx.*` tables with a release edge before the game thread's intercept observes them
  (acquire) — an explicit happens-before, never a wall-clock margin (design §5/§6.4).
- Worker-startup budget logged (kcdx.dll DllMain start → worker VM-build point).
- A permanent regression row self-reports the single-state + adoption invariants; the
  P2 shim's pinned cap-79 contract FLIPS to PASS here (this is the launch that wires
  `Resolve()` at init).
