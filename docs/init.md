# kcdx initialization & startup ordering

The authoritative contract for how kcdx boots: what initializes, in what order,
in which execution context, and how load-order application is driven. The
**mandate**: startup is a DECLARED phase order — every subsystem is
spun up before the phase that needs it, application happens in one
load-order-driven flow, and adding a subsystem at the wrong phase FAILS LOUD.
No emergent "statement-order + comments" sequencing.

This doc has two parts: the **target phase model** (the contract the code
implements) and the **as-is sequence** (today's boot, until the refactor lands).
This doc is the forward design + the live reference.

> STATUS: the phase model below is DESIGNED + APPROVED.
> IMPLEMENTED so far: the `InitPhase` enum + `g_phase` + `KCDX_REQUIRE_PHASE`
> asserts (the pure refactor) and the early version-detection
> promotion (this doc's §Migration plan). REMAINING: the one
> apply-driver unification, and the absorb. The §"As-is" section
> describes the older statement-order boot and is being superseded as the
> migration lands. Each landed step is confirmed by a boot (suite passes, no
> regression).

## The three execution contexts (hard physical constraints, not choices)

kcdx startup spans three contexts, in this real-time order. A phase is pinned to
exactly ONE context, and the context's safety class is a PROPERTY of the phase
(encoded, not commented):

| Ctx | Thread / when | Capability + safety class |
|---|---|---|
| **A** | `DllMain(DLL_PROCESS_ATTACH)`, under the LOADER LOCK, before WHGame.dll is mapped (launcher injected us into a CREATE_SUSPENDED game process) | LOADER-SAFE ONLY: VirtualProtect/memcpy, `LdrRegisterDllNotification`, std:: containers, tomlplusplus. NO MinHook init, NO CreateThread-dependent work, NO LoadLibrary. (The loader-safety contract: the loader is an own-launcher with an A/B early/late context split.) |
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
2  BeforeGameApply                    A    [TARGET] before_game load-order slice
                                           applied (the ONE apply-driver,
                                           before_game zone) + LDR notifications
                                           armed. ⚠️ NOT YET WIRED — no
                                           ApplyZone(BeforeGame) call site exists;
                                           today only LDR notifications + the
                                           hardcoded bugsplat_ctor_probe dev-probe
                                           run here. before_game APPLY is deferred
                                           (see §As-is).
─── (DllMain returns; WorkerThread already spawned) ───
3  WorkerInit                         B    log::Init, exception filter, watchdog
4  GameDllMapped                      B    WaitForGameDll returned; WHGame.dll mapped
5  VersionDetected                    B    g_runtimeGameVersion known (reads
                                           kcd_launcher.log, falling back to WHGame's
                                           VS_VERSIONINFO; needs only WHGame MAPPED) —
                                           everything version-gated
                                           (address_library::Resolve) depends on
                                           >= this phase. Advances RIGHT AFTER
                                           GameDllMapped — the earliest possible
                                           point. pre-map (ctx A) detection is
                                           impossible — GetModuleHandleW(WHGame.dll)
                                           is null under the loader lock; earliest
                                           possible is right after WHGame maps.
6  EngineHooksInstalled               B    hooks::Install (lua_pcall + update);
                                           MinHook live
7  CtorBracketInstalled               B    [absorb] the C_ModManager ctor bracket
                                           INSTALLED — kcdx FULLY replaces the
                                           native ctor (and the SELECT call
                                           inside it), synthesizing the
                                           C_ModManager itself. Install is
                                           EARLY (right after EngineHooksInstalled)
                                           so it wins the race against
                                           CSystem::Init's call to the native
                                           ctor on the game's main thread; the
                                           bracket FIRES later, inside
                                           CSystem::Init, after the worker
                                           thread reaches PluginsLoaded
8  PluginsLoaded                      B    RegisterHandlers (Kind::Hook +
                                           Kind::Bytes) then DiscoverAndLoad;
                                           Plugin_Preload/Load fired. Plugins load
                                           AFTER the bracket install (race-
                                           critical) but BEFORE the bracket FIRES,
                                           so the kcdx enabled list reflects
                                           every loaded plugin
9  EngineSubsystemsInit               B    save_load_hooks, serialization (after
                                           save_load) — advances LAST of the ctx-B
                                           group
─── (game begins executing; CSystem::Init runs; first update tick) ───
10 AfterGameApply                     C    after_game load-order slice applied
                                           (the ONE apply-driver, after_game zone) +
                                           kcdx Lua table registered
```

Rules the enum enforces:
- **Monotonic.** `g_phase` only advances. An op asserting `>= X` run before X
  trips loud.
- **Context-pinned.** Each phase names its context; a context-A phase is
  statically known to be loader-lock-limited (a MinHook call in a ctx-A phase is
  a design error the phase identity surfaces).
- **Spin-up-before-use.** A subsystem initializes at the EARLIEST phase before
  any phase that uses it. Version detection (phase 5) precedes every
  version-gated read; the Kind::Hook/Kind::Bytes handlers register before
  DiscoverAndLoad — both within phase 8 PluginsLoaded — so a C++ plugin's
  Load-time hook is accepted (`lua_registry::Append` needs the handler
  registered); save/load messages (phase 9) precede serialization (phase 9,
  ordered within).

## The ONE apply-in-load-order flow

Load order is RESOLVED ONCE (phase 1, `load_order::Resolve` → one ordered list).
A SINGLE apply-driver is the only code that applies entries; it is invoked at
each zone boundary with that zone's SLICE of the one list:

- **before_game slice** → TARGET: applied at phase 2 (ctx A), in load-order
  order. ⚠️ **NOT YET WIRED** — see the caveat below.
- **after_game slice** → applied at phase 10 (ctx C), in load-order order. This
  is the SOLE live invocation today (`lua_registry::ApplyZone(AfterGame)`).

"One flow" = one resolved list + one apply function. The zones are not separate
apply logic — they are just the two INVOCATION POINTS of the one driver, dictated
by the physical WHGame-DllMain boundary (a before_game patch MUST be applied
before the game's code runs; an after_game hook needs the live engine). Every
application KIND (patch/bytes, hook, mid-hook, trampoline, and the asset-overlay
mounts the absorb adds) routes through this one driver. This is the realization
of "apply in the order the load order says" (`load-order.md`) without pretending
the zone split doesn't exist.

> ⚠️ **STUBBED — the before_game slice invocation is NOT BUILT today.** The
> "one apply-driver per zone slice" model is the TARGET. As built, the one live
> driver (`lua_registry::ApplyZone`) is invoked ONLY for the after_game slice
> (`ApplyZone(AfterGame)` at the first update tick). There is **no
> `ApplyZone(BeforeGame)` call site** — the before_game-slice invocation at
> phase 2 (ctx A) is unbuilt. The only before_game machinery running today is
> (1) `ldr_notify`, which iterates the legacy `patch::g_patches` vector that has
> had no populator since the legacy byte-patch parser was removed (so it applies
> NOTHING), and (2) the
> HARDCODED `bugsplat_ctor_probe::ArmLdrInstall` dev-probe wired directly into
> `dllmain.cpp`'s `RunBeforeGameZoneInDllMain` — not a load-order entry, not
> driven by the apply-driver. **before_game application applies nothing through
> the registry today; it is deferred.**
> The `kcdx-engine/builtin/bugsplat-filename-fix` builtin (zone=before_game) is a
> MANIFEST-ONLY STUB: it declares the zone but ships no behavior and is
> ship-disabled (`enabled = false`) until a later rewrite lands it in place.

## The mod-loader absorb (asset overlays) in the model

The kcdx "absorb the KCD2 mod loader" feature (the absorb design, see
[`mod-loader-absorb.md`](mod-loader-absorb.md)) slots in as:
- **phase 7 `CtorBracketInstalled`** — the ctor bracket on the
  `wh::C_ModManager` constructor is kcdx INFRASTRUCTURE (like
  `hooks::Install`), NOT a load-order entry. INSTALL is EARLY (right after
  `EngineHooksInstalled`, before `RegisterHandlers` + `DiscoverAndLoad`) so it
  wins the race against `CSystem::Init` calling the native ctor on the
  game's main thread. The bracket FIRES later, inside `CSystem::Init`, AFTER the
  worker thread reaches phase 8 `PluginsLoaded` — so the kcdx enabled list
  reflects every loaded plugin even though the bracket was installed earlier.
  Inside the bracket kcdx fully synthesizes the C_ModManager (vtable, sys,
  modsDir CryString, the enabled-list vector triple, the init flag) and the
  native ctor + the native SELECT are NEVER called.
- **Asset-overlay entries** (a plugin's / mod's assets) ARE load-order entries,
  applied THROUGH the takeover: kcdx, now owning ctor construction, builds the
  engine's enabled-mod list FROM the resolved load order — so a synthesized
  I_Mod record for every enabled mod (vanilla pak mod + kcdx plugin alike)
  mounts via the native MOUNT, in load-order order.
- **Placement is confirmed in context B (worker thread).** A worker-thread
  hook installed at this phase fires before `CSystem::Init` reaches the ctor —
  confirmed against the running binary — so phase 7 sits in context B, not
  context A (the loader-lock / before_game machinery).

## As-is (today, until the refactor lands)

The current boot does the SAME operations in the SAME order, but expressed as
statement order in two functions + comment paragraphs — NOT a phase enum, no
asserts. Summary of where the code is now:
- `dllmain.cpp` `RunBeforeGameZoneInDllMain()` = ctx A (phases 0–3, un-enumerated).
  ⚠️ It arms LDR notifications and runs the hardcoded `bugsplat_ctor_probe::
  ArmLdrInstall` dev-probe — but it does NOT apply any before_game registry
  slice (there is no `ApplyZone(BeforeGame)` call). The before_game apply path
  is STUBBED: `ldr_notify` walks the empty `patch::g_patches` (no populator
  since the legacy byte-patch parser was removed) and applies nothing;
  before_game registry-apply is deferred.
- `dllmain.cpp` `WorkerThread()` = ctx B (phases 4–9, un-enumerated; the FOpen
  probe sits after DiscoverAndLoad because it needs the game version — a
  dependency currently encoded only as placement + a comment).
- `hooks.cpp` `HookedUpdate` first-tick block = ctx C (phase 10).

KNOWN GAPS the refactor closes: emergent ordering (no enum/assert);
probes inserted by convenience (the FOpen-probe-broke-on-version=0 bug);
apply scattered across 3+ sites; the version lifecycle hole; and the absorb's
hard one-shot constraint the current model can't express.

## Migration plan (locked, all-Option-A)

1. **Pure behavior-preserving refactor FIRST (DONE)** — same operations, same
   order, formalized into the `InitPhase` enum + `g_phase` +
   `KCDX_REQUIRE_PHASE` asserts. NO behavior change. Version detection was
   deliberately LEFT at its existing late site here (the advance bracketed the
   existing call inside DiscoverAndLoad) so the step changed no behavior; the
   early-promotion was carved out as the separate step 2 below.
2. **Version detection promoted EARLY (DONE — this step).** The SINGLE version
   detection (`DetectRuntimeGameVersion`, one call/one write) moved from its
   late site inside `DiscoverAndLoad` to right after `WaitForGameDll` returns
   (ctx B, `GameDllMapped` → `VersionDetected`), before `hooks::Install` and the
   full plugin load. This closes the version-lifecycle hole (audit gap #4):
   `g_runtimeGameVersion` is now known before every version-gated read, and the
   `KCDX_REQUIRE_PHASE(VersionDetected)` guard in `address_library::Resolve`
   genuinely enforces it. Ctx-A detection is impossible (WHGame unmapped under
   the loader lock — `GetModuleHandleW` null), so right-after-`GameDllMapped` is
   the earliest achievable. One axis: timing only — `DetectRuntimeGameVersion`'s
   fallback-source logic and the per-plugin compat gate are unchanged.
3. **The ONE apply-driver unification (PENDING).** before_game / after_game
   apply still fire from separate sites; collapsing them into one resolved-list-
   driven apply-driver (per §"The ONE apply-in-load-order flow") is the remaining
   gap-fix and a later step.
4. **THEN the phase-7 context resolution** — re-slotted into the new model,
   resolving phase 7's context (live-confirmed against the running binary).
5. **THEN the absorb feature** — builds on the verified phase model; the loader
   is an own-launcher with an A/B early/late context split, and its earlier "no
   `mods/` folder" stance is superseded (kcdx owns the loader).

One axis at a time — probe a checkable unknown against the binary before changing
code: restructure (no behavior change) and verify equivalent, THEN change
behavior. Never both in one step on load-bearing working boot code.

## Anchors
- [`docs/load-order.md`](load-order.md) — the resolved zone/priority list the apply-driver walks.
- The loader-safety contract: the loader is an own-launcher with an A/B early/late
  context split, which the phase context-pinning encodes (its earlier "no
  `mods/` folder" stance is superseded by the absorb).
- The approved absorb design — see [`mod-loader-absorb.md`](mod-loader-absorb.md) —
  whose phase-7 placement is live-confirmed against the running binary.
