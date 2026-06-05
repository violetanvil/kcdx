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
- Install the interception per the **P1-SETTLED verdict (PROBE P11 v2, 2026-06-05):
  hook `lua_newstate` (callee, id 114)** so the engine's `CScriptSystem::Init` call
  returns kcdx's state, and Init runs its own `storedebug=0` / `luaL_openlibs` /
  3-extension-registrar sequence ON kcdx's state. The narrow hook is CONFIRMED SAFE
  (the state is virgin `storedebug=1` at creation; static evidence — seed id 121 —
  confirms Init overwrites with no read-branch on a virgin field). The
  hook-`CScriptSystem::Init`-itself fallback is NOT needed (P1 falsified the
  read-branch risk) — do not build it.
- Validate the single-state + mainthread (`[L->l_G+0xB0]==L`) invariants at install.
- **Cross-thread awareness (the gate is P4's, but P3 must not assume single-thread).**
  P1 v2 verified: kcdx builds the VM on the **worker thread**; the engine reaches
  `CScriptSystem::Init` (and the `lua_newstate` call this step intercepts) on the
  **game main thread**. So the kcdx-side VM-build (this step) and the engine's
  adoption call are on DIFFERENT threads. This step builds the VM-build + the
  intercept; it does NOT itself open boot assets, so it does not own the boot-open
  gate (that is P4 step 1's mandatory event gate, design §5). But P3 MUST ensure the
  VM + `kcdx.*` tables are fully published (release/acquire happens-before) before the
  game thread's intercept reads them — the adoption is a cross-thread handoff, gated
  the same disciplined way (`.claude/rules/concurrency.md`), never a timing assumption.
- Still coexists with static Lua (dropped in P5) — this isolates the adoption.

## Test bar

A `test-plugins/cap-NN-vm-adopt/` regression self-reports: exactly one `lua_State`
(no second `lua_newstate`); `[L->l_G+0xB0]==L`; `kcdx.*` tables present in the
adopted state; a CryEngine script call (`System.LogAlways`) still runs on the adopted
state. PROBE Q silent. The single-state assertion is the falsifiable claim. Runnable
at this step. Confirmed by the user's launch + the agent's dev-log read. (P1 v2
already observed the baseline this preserves: one engine `lua_newstate`,
`mainthread_self=1` — this step asserts the SAME invariants hold when kcdx owns the
state.)

## Dependencies

P1 step 1 (the intercept-point verdict this step builds), P2 steps 1+2 (the shim
must resolve before the VM build), P3 step 2 (WHGame must be force-loaded before the
shim resolves its symbols).

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §1 (kcdx owns the VM) + §4.1 (the
intercept point — SETTLED to the narrow `lua_newstate`-callee hook by PROBE P11 v2,
recorded in the §4 PROBE-SETTLED note) + §3 (the verified `CScriptSystem::Init`
sequence) + §5 (the cross-thread adoption is gated/published, never timed). Build to
the §4.1 settled verdict, NOT to this doc's summary (`.claude/rules/spec-conformance.md`).

## RE / author-burden note

`CScriptSystem::Init` (id 121) and `lua_newstate` (id 114) resolve by id through the
Address Library, never a hardcoded RVA (AP1). The interception's exact form is the
P1 probe's output, not an author-supplied offset.

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E10, E2; design §1, §4.1.
