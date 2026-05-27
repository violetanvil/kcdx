# kcdx initialization & startup ordering

The authoritative contract for how kcdx boots: what initializes, in what order,
in which execution context, and how load-order application is driven. The
**mandate** (2026-05-26): startup is a DECLARED phase order — every subsystem is
spun up before the phase that needs it, application happens in one
load-order-driven flow, and adding a subsystem at the wrong phase FAILS LOUD.
No emergent "statement-order + comments" sequencing.

This doc has two parts: the **target phase model** (the contract the code
implements) and the **as-is sequence** (today's boot, until the refactor lands).
The as-is audit + gaps live in
[`known-issues/init-sequencing-audit.md`](known-issues/init-sequencing-audit.md);
this doc is the forward design + the live reference.

> STATUS (2026-05-26): the phase model below is DESIGNED + APPROVED (via
> /senior-architect-consult, all-Option-A). The pure behavior-preserving refactor
> that implements it is NOT yet landed — the as-is sequence (§"As-is") is what
> the code does today. This doc updates to "implemented" when the refactor lands
> + its verification-checkpoint boot passes.

## The three execution contexts (hard physical constraints, not choices)

kcdx startup spans three contexts, in this real-time order. A phase is pinned to
exactly ONE context, and the context's safety class is a PROPERTY of the phase
(encoded, not commented):

| Ctx | Thread / when | Capability + safety class |
|---|---|---|
| **A** | `DllMain(DLL_PROCESS_ATTACH)`, under the LOADER LOCK, before WHGame.dll is mapped (launcher injected us into a CREATE_SUSPENDED game process) | LOADER-SAFE ONLY: VirtualProtect/memcpy, `LdrRegisterDllNotification`, std:: containers, tomlplusplus. NO MinHook init, NO CreateThread-dependent work, NO LoadLibrary. (`loader-architecture.md` loader-safety contract.) |
| **B** | `WorkerThread`, a normal thread spawned by DllMain, runs at WHGame-MAPPED time (before WHGame's DllMain, far before `CSystem::Init`) | FULL capability: MinHook, threads, file I/O. |
| **C** | the game's MAIN THREAD, first `update` tick, after `CSystem::Init` has run | FULL capability + the engine is live (lua_State up, gEnv populated). |

Why three: WHGame.dll's DllMain is the only natural phase break in KCD2 startup
(the before/after-game sentinel in `load-order.md`). Context A is "before the
game's code runs"; B is "engine mapped, not yet initialized"; C is "engine
live." `WaitForGameDll` (the B-entry gate) signals at DLL-MAPPED time — NOT
init-complete — so B runs well before `CSystem::Init`.

## The phase model (the contract)

A single ordered `enum class InitPhase`, a `g_phase` advanced explicitly as the
boot progresses, and `KCDX_REQUIRE_PHASE(p)` assertions at use sites. A read or
op that requires phase ≥ X asserts it; a subsystem placed at the wrong phase
trips the assert LOUDLY (closes the bug class where the FOpen probe silently read
an unset game version because it sat in the wrong slot — audit gap #4).

Phases, in order, each tagged with its context:

```
InitPhase (ordered)                  ctx  what is guaranteed up by this phase
─────────────────────────────────────────────────────────────────────────────
0  PreInit                            A    paths::Init, log session stamp
1  ConfigLoaded                       A    every kcdx.toml parsed; load order RESOLVED
2  VersionDetected                    A    g_runtimeGameVersion known (reads
                                           kcd_launcher.log; needs the game-root
                                           PATH, NOT WHGame mapped) — everything
                                           version-gated (address_library::Resolve)
                                           depends on >= this phase
3  BeforeGameApply                    A    before_game load-order slice applied
                                           (the ONE apply-driver, before_game zone)
                                           + LDR notifications armed
─── (DllMain returns; WorkerThread already spawned) ───
4  WorkerInit                         B    log::Init, exception filter, watchdog
5  GameDllMapped                      B    WaitForGameDll returned; WHGame.dll mapped
6  EngineHooksInstalled               B    hooks::Install (lua_pcall + update);
                                           MinHook live
7  ModLoaderTakeoverArmed             B    [absorb] the C_ModManager SELECT detour
                                           installed — PLACEMENT U.6-GATED (see below)
8  EngineSubsystemsInit               B    save_load_hooks, serialization (after
                                           save_load), Kind handlers (before plugins)
9  PluginsLoaded                      B    DiscoverAndLoad; Plugin_Preload/Load fired
─── (game begins executing; CSystem::Init runs; first update tick) ───
10 AfterGameApply                     C    after_game load-order slice applied
                                           (the ONE apply-driver, after_game zone) +
                                           KCDX Lua table registered
```

Rules the enum enforces:
- **Monotonic.** `g_phase` only advances. An op asserting `>= X` run before X
  trips loud.
- **Context-pinned.** Each phase names its context; a context-A phase is
  statically known to be loader-lock-limited (a MinHook call in a ctx-A phase is
  a design error the phase identity surfaces).
- **Spin-up-before-use.** A subsystem initializes at the EARLIEST phase before
  any phase that uses it. Version detection (phase 2) precedes every
  version-gated read; Kind handlers (phase 8) precede plugin load (phase 9);
  save/load messages (phase 8) precede serialization (phase 8, ordered within).

## The ONE apply-in-load-order flow

Load order is RESOLVED ONCE (phase 1, `load_order::Resolve` → one ordered list).
A SINGLE apply-driver is the only code that applies entries; it is invoked at
each zone boundary with that zone's SLICE of the one list:

- **before_game slice** → applied at phase 3 (ctx A), in load-order order.
- **after_game slice** → applied at phase 10 (ctx C), in load-order order.

"One flow" = one resolved list + one apply function. The zones are not separate
apply logic — they are just the two INVOCATION POINTS of the one driver, dictated
by the physical WHGame-DllMain boundary (a before_game patch MUST be applied
before the game's code runs; an after_game hook needs the live engine). Every
application KIND (patch/bytes, hook, mid-hook, trampoline, and the asset-overlay
mounts the absorb adds) routes through this one driver. This is the realization
of "apply in the order the load order says" (`load-order.md`) without pretending
the zone split doesn't exist.

## The mod-loader absorb (asset overlays) in the model

The kcdx "absorb the KCD2 mod loader" feature (FINDINGS
`_research/phase8.5-pak-resolver/FINDINGS.md` §"ABSORB DESIGN — APPROVED")
slots in as:
- **Phase 7 `ModLoaderTakeoverArmed`** — the SELECT-orchestrator detour
  (`wh::C_ModManager` `FUN_180da104c`) is kcdx INFRASTRUCTURE (like
  `hooks::Install`), NOT a load-order entry. It must be armed before
  `CSystem::Init` runs the native SELECT.
- **Asset-overlay entries** (a plugin's / mod's assets) ARE load-order entries,
  applied THROUGH the takeover: kcdx, now owning SELECT, builds the engine's
  enabled-mod list FROM the resolved load order's asset slice — so overlays apply
  via the one apply-driver, in load-order order, same as every other kind.
- **PLACEMENT IS U.6-GATED.** Whether phase 7 can sit in context B (worker
  thread) depends on the unproven runtime fact: does a worker-thread detour
  install BEFORE `CSystem::Init` reaches SELECT? PROBE U.6 (the log-only SELECT
  detour, `src/probes/mod_loader_probe.{h,cpp}`, built but held out of the boot
  until the refactor lands) resolves it: fires → phase 7 stays in B; never fires
  while the native "[Mod] N mods enabled" line appears → the takeover must move
  to context A (loader-lock, the before_game/Phase-11 LDR machinery), and phase 7
  relocates accordingly. Do not finalize phase 7's context until U.6.

## As-is (today, until the refactor lands)

The current boot does the SAME operations in the SAME order, but expressed as
statement order in two functions + comment paragraphs — NOT a phase enum, no
asserts. Full as-is mapping + the implicit dependencies + the gaps:
[`known-issues/init-sequencing-audit.md`](known-issues/init-sequencing-audit.md).
Summary of where the code is now:
- `dllmain.cpp` `RunBeforeGameZoneInDllMain()` = ctx A (phases 0–3, un-enumerated).
- `dllmain.cpp` `WorkerThread()` = ctx B (phases 4–9, un-enumerated; the FOpen
  probe sits after DiscoverAndLoad because it needs the game version — a
  dependency currently encoded only as placement + a comment).
- `hooks.cpp` `HookedUpdate` first-tick block = ctx C (phase 10).

KNOWN GAPS the refactor closes (audit §Gaps): emergent ordering (no enum/assert);
probes inserted by convenience (the FOpen-probe-broke-on-version=0 bug);
apply scattered across 3+ sites; the version lifecycle hole; and the absorb's
hard one-shot constraint the current model can't express.

## Migration plan (locked, all-Option-A)

1. **Pure behavior-preserving refactor FIRST** — same operations, same order,
   formalized into the `InitPhase` enum + `g_phase` + `KCDX_REQUIRE_PHASE`
   asserts + the one apply-driver; version detection promoted to phase 2. NO
   behavior change. Verified by a `/verification-checkpoint` launch confirming the
   boot is equivalent (suite still passes, no regression).
2. **THEN PROBE U.6** — re-slotted into the new model, resolves phase 7's context.
3. **THEN the absorb `/feature`** — builds on the verified phase model; updates
   `loader-architecture.md` (the "no `mods/` folder" line is superseded — kcdx
   owns the loader) + adds the `docs/design.md` loader-absorb section.

One axis at a time (`results-driven.md`): restructure (no behavior change) and
verify equivalent, THEN change behavior. Never both in one step on load-bearing
working boot code.

## Anchors
- `docs/known-issues/init-sequencing-audit.md` — the as-is + gaps (spec input).
- `docs/load-order.md` — the resolved zone/priority list the apply-driver walks.
- `.claude/rules/loader-architecture.md` — the A/B context split + loader-safety
  contract phase context-pinning encodes (its "no `mods/` folder" line is
  superseded by the absorb).
- `_research/phase8.5-pak-resolver/FINDINGS.md` — the absorb design + U.6 the
  phase-7 placement waits on.
