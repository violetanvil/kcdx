# P5 step 2 — move console + cvar to the worker; add the kcdx-subsystems-ready phase

## What

Move `console::Init` + `cvar::Init` from the game-thread first-tick latch
(`src/hooks.cpp`) to the worker (`src/dllmain.cpp`, after `asset_overlay::Install`),
and add the new `kcdx-subsystems-ready` ctx-B phase to `src/init_phase.h`. PROBE
INITORDER PROVED this is safe: `gEnv->pConsole` (console::Init's only hard
dependency) is non-null at the worker pre-boot-open point. After this step every
kcdx subsystem (refdb, console, cvar, asset seam, serialization, hook/bytes handlers)
is up on the worker BEFORE the boot open — the ordered-init spine of the startup
timeline.

## Scope

- Move `kcdx::console::Init()` + `kcdx::cvar::Init()` from `src/hooks.cpp`'s
  first-update-tick latch to `src/dllmain.cpp`'s worker sequence, after
  `asset_overlay::Install()` (the proven point — PROBE INITORDER `iconsole_nonnull=1`
  there). console::Init's deferred-registration queue (already present,
  `src/console.cpp`) keeps working; cvar::Init shares the same `gEnv->pConsole`
  precondition.
- Add `KcdxSubsystemsReady` (name per the design's author-token reconciliation) to
  the `InitPhase` enum (append per the enum's discipline); the worker
  `AdvanceTo(KcdxSubsystemsReady)` after the moved inits + the existing worker
  subsystem inits complete, before the boot open.
- Verify console::Init's OTHER resolves (`IConsole_AddCommand`/`RemoveCommand`/
  `ExecuteString`/`PrintLine`) succeed at the worker point (they resolve from refdb's
  in-memory cache, up since `refdb::Open` — confirm console::Init returns true on the
  worker).
- Survivor sweep (`.claude/rules/deletion-hygiene.md`): console.cpp's comment "plugin.lua
  runs BEFORE console::Init() in the first-update-tick" + any doc/rule prose that
  states console inits on the game thread is repointed to the worker.

## Test bar

A `test-plugins/cap-NN-console-on-worker/` (next free cap-NN): a worker-registered
console command DISPATCHES (the command was registered at the worker phase, not the
game-thread latch — FAILS if console::Init didn't run / the command never registers);
the kcdx-subsystems-ready phase ADVANCED on the worker tid pre-boot-open (a row reads
`g_phase` + `GetCurrentThreadId()` against the boot-open marker — FAILS if it advanced
on game-main or after the boot open). PROBE Q silent across the moves. Confirmed by
the user's launch + the agent's dev-log read.

## Dependencies

P5 step 1 (the worker GC-safety probe must confirm a worker bind is PROBE-Q-silent
before console/cvar bind on the worker). PROBE INITORDER (the move is proven safe —
archive, already done).

## Design authority

[`bring-forward-design.md`](bring-forward-design.md) §3 (the ordered-init rule + the
PROBE INITORDER proof that console+cvar move) + §4 (kcdx-subsystems-ready as a
reachable ctx-B phase) + §9 (the units: init_phase.h, dllmain.cpp/hooks.cpp).
[`../../../../../src/init_phase.h`](../../../../../src/init_phase.h) is the phase
enum's existing append-discipline. Build to the design §3/§4, not this summary.

## RE / author-burden note

No author hex. console::Init resolves `gEnv_pConsole` + the IConsole vtable methods
by canonical NAME through refdb (already seeded; no new DB rows). The move changes
WHERE Init runs, not how it resolves.

## Reference

[`../plan-spec.md`](../plan-spec.md) §"Phase 5" rows "console::Init + cvar::Init move"
+ "New ctx-B phase: kcdx-subsystems-ready"; design §3, §4.
