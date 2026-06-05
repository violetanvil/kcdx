# P3 step 2 — worker builds the one state; engine adopts it (no force-load)

## What

The keystone. kcdx's WORKER thread waits for the game to map WHGame
(`WaitForGameDll`, already built), then — at the post-`refdb::Open` point where
WHGame is mapped and `lua_newstate` resolves — builds the ONE `lua_State` itself via
the shim's `lua_newstate`, registers `kcdx.*` tables, and installs the P1-settled
interception on `CScriptSystem::Init`'s VM-creation so the engine ADOPTS kcdx's state
and never allocates its own. **No force-load** — PROBE P3 verified a kcdx DllMain
force-load of WHGame is impossible (CREATE_SUSPENDED + loader lock) and unnecessary
(the game maps WHGame itself; the worker waits). This step folds the prior
"force-load" step: there is nothing to force-load; the WHGame-mapping pipeline
(`WaitForGameDll` + the LDR before_game apply) ALREADY EXISTS and this step CONFIRMS
it, does not rebuild it.

## Scope

- **No force-load; confirm the existing pipeline.** kcdx does NOT `LoadLibraryW`
  WHGame (impossible). The worker thread's `WaitForGameDll` (exists, `src/dllmain.cpp`
  + `src/ldr_notify.cpp`) blocks until the game maps WHGame; the LDR before_game apply
  pass (`ApplyEntriesForModule` per mapped module + the WHGame-loaded `SetEvent`)
  already fires. This step verifies that pass fires per module (the bugsplat fix's
  `BugSplat64.dll` target applies when WHGame's chain maps) and measures the
  worker-startup budget — it does not build new mapping machinery.
- **Build the VM on the worker.** `src/dllmain.cpp`: at the post-`refdb::Open` point
  (WHGame mapped, `lua_shim::Resolve()` succeeds), call the shim's `lua_newstate` →
  the one state. Register `kcdx.*` tables.
- **Install the interception per the P1-SETTLED verdict (PROBE P11 v2): hook
  `lua_newstate` (callee, id 114)** so the engine's `CScriptSystem::Init` call returns
  kcdx's state, and Init runs its own `storedebug=0` / `luaL_openlibs` /
  3-extension-registrar sequence ON kcdx's state. The narrow hook is CONFIRMED SAFE
  (the state is virgin `storedebug=1` at creation; static evidence — seed id 121 —
  confirms Init overwrites with no read-branch on a virgin field). The
  hook-`CScriptSystem::Init`-itself fallback is NOT needed (P1 falsified the
  read-branch risk) — do not build it.
- Validate the single-state + mainthread (`[L->l_G+0xB0]==L`) invariants at install.
- **Cross-thread adoption — published, never timed (hard).** PROBE P3 + P1 v2
  verified: kcdx builds the VM on the **worker thread**; the engine reaches
  `CScriptSystem::Init` (the `lua_newstate` call this step intercepts) on the **game
  main thread**, ~2 s later. The worker-builds → game-adopts handoff is a cross-thread
  dependency: the worker MUST publish the built VM + `kcdx.*` tables with a release
  edge before the game thread's intercept observes them (acquire) — an explicit
  happens-before, NEVER a wall-clock margin (`.claude/rules/concurrency.md`;
  `~/.claude/memory` — the timing-window-as-explanation is forbidden). The boot-open
  gate (the early slot signals → the boot-open path blocks) is P4's mandatory event
  gate (§5); THIS step owns the VM-adoption publish edge.
- Still coexists with static Lua (dropped in P5) — this isolates the adoption.

## Test bar

A `test-plugins/cap-NN-vm-adopt/` regression self-reports: the game BOOTS (a bad
adoption AVs — boot is the falsifiable observable); exactly one `lua_State` (no second
`lua_newstate`); `[L->l_G+0xB0]==L`; `kcdx.*` tables present in the adopted state; a
CryEngine script call (`System.LogAlways`) still runs on the adopted state; the LDR
before_game apply fired per mapped module. PROBE Q silent. The single-state assertion
is the falsifiable claim. Runnable at this step. Confirmed by the user's launch + the
agent's dev-log read. (P1 v2 already observed the baseline this preserves: one engine
`lua_newstate`, `mainthread_self=1` — this step asserts the SAME invariants hold when
kcdx owns the state. cap-79 — the P2 shim's pinned contract — FLIPS to PASS here, the
launch that wires `Resolve()` at init.)

## Dependencies

P1 step 1 (the intercept-point verdict this step builds; PROBE P3's no-force-load /
worker-build finding), P2 steps 1+2 (the shim must resolve before the VM build), P3
step 1 (the `early_hook` primitive the before_game apply drives). The
WHGame-mapping pipeline (`WaitForGameDll` + LDR apply) already exists.

## Design authority

[`../lua-vm-design.md`](../lua-vm-design.md) §1 (kcdx owns the VM) + §6.2 (no
force-load — the game maps WHGame, kcdx waits) + §6.4 (worker VM build + cross-thread
adoption) + §4.1 (the intercept point — SETTLED to the narrow `lua_newstate`-callee
hook) + §3 (the verified `CScriptSystem::Init` sequence) + §5 (the cross-thread
adoption is published/gated, never timed). PROBE P3 archive:
`_research/probe-archive/p3-vm-build-timing.md`. Build to the §4.1/§6.2 settled
verdicts, NOT this doc's summary (`.claude/rules/spec-conformance.md`).

## RE / author-burden note

`CScriptSystem::Init` (id 121) and `lua_newstate` (id 114) resolve by id through the
Address Library, never a hardcoded RVA (AP1). The interception's exact form is the P1
probe's output, not an author-supplied offset. The force-load that the prior step doc
named is GONE (PROBE P3 — impossible + unnecessary).

## Reference

[`../plan-spec.md`](../plan-spec.md) coverage rows E9 (the WHGame-mapping pipeline,
confirmed-not-built), E10, E2; design §6.2, §6.4, §1, §4.1.
