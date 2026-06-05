# P3 step 3 — kcdx builds the one state; engine adopts it (Init interception)

## What

The keystone. After the force-load (step 2) maps WHGame and the shim (P2) is
resolved, kcdx builds the ONE `lua_State` itself via the shim's `lua_newstate`,
registers `kcdx.*` tables, and installs the Phase-1-decided interception on
`CScriptSystem::Init`'s VM-creation so the engine ADOPTS kcdx's state and never
allocates its own.

## Scope

- `src/dllmain.cpp`: after force-load + `lua_shim::Resolve()`, call the shim's
  `lua_newstate` → the one state. Register `kcdx.*` tables.
- Install the interception per the Phase-1 verdict: **lean — hook `lua_newstate`
  (callee `0x14492A8`)** so the engine's `CScriptSystem::Init` call returns kcdx's
  state, and Init runs its own `storedebug=0` / `luaL_openlibs` / 3-extension-registrar
  sequence ON kcdx's state. (Fallback if P1 found Init reads a virgin-state field:
  hook `CScriptSystem::Init` itself and replicate the post-newstate sequence — P1's
  outcome decides which.)
- Validate the single-state + mainthread invariants at install.
- Still coexists with static Lua (dropped in P5) — this isolates the adoption.

## Test bar

A `test-plugins/cap-NN-vm-adopt/` regression self-reports: exactly one `lua_State`
(no second `lua_newstate`); `[L->l_G+0xB0]==L`; `kcdx.*` tables present in the
adopted state; a CryEngine script call (`System.LogAlways`) still runs on the adopted
state. PROBE Q silent. The single-state assertion is the falsifiable claim. Runnable
at this step. Confirmed by the user's launch + the agent's dev-log read.

## Dependencies

P1 step 1 (the intercept-point verdict this step builds), P2 steps 1+2 (the shim
must resolve before the VM build), P3 step 2 (WHGame must be force-loaded before the
shim resolves its symbols).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §1 (kcdx owns the VM) + §4.1 (the
intercept point + the lean/fallback) + §3 (the verified `CScriptSystem::Init`
sequence). Build to the §4.1 verdict P1 recorded, NOT to this doc's summary
(`.claude/rules/spec-conformance.md`).

## RE / author-burden note

`CScriptSystem::Init` (id 121) and `lua_newstate` (id 114) resolve by id through the
Address Library, never a hardcoded RVA (AP1). The interception's exact form is the
P1 probe's output, not an author-supplied offset.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E10, E2; design §1, §4.1.
