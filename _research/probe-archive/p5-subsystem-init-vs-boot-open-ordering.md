# PROBE INITORDER — subsystem-init vs the boot-asset open: the ordered-init rule is BUILDABLE

**Run:** `kcdx-dev_2026-06-07_20-05-24.log` (live, dev-mode, launch-to-menu).
Clean, no FAULTED. Observe-only (no reorder, no behavior change — 8 timestamped
observation points across the existing init sites + the first boot-asset open).

**Trust:** primary evidence (live observation, tid + wall_ms on every line).
Settles the checkable runtime-mechanism claim the Phase-5 ordered-init design
rests on (the keystone proved only the VM build moves ahead of the boot open;
this proves the full early-relevant subsystem set can — INCLUDING console).

## The question

The Phase-5 "ordered-init" rule (supersedes the declare-vs-act rule) requires:
kcdx inits ALL early-relevant subsystems in dependency order on the worker →
THEN runs the early before_game plugins against a fully-initialized kcdx → THEN
the game launches (boot-asset open). For that, every early-relevant kcdx
subsystem must be able to init BEFORE the engine's first boot-asset open. Static
source-reading showed 5 of 6 already init on the worker; `console::Init` is the
odd one out (game-thread first-tick, `src/hooks.cpp`). The load-bearing unknown:
is `gEnv->pConsole` (IConsole) available at the worker's pre-boot-open point, so
`console::Init` CAN move to the worker? (console::Init resolves `gEnv_pConsole`
and derefs it; it fails loud if the deref is null — `src/console.cpp:459-474`.)

## Observed (8 points, tid + wall_ms on every line)

| Point | thread (tid) | wall_ms | vs. first boot open |
|---|---|---|---|
| refdb::Open | 11224 (worker) | 1561109 | −2937 ms before |
| hook/bytes RegisterHandlers | 11224 (worker) | 1562234 | −1812 ms before |
| save_load_hooks::Install | 11224 (worker) | 1563312 | −734 ms before |
| serialization::Init | 11224 (worker) | 1563312 | −734 ms before |
| asset_overlay::Install | 11224 (worker) | 1563406 | −640 ms before |
| **console_available_at_worker** | 11224 (worker) | 1563406 | `storage_resolved=1`, `iconsole_nonnull=1`, `iconsole=0x1CE58A699C0` |
| **first boot-asset open** (`./config/cvargroups/sys_spec_characters.cfg`) | **6068 (game-main)** | 1564046 | — (reference point) |
| console::Init (current site) | 6068 (game-main) | 1575203 | **+11157 ms AFTER** |

## Verdict — the ordered-init rule is BUILDABLE (gEnv->pConsole is up early)

- **5 of 6 subsystems already init on the WORKER before the boot open** (refdb,
  hook/bytes handlers, save_load_hooks, serialization, asset_overlay) — confirmed
  at runtime, all on the worker tid (11224), all preceding the boot open
  (1564046). They are already correctly ordered; the ordered-init rule keeps them.
- **console::Init is the ONLY late one** — game-main tid (6068), +11.2 s AFTER the
  boot open. It is the one subsystem the ordered-init rule must move forward.
- **THE DECIDER: `iconsole_nonnull=1` at the worker pre-boot-open point** — a real
  IConsole pointer (`0x1CE58A699C0`), `gEnv_pConsole` storage resolved, on the
  worker, BEFORE the boot open. **Therefore `console::Init` CAN move to the worker:
  its only hard dependency (a non-null gEnv->pConsole) is already satisfied at the
  worker point.** The ordered-init rule is fully buildable for the whole early set.
- **The boot open is CROSS-THREAD from the worker** (game-main 6068 vs worker
  11224) — consistent with PROBE P4: the early slot (worker) → boot open
  (game-main) dependency is ordered by the Phase-4 EVENT GATE, never a wall-clock
  margin. The ~640 ms worker-asset_overlay-to-boot-open margin is the race the gate
  kills, not the guarantee.

The design's ordered-init assertion was CORRECT; the probe upgrades it from
asserted to proven, and settles the one open mechanism (console-movability).

## What the Phase-5 design revision builds on this (the proven topology)

- The early-relevant kcdx subsystems init in dependency order on the WORKER,
  BEFORE the boot open. Five already do; `console::Init` moves from the game-main
  first-tick (`src/hooks.cpp:499`) to the worker init phase (`src/dllmain.cpp`,
  after asset_overlay::Install — the point where `iconsole_nonnull=1` was
  observed). `cvar::Init` rides the same gEnv->pConsole precondition
  (`src/hooks.cpp:505`) and moves with it.
- The three "gap" verbs the sweep found dissolve: console is up by the slot →
  `kcdx.command` is just early; serialization is up by the slot →
  `kcdx.cosave` registration is early; `kcdx.scan` is imperative-but-early-
  runnable (WHGame mapped) — docs corrected to say it ACTS, not declares.
- The early/late split is "needs only kcdx (+ mapped WHGame) vs needs the LIVE
  GAME", and subsystem-readiness is a STRUCTURAL guarantee (kcdx fully inits on
  the worker before the slot), not a per-verb probe-around.
- The worker→game-main boot-open dependency stays gated by the Phase-4 event
  gate (cross-thread, proven again here).

## Caveat for the build (not blocking — a build-time check)

The probe proves `gEnv->pConsole` is non-null at the worker point. It does NOT
prove `console::Init`'s OTHER resolves succeed there (`IConsole_AddCommand` /
`RemoveCommand` / `ExecuteString` / `PrintLine` via `refdb::ResolveAddrByName`)
— those resolve from refdb's in-memory cache, which is up (refdb::Open precedes,
worker tid). So they should resolve; the move's build step confirms console::Init
returns true at the worker site (a regression row asserts a worker-registered
command dispatches). Moving cvar::Init likewise rides the same precondition.

## Reusable wiring

- The init-order instrumentation recipe: a one-line `LOG_DEBUG_KV(
  "PROBE_INITORDER", "subsystem_init", subsys/tid/wall_ms)` at each subsystem's
  existing init call site (worker: `src/dllmain.cpp`; game-thread: `src/hooks.cpp`
  first-tick latch); a one-shot (file-scope atomic CAS) `first_boot_open` marker
  at the `asset_overlay` `RecordBootOpen` site (HOOK 1 + HOOK 2 share the flag);
  the console-availability read = the SAME `refdb::ResolveAddrByName("gEnv_pConsole")`
  + deref `console::Init` uses (`src/console.cpp:459-469`), logged read-only.
- ALWAYS log tid — the cross-thread fact (worker vs game-main) is load-bearing.
- `GetTickCount64()` for wall_ms, `GetCurrentThreadId()` for tid (both via
  `<windows.h>`, already included in all three sites).
- Probe removed from source post-run; finding here.
