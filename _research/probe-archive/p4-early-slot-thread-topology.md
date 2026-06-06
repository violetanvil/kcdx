# PROBE P4 — early-Lua-slot thread topology: cross-thread (the §5 event gate is the proven mechanism)

**Run:** `kcdx-dev_2026-06-05_20-16-02.log` (live, dev-mode, launch-to-menu). Clean,
no FAULTED. Observe-only (no slot, no gate, no asset registered — three timestamped
observation points).

**Trust:** primary evidence (live observation, tid on every line). Settles a
checkable runtime mechanism the P4 step docs ASSERTED (cross-thread → event gate) but
which the keystone had put in question (was it same-thread?). The probe-first rule
(`results-driven.md` §"a design clause asserting a runtime mechanism is a probe
target") applied — and here the probe CONFIRMS the assertion (unlike PROBE P3, which
overturned the force-load assertion).

## The question

P4 must run a plugin's early Lua slot EARLY ENOUGH to precede the engine's
boot-asset open (so a `kcdx.assets.register/replace` wins the boot asset — KI-0005).
The step docs assert "the slot runs on the worker thread, the boot open on game-main
→ cross-thread → build an event gate." That was unproven post-keystone (the keystone
showed `plugin.lua` runs game-main today; maybe a same-thread slot suffices). Probe
the actual topology.

## Observed (three points, tid on every line)

| Point | thread (tid) | wall_ms |
|---|---|---|
| **A** worker VM-publish (`lua_vm_build`, post-PublishLuaState) | **46672 (worker)** | 464890546 |
| **B** first boot-asset open (`asset_overlay` HOOK 1/2) | **24980 (game-main)** | 464892031 |
| **C** first-tick latch (`hooks.cpp`, where `plugin.lua`/RunAll runs today) | 24980 (game-main) | 464902359 |

## Verdict — CROSS-THREAD; the §5 event gate is the PROVEN mechanism (outcome 1)

- **A.wall < B.wall** (464890546 < 464892031, ~1.5 s): the worker VM-publish point
  precedes the first boot-asset open. A worker slot CAN run before the boot open.
- **A.tid ≠ B.tid** (worker 46672 vs game-main 24980): the worker slot point and the
  boot open are on DIFFERENT threads → a genuine cross-thread dependency.
- **C.wall > B.wall** (464902359 > 464892031, ~10 s): today's `plugin.lua` point
  (game-main first-tick latch) runs ~10 s AFTER the boot open — the KI-0005 gap,
  anchored. There is NO viable game-main Lua point before the boot open (the earliest
  game-main Lua is the first-tick latch, which is 10 s too late). So a same-thread
  slot is NOT viable.

**Therefore:** the early slot runs on the WORKER (the only point early enough); the
boot open is on game-main; the dependency (boot-open must see the slot's asset
registration) is CROSS-THREAD. The §5 mandatory event gate IS the right mechanism —
the slot (worker) signals a readiness event after its register; the boot-open path
(game-main, `asset_overlay` HOOK 1/2) waits-and-BLOCKS on it. NOT program-order (the
threads differ), NOT a wall-clock margin (that ~1.5 s is the race the gate kills, not
the guarantee), NOT the existing freeze (`NotifyVmReady` is the late-path teaching
mechanism, not the early-path serve).

The step docs' gate conclusion was CORRECT; the probe upgrades it from asserted to
proven, and confirms the same-thread alternative is closed. Design §5/§6.4 + the P4
step docs stand as-written (no correction needed — validated, not overturned).

## What P4 builds (on this proven topology)

- **P4 step 1:** the early Lua slot runs on the WORKER, after the VM is published
  (point A), before the boot open. It registers the plugin's `kcdx.assets.*` then
  signals a NEW readiness event. The boot-open path (`asset_overlay` HOOK 1/2,
  game-main) `WaitForSingleObject`s that event (bounded timeout) and BLOCKS until
  signaled before resolving a boot-asset overlay — the §5 mandatory event gate. The
  order-inversion regression FAILS if the boot open resolves the store before the slot
  signaled.
- **Slot shape (the P1 §4.4 candidate-A-vs-B question):** the slot is WORKER-thread,
  so it is NOT the existing game-thread `plugin.lua` (RunAll) path (that's point C,
  game-main + too late). It is a NEW worker-run early entrypoint (candidate B's shape
  — `plugin.lua` cannot move to the worker without moving ALL of RunAll, which the
  keystone keeps game-thread for the kcdx.* registration). NOTE this resolves the §5
  candidate question toward B (a distinct early entrypoint) — surfaced for the build:
  reusing game-thread `plugin.lua` (candidate A) is ruled out by the topology (it runs
  game-main, 10 s after the boot open).

## Reusable wiring

- A = worker tid, post-`PublishLuaState` in `lua_vm_build`. B = game-main tid, the
  `asset_overlay` `RecordBootOpen` boot-open path. C = game-main tid, the `hooks.cpp`
  first-tick `done`-latch.
- ALWAYS log tid — the cross-thread fact is the whole point.
- The gate's event is a NEW edge (neither `g_kcdxReadyEvent` — ctor-bracket — nor
  `g_whgameLoadedEvent` — worker WHGame wait — covers it).
- Probe removed post-run; finding here.
