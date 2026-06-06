# RESUME STATE — Phase 11, mid-flight (2026-06-05)

Where work stands the moment the user paused to `/design` the new Phase 5.
Delete this file once the Phase-5 design + plan land and P4-step-1-foundation
is building (it is a transient handoff note, not a tracked lifecycle artifact).

## The exact spot we paused at

The user invoked `/design` ("design it, then we will plan") to design the NEW
Phase 5 (bring-forward early capability). Immediately before, two decisions had
been made (via AskUserQuestion, both answered) but **NOT YET APPLIED to the
tree**. This file records them so the next session applies them, then runs the
design.

**Sequence to resume:**
1. Apply the two pending tree edits below (P4-step-1 re-scope + the new-phase
   insert/renumber) — the ledger must reflect reality before anything builds.
2. Run `/design` for the new Phase 5 (the user's current invocation) — see
   "The new Phase 5 — design scope" below for what it must settle.
3. Then `/plan` decomposes the settled Phase-5 design into steps.
4. Then build P4-step-1-FOUNDATION (gate + CAS only) — it does NOT depend on
   the Phase-5 design (pure infra), so it can land before OR after the Phase-5
   plan; sequence at the user's preference.

## Decision 1 (ANSWERED) — P4 step 1 re-scoped to FOUNDATION ONLY

P4 step 1 builds, THIS cycle, ONLY the race-safety infrastructure the new phase
reuses verbatim regardless of how the early surface generalizes:

- **The cross-thread event gate** — worker signals (release) → boot-open path
  (`asset_overlay.cpp` HOOK 1/HOOK 2, game-main) waits-and-blocks (acquire).
  This makes the worker→game-main CROSS-THREAD DEPENDENCY deterministic. (NOT a
  "race" — sloppy earlier phrasing the user caught; it is a producer→consumer
  cross-thread dependency that the gate makes a happens-before edge so it never
  BECOMES a race. The ~1.5s margin is the race-if-ungated, never the guarantee.)
- **The `RegisterRuntimeOverlay` two-writer CAS** — `src/asset_namespace.{h,cpp}`
  RCU store today is single-writer-main-thread (plain release-swap); the header
  (`asset_namespace.h` §"WRITER SERIALIZATION", ~lines 48-62) NAMES the fix: a
  CAS retry loop when a build-time + a runtime write could overlap. A worker
  writer breaks the single-writer premise → CAS upgrade.
- **The cap-82 order-inversion regression** — FAILS if the boot open resolved
  the store BEFORE the slot signaled (and it wasn't the bounded-timeout degraded
  path). The falsifiable proof the gate holds. (cap-NN: next free is cap-82.)

**DEFERRED out of P4 step 1** (to the new Phase 5 + its `/design`):
- the worker `before_game` Lua slot-RUNNER shape (Lua-only vs Lua+C++),
- the early-bind surface SET (which `kcdx.*` subset binds early),
- because the new phase re-designs exactly these (generalizes to Lua+C++ early
  capability — decision #2 below). Building the slot-runner shape now = throwaway
  (the user's explicit exception clause: "only not if what we build now needs to
  be rebuilt").

**P4 step 2** (boot-asset serve, KI-0005) rides the Phase-5-settled slot shape —
it consumes the runner, so it waits for the design too.

**STILL OWED before the foundation build:** a worker-GC-safety PROBE — confirm a
`kcdx.assets`-subset bind + a `RegisterRuntimeOverlay` write on the WORKER VM is
GC-safe + PROBE Q silent. The keystone proved build+adopt on the worker; it did
NOT prove subset-bind + RCU-write on the worker. Per the user's standing "don't
build what isn't proven" + `results-driven.md`. (This probe gates the
foundation's CAS-on-worker assumption AND the Phase-5 slot. Run it as the
foundation build's probe step, or as a Phase-5 early step — sequencing TBD.)

## Decision 2 (ANSWERED) — insert the new phase as Phase 5, RENUMBER downstream

The new bring-forward phase lands between current P4 and current P5, by integer
renumber (user chose strict integers over a sort-stable `phase-04b`):

- `git mv phase-05-drop-static-lua → phase-06-drop-static-lua`
- `git mv phase-06-serve-execute → phase-07-serve-execute`
- NEW: `phase-05-bring-forward-early-capability/`

**Cross-references to rewrite in the SAME change as the renumber** (per
`doc-organization.md` — dir + index stay synced; per `concurrency-git.md` —
`git mv` of dirs is a parallel-chat hazard, do it atomically, stage by exact
path):
- top `README.md` phase ledger (the 6→7-row table + the build-order rationale
  prose naming P5/P6),
- `plan-spec.md` coverage map rows that cite "P5 step 1" / "P6 step 1" (drop
  static Lua → now P6; serve-execute → now P7) + any §-number build-order text,
- `lua-vm-design.md` §9 (build order) — renumber the phase list,
- the two moved dirs' own README/step back-links (`../../` depth unchanged; only
  the phase NUMBER in prose),
- the moved step docs' Dependencies/Reference prose that names its own old phase
  number.

## The new Phase 5 — design scope (what `/design` must settle)

**Goal:** bring forward to the worker (early, before the engine's boot-asset
open / before-game window) the surfaces that cost CAPABILITY / USER-UX /
PERFORMANCE if left at the game-thread first-tick latch — NOT "everything that
would break if moved" (the user's bar: "it would break" is an engineering task
to solve — gate/CAS/decouple — never a reason to keep something late).

**The enumerated move-forward set (user APPROVED all three + the early-C++/Lua
capability):**
1. **`kcdx.assets` register/replace subset on the worker** — capability + user
   UX. KI-0005: a boot/menu asset is opened once in `CSystem::Init` + cached; a
   late register can NEVER win it. Late = the capability doesn't exist.
2. **`before_game` Lua entrypoint execution (the early slot itself)** —
   capability. The before_game Lua zone has no runtime today (`RunAll` is ~10s
   too late).
3. **`kcdx.hook` install of a `before_game`-zoned target (the hook intent
   drained by `ApplyZone`)** — capability. A before_game hook on an init-time
   function (bugsplat-filename fix is first consumer) must apply before that
   call.
4. **EARLY C++ AND Lua capability (decision #2 — user: "yes, we need early C++
   and Lua capability")** — a NEW early notification/entry surface for BOTH
   languages at the before_game point (NOT moving `kcdxMessage_LuaReady`, whose
   contract is "full kcdx.* is up" — this is a NEW earlier event/entry). This is
   why the slot-runner generalizes Lua-only → Lua+C++, which is why P4-step-1's
   slot-runner shape was deferred here.

**Stays LATE (NOT moved — no capability/UX/perf cost to staying, per the
enumeration):** `kcdxMessage_LuaReady` firing (its precondition is the full
table); `SetLuaState`/game-thread-id capture (it DEFINES the game thread = game
thread by construction; the early slot reads `g_L` published by the keystone, it
does NOT call SetLuaState — DECOUPLE, don't move); the full `RegisterKcdxTable`
(only the assets subset has an early consumer); `ApplyZone(AfterGame)` /
trampoline `ApplyAll` / scan `RunAll` (after-game by design; scan WANTS pristine
pre-patch bytes); `NotifyVmReady` freeze (reframe as the event gate, not a move);
`LogInventory` + cap-45/46 (observability, wants post-apply).

**The methodology the design must encode (settled via consult + user):**
narrow-slice-forward + probe-before-build. Bring forward only the slice with a
real cornerstone cost; each new worker-bound surface with a cross-thread
dependency on a game-thread consumer needs the event gate (Decision-1 infra) to
make the dependency deterministic; any worker-side bind/write is GC-safety-probed
before built.

## Load-bearing grounded facts (cite these, don't re-derive)

- First-tick game-thread latch: `src/hooks.cpp:346-446` (`HookedUpdate`,
  `done` CAS one-shot). Order: NotifyVmReady → RegisterKcdxTable → set_lua_state
  / SetLuaState → RunAll → ArmFreallocProbe → ApplyAll → scan → LogInventory.
- `SetLuaState` captures `g_gameMainThreadId` to its running thread
  (`lua-callback-threading.md:46-47`); the chain's 3 off-thread dispatch gates
  classify against it. → moving it to the worker would mis-define the game
  thread (the reason to leave it is decouple-not-move, NOT "it breaks").
- RCU writer premise: `src/asset_namespace.h:48-62` (single-writer-main-thread,
  plain release-swap; CAS named for the overlap case).
- Worker init sequence: `src/dllmain.cpp:208-299` — keystone `BuildAndAdoptVM`
  (234), CreateReadyEvent (253), InstallCtorBracket (280), RegisterHandlers
  (292-293), DiscoverAndLoad (295+). The pattern (things the engine races us for
  already moved to the worker behind explicit gates) is the precedent.
- Keystone published `g_L` (release) on the worker inside `BuildAndAdoptVM`
  (`src/lua_vm_build.cpp`); the early slot reads `g_L` (acquire) — no SetLuaState
  call needed.
- PROBE P4 archive: `_research/probe-archive/p4-early-slot-thread-topology.md`
  (A=worker VM-publish < B=game-main boot-open by ~1.5s, different threads →
  cross-thread → event gate is the proven mechanism; candidate B = a new
  worker-run early entrypoint, NOT reuse of game-main RunAll).

## P4-step-1 foundation build brief (ready to dispatch once we resume there)

Subagent scope when the foundation build runs (after the worker-GC-safety probe):
- Build the NEW manual-reset event `g_kcdxLuaSlotReadyEvent` (distinct from
  `g_kcdxReadyEvent` ctor-bracket + `g_whgameLoadedEvent`), created on the worker
  before the slot point.
- Insert the WAIT (acquire, bounded timeout) in `asset_overlay.cpp` HOOK 1
  (`AdjustFileNameResolver`, before `LookupRuntimeOverlay` ~line 190) + HOOK 2
  (`FOpenLooseOverlay` ~line 311), latched once (not per-call — hot resolver).
- Timeout fork was leaning 5000ms + serve-vanilla-on-timeout + fail-loud
  `LOG_WARN_KV("ASSET_OVERLAY","lua_slot_gate_timeout",...)` — but this is now
  entangled with the Phase-5 slot design; CONFIRM the timeout value when the
  foundation builds (it was surfaced, not yet user-decided).
- Upgrade `RegisterRuntimeOverlay` (`src/asset_namespace.cpp`) to a CAS retry
  loop (the header's named fix).
- cap-82 order-inversion regression self-reporting from C++ engine
  instrumentation (`kcdx::test::ReportResult` is thread-safe C++, callable from
  the gate sites — does NOT need `kcdx.test` bound on the worker VM).
- Acyclicity confirmed: worker waits only on `WaitForGameDll` (satisfied at
  WHGame-map, before boot open); game-main waits on the slot → no wait cycle.
- NOTE the foundation alone has no SIGNAL caller yet (the slot that calls
  SetEvent is the deferred runner). Decide at foundation-build time whether to
  (a) ship the gate dormant (event created, waited-on, but only the Phase-5 slot
  signals it — boot opens take the bounded-timeout degraded path until Phase 5
  lands, which is the SAFE vanilla-serve), or (b) hold the WAIT insertion until
  the slot exists too. (a) keeps the CAS + event infra landable now; (b) couples
  the wait to the signal. This is a real sub-decision surfaced at that point.

## Open sub-decisions still owed (surfaced, not yet user-decided)

- Gate timeout value + degraded behavior (leaning 5000ms + vanilla + warn).
- Foundation-now sequencing: gate dormant vs hold-the-wait (the (a)/(b) above) —
  only relevant if foundation builds BEFORE the Phase-5 slot.
- Everything in "The new Phase 5 — design scope" is the `/design` dialogue's job
  to settle (the early C++/Lua surface shape, the general early-bind set, the
  slot-runner's Lua+C++ shape).
