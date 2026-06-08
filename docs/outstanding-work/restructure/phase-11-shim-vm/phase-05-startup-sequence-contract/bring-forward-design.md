# Phase 5 — the startup-sequence author contract (control · visibility · docs)

**Status:** v2 (settled 2026-06-07 — broadened from the v1 "before-game early
capability" design; the before-game window is now ONE part of the full startup
contract).
**Owns:** kcdx's startup sequence as a single AUTHOR-FACING contract — one
documented timeline the engine ORDERS by (control), authors OBSERVE + REACT to
(visibility), and authors LEARN from (docs). Includes the before-game early
author surface (the v1 scope, now §7) as the early-slot section of that timeline.
**Consumes:** [`../lua-vm-design.md`](../lua-vm-design.md) §5 (the worker-run slot
+ the event gate — the mechanism the before-game window rides),
[`before-game-hooks.md`](../../../before-game-hooks.md) §3/§5/§6 (the before_game-hook
model + self-registration + the BugSplat consumer), the Phase-4 FOUNDATION (the
cross-thread event gate + the `RegisterRuntimeOverlay` two-writer CAS —
[`../RESUME-STATE.md`](../RESUME-STATE.md)). The internal phase model
[`src/init_phase.h`](../../../../../src/init_phase.h) + [`docs/init.md`](../../../../init.md)
(the engine-internal contract this promotes to author-facing).
**Builds on:** the keystone (the worker builds + publishes the one VM; the early
slot reads `g_L`, never calls `SetLuaState` — PROBE FIXC); PROBE INITORDER
([`../../../../../\_research/probe-archive/p5-subsystem-init-vs-boot-open-ordering.md`](../../../../../_research/probe-archive/p5-subsystem-init-vs-boot-open-ordering.md))
— the ordered-init rule is proven buildable (console + cvar move to the worker).

---

## §1 Vision

**kcdx's startup sequence is a single author-facing CONTRACT — one documented
timeline that the engine orders by, that authors observe and react to, and that
authors learn from — so a plugin author always knows exactly when their code runs
relative to everything kcdx and the engine do.** Full control (kcdx deliberately
orders every part), full visibility (the author can know + react to every
reachable phase), well-documented (one timeline an author learns once). This is
paramount: a total-conversion author writing across Lua and C++ must place their
code with certainty, not guess at timing.

**The unifying decision (settled this dialogue):** promote the rich INTERNAL phase
model ([`src/init_phase.h`](../../../../../src/init_phase.h) — `InitPhase`,
monotonic `g_phase`, logged advances, the `KCDX_REQUIRE_PHASE` guard) to THE
author-facing startup contract. One timeline serves three jobs: control (the engine
advances `g_phase` through it), visibility (authors subscribe + query it), docs (one
timeline reference). The existing lifecycle messages
(`PostLoad`/`PostPostLoad`/`InputLoaded`/`LuaReady`) reconcile as named points ON
this timeline — not a separate, drifting set.

**v1 success criteria (measurable):**

- An author can SUBSCRIBE to every author-reachable startup phase (a lifecycle
  event per phase via `kcdx.on` / C++ `RegisterListener`, full parity) and the
  listener fires at exactly that phase, on the correct thread; subscribing to an
  already-past phase fires immediately (the on-ready discipline, generalized).
- An author can QUERY the current phase: `kcdx.startup.phase()` returns the current
  named phase; `kcdx.startup.at_least(phase)` returns whether startup has reached a
  given phase; a C++ accessor mirrors both (parity).
- The author-reachable phases are the curated set (§4): the ctx-B worker milestones
  (WHGame-mapped, refdb/Address-Library ready, **kcdx-subsystems-ready**, the
  **before_game early-slot**) + the ctx-C game-live points (`PostLoad`/`PostPostLoad`/
  `InputLoaded`/`LuaReady`, reconciled). ctx-A (DllMain, loader-lock, pre-plugin) is
  SHOWN on the timeline doc but carries no subscribe-able event.
- A `before_game`-zoned Lua plugin's `lua_before` entrypoint + a C++ plugin's
  kcdx-driven before-game entry run on the worker, before the engine's boot-asset
  open, against a FULLY-INITIALIZED kcdx (all subsystems up — §3).
- A before_game `kcdx.hook` / `kcdx.bytes` declared in the early slot ACTUALLY
  INSTALLS before the engine's init call (the before_game apply-driver, §7.5) — not
  just runs the slot. This is the "full CONTROL" half: the early window can hook the
  engine's own init, closing `docs/init.md`'s STUBBED before_game apply path.
- The full test suite stays green; PROBE Q stays silent; every author-facing phase
  event + the query API + the timeline doc exist with Lua+C++ parity and a test.

**Top-level architecture decision (settled):** ONE author-facing timeline (the
promoted phase model), not a separate curated event set drifting from the internal
phases. Rejected: keep the phase model internal + add a curated author-event subset
— that perpetuates the two-models-can-drift gap this design closes, and makes "full
visibility" partial by construction (a reachable phase with no curated event leaves
the author stuck).

---

## §2 Glossary

| Term | Meaning |
|---|---|
| **The startup timeline** | the ordered sequence of `InitPhase` values (corrected for ordered-init, §3), promoted to the author-facing contract. The engine advances `g_phase` through it (control); authors subscribe + query it (visibility); one doc describes it (docs). |
| **Author-reachable phase** | a phase an author plugin can act at: the ctx-B worker milestones + the ctx-C game-live points. Each exposes a lifecycle event + appears on the timeline doc. (ctx-A is shown-not-subscribable — §4.) |
| **The three contexts** | A = DllMain (loader-lock, pre-WHGame — internal only); B = worker thread (WHGame mapped, full capability — where kcdx inits its subsystems + runs the before_game slot); C = game main thread (engine live). From `init_phase.h`. |
| **kcdx-subsystems-ready** | the ctx-B phase where ALL kcdx subsystems (refdb, console, cvar, asset seam, serialization, hook/bytes handlers) are initialized on the worker, BEFORE the boot-asset open. The ordered-init signal (§3); proven reachable by PROBE INITORDER. |
| **The before-game early-slot** | the ctx-B phase (after kcdx-subsystems-ready, before the boot open) where a plugin's early Lua (`lua_before`) / C++ before-game entry runs against fully-initialized kcdx. The v1 scope, now §7. |
| **`declare-vs-act` → needs-only-kcdx-vs-needs-live-game** | the rule scoping what is callable in the before-game window (§7.3): early iff it needs only kcdx (+ mapped WHGame); late iff it needs the LIVE GAME. Subsystem-readiness is structural (kcdx fully inits before the slot), not a per-verb probe-around. |
| **The event gate** | the Phase-4 cross-thread happens-before edge: the early slot signals readiness (release); the boot-open path waits-and-blocks (acquire). Orders the cross-thread worker→boot-open dependency. |
| **`kcdx.startup.*`** | the new author domain sub-table for the query API (`phase()`, `at_least()`), per the author-surface law (`kcdx.<domain>.<verb>`). |

---

## §3 The ordered-init rule (PROBE INITORDER-proven) — the timeline's worker spine

The startup timeline's ctx-B (worker) spine is the **ordered-init rule**: kcdx
initializes ALL its early-relevant subsystems in dependency order on the worker,
THEN runs the before-game plugins against a fully-initialized kcdx, THEN the game
launches (boot-asset open), THEN the after-game (game-live) surface runs.

**Proven by PROBE INITORDER (2026-06-07, archive
`_research/probe-archive/p5-subsystem-init-vs-boot-open-ordering.md`):**

- **5 of 6 early-relevant subsystems already init on the worker before the boot
  open** — refdb::Open, the hook/bytes apply handlers, save_load_hooks, serialization,
  asset_overlay (all worker tid, all preceding the first boot open by 0.6–2.9 s).
- **`console::Init` is the only one currently late** (game-main first-tick, +11.2 s
  after the boot open). It MOVES to the worker — **proven safe**: `gEnv->pConsole`
  (its only hard dependency) is non-null at the worker pre-boot-open point
  (`iconsole=0x1CE58A699C0`). `cvar::Init` rides the same precondition and moves with
  it.
- **kcdx-subsystems-ready** is therefore a real, reachable ctx-B phase: the point on
  the worker where every kcdx subsystem is up, BEFORE the boot open.

**The early/late split is needs-only-kcdx-vs-needs-the-live-game**, and
subsystem-readiness is a STRUCTURAL guarantee (kcdx fully inits on the worker before
the slot), not a per-verb probe-around. This supersedes the v1 "declarative
(store-write) vs imperative" axis, which a completeness sweep found conflated
"declarative" with "early-safe" for three verbs that call a live kcdx subsystem at
registration. Those three now dissolve:

- **`kcdx.command`** — console is up by the slot → registering a command early is
  fine (console::Init moved to the worker).
- **`kcdx.cosave` registration** — serialization is up by the slot → early.
- **`kcdx.scan`** — imperative-but-early-RUNNABLE (it scans live module memory; WHGame
  is mapped at the slot) — docs say it ACTS (reads memory now), not declares.

The boot open is CROSS-THREAD from the worker (game-main vs worker — PROBE INITORDER
confirmed, consistent with PROBE P4), so the worker→boot-open dependency stays gated
by the Phase-4 event gate, never a wall-clock margin.

---

## §4 The author-facing phase set (curated, reachable)

Not all internal phases are author-reachable. The author-facing set is curated by
REACHABILITY; ctx-A is shown on the timeline doc but carries no event.

### Author-reachable — a subscribe-able lifecycle event + a query value + a doc entry

- **ctx-B (worker) milestones:**
  - **WHGame-mapped** (`GameDllMapped`) — the game binary is mapped; an author hook
    on a WHGame export becomes installable.
  - **Address-Library ready** (`RefdbOpened`) — every name/id resolve is live; an
    author can resolve targets.
  - **kcdx-subsystems-ready** (NEW, §3) — every kcdx subsystem is up on the worker
    (console, cvar, asset seam, serialization, hook/bytes handlers), before the boot
    open. The "kcdx is fully ready, and the game has not started" point.
  - **before_game early-slot** (NEW, §7) — the author's early Lua / C++ entry runs.
- **ctx-C (game-live) points** — the existing lifecycle messages, reconciled as
  timeline points (§5): `PostLoad`, `PostPostLoad`, `InputLoaded`, `LuaReady`.

### ctx-A (DllMain, loader-lock, pre-plugin) — shown, NOT subscribe-able

The ctx-A phases (`PreInit`, `ConfigLoaded`, `BeforeGameApply`) run under the loader
lock, before WHGame is mapped and before any author plugin DLL is loaded. An author
event there is **unsubscribable by construction** (the author's DLL is not loaded)
and **actively dangerous** (a loader-lock callback can silently deadlock the game —
the footgun [`before-game-hooks.md`](../../../before-game-hooks.md) §3 decision 5
warns about). So ctx-A appears ON the timeline doc (the author sees the WHOLE
sequence) but carries NO subscribe-able event, marked "engine-internal, pre-plugin".

### Why curation, not 1:1

Exposing every internal phase 1:1 would ship unsubscribable/hazardous ctx-A events
that mislead authors into an unsafe pattern. Curation by reachability gives "every
MEANINGFUL part" (the ask's spirit) — every phase an author can reach is visible +
reactable; ctx-A is excluded by physics, not omitted (the doc still shows it).
Internal phases that are pure plumbing with no author meaning
(`EnabledListBuiltAndReady`, the internal mod-loader build) likewise stay
engine-internal (shown on the timeline as internal markers, no event).

---

## §5 The author surfaces — react + query

### 5.1 React — a lifecycle event per author-reachable phase (determined by the author-surface law)

Each author-reachable phase (§4) fires a lifecycle event through the existing message
bus. An author subscribes via the existing lifecycle verb — `kcdx.on(event, fn)`
(Lua) / `RegisterListener` (C++) — full Lua+C++ parity
([`.claude/rules/lua-api-surface.md`](../../../../../.claude/rules/lua-api-surface.md)).
This is DETERMINED by the author-surface law (the lifecycle subscription mechanism
already exists; the new phases get events on it), not a new fork.

- The phase events are named on the timeline (stable tokens reconciled to
  author-friendly names — e.g. `kcdx.on("kcdx.subsystems_ready", fn)`,
  `kcdx.on("kcdx.before_game", fn)`). The exact token-per-phase reconciliation is a
  build-time read of the firing sites (§8).
- **Subscribing to an already-past phase fires immediately** — the
  `kcdx.dev.on_ready` "already-ready vs subscribe-and-wait" discipline, generalized
  to every phase. An author never misses a phase by subscribing late.
- The existing append-only `kcdxMessageType` enum carries the new phase messages
  (AP11-safe, new values at the END).

### 5.2 Query — `kcdx.startup.*` (NEW)

A new author domain sub-table (`kcdx.<domain>.<verb>`, per the author-surface law):

```lua
kcdx.startup.phase()          -- returns the current named phase (a stable token)
kcdx.startup.at_least(phase)  -- returns whether startup has reached `phase`
```

C++ mirrors both via the interface (parity). Backed by the existing `g_phase` atomic
+ `init::Name()` — a thin READ surface over what already exists internally (the
internal `KCDX_REQUIRE_PHASE` guard already proves the query is useful; this exposes
the same to authors). Lets an author BRANCH on the current state, not only react to
a transition ("if `kcdx.startup.at_least("kcdx.subsystems_ready")` then do X now;
else `kcdx.on("kcdx.subsystems_ready", do_x)`").

### 5.3 The timeline doc — the author learns the whole sequence (well-documented)

A NEW author-facing startup-sequence reference (the timeline authors read), per
[`.claude/rules/docs-discipline.md`](../../../../../.claude/rules/docs-discipline.md):
every phase in real-time order, what kcdx does at it, what subsystems are up, what
the author can SAFELY do at it (and what they can't yet), the event token to
subscribe to, the query value it returns, which context (A/B/C) it runs in, and the
ctx-A "shown-not-subscribable" markers. `docs/init.md` STAYS the internal engine
contract; the new author doc and `docs/init.md` cross-reference (the author doc is
the WHAT-can-I-do-when view; init.md is the engine's internal ordering contract).

---

## §6 User stories & acceptance criteria

Organized by author concern.

### US-1 — An author subscribes to a startup phase
A plugin (Lua or C++) registers for an author-reachable phase
(`kcdx.on("kcdx.subsystems_ready", fn)` / C++ `RegisterListener`).
**Acceptance:** the listener fires at exactly that phase, on the correct thread (a
self-reporting test row that FAILS if it fired at the wrong phase or wrong thread).
A subscription to an already-past phase fires immediately (a row asserting the
fire-now path).

### US-2 — An author queries the current phase
A plugin calls `kcdx.startup.phase()` / `kcdx.startup.at_least(p)` (+ the C++
accessor).
**Acceptance:** returns the correct current named phase / the correct reached-yet
boolean (a row that FAILS if the query disagrees with the actual `g_phase`); Lua and
C++ return the same value (parity row).

### US-3 — A Lua plugin runs early Lua against fully-initialized kcdx
A `before_game`-zoned plugin declares a `lua_before` entrypoint; the worker runs it
at the before-game early-slot phase, with every kcdx subsystem up (§3).
**Acceptance:** the entrypoint runs on the worker tid, pre-boot-open (a self-reporting
row); a `kcdx.command` / `kcdx.assets.register` call in it succeeds (the subsystem is
up — §3).

### US-4 — A C++ plugin does early work via the kcdx-driven entry
A C++ plugin exports the before-game entry; the worker invokes it at the early-slot
phase with a live root pointer.
**Acceptance:** the export runs on the worker pre-boot-open (a row self-reports it ran
+ on the worker tid) without the plugin writing a DllMain detour.

### US-5 — A Lua plugin replaces a boot/menu asset (KI-0005)
A `before_game`-zoned plugin's `lua_before` calls
`kcdx.assets.replace("Libs/UI/Textures/KCDLogo.dds", ...)`; the replacement wins the
boot-asset open.
**Acceptance:** the boot asset opens with `rt=HIT` from the early-registered overlay
(agent reads the dev log); the user sees the replaced asset render (perceptual
confirm); the boot-open path observed the slot's readiness event SIGNALED before
resolving (the gate held — the falsifiable order-inversion row).

### US-6 — A C++ plugin installs a true-load-time native detour (the expert hatch)
A plugin needing a hook that fires BEFORE the worker runs (BugSplat's ctor at
LDR-notification time) self-registers from its own DllMain via `early_hook`
([`before-game-hooks.md`](../../../before-game-hooks.md) §5).
**Acceptance:** unchanged from before-game-hooks.md §6 (PROBE T-confirmed). RETAINED,
not rebuilt — this design adds the kcdx-driven entry (US-4) alongside it.

### US-7 — An out-of-window call is handled honestly
A plugin calls a verb that needs the live game from the before-game window (a verb
that reads/calls live game state — §7.3 late set).
**Acceptance:** a structured teaching error (AP14 — never a silent no-op) naming the
constraint + the phase to use; a row asserts the reject is loud + reads the actual
reject path, not a tautology (AP15).

### US-8 — A before_game hook/patch declared in the early slot ACTUALLY INSTALLS early
A `before_game`-zoned plugin's `lua_before` (or C++ before-game entry) calls
`kcdx.hook` / `kcdx.bytes` on a function the engine calls during its own init; the
queued registry entry is INSTALLED by the before_game apply-driver (§7.5) before the
engine reaches that init call.
**Acceptance:** the hook FIRES when the engine calls the target during init (a
self-reporting row that FAILS if the hook never fired — proving the apply-driver
drained the before_game slice, not just that the slot ran); the install ordered
before the init call. This is the load-bearing "hook any part" capability — without
§7.5's apply-driver the queue is never drained and the hook never installs.

---

## §7 The before-game window (the early-slot phase — the v1 scope, folded in)

The before-game early-slot phase (§4) is where a plugin's early Lua / C++ runs. This
is the v1 "bring-forward early capability" design, now one section of the timeline.

### 7.1 The early entries
- **Lua — the `lua_before` `[entrypoints]` key** (mirrors `lua_after` in
  `src/config.cpp`; `src/plugin_loader.h` gains `luaBeforeEntrypointsRel`). A
  `before_game`-zoned plugin declares it; the worker runs it on the published VM at
  the early-slot phase via the existing `RunOneEntrypointFile` (SEH-guard,
  owner-attribution).
- **C++ — the kcdx-driven before-game entry** — a NEW exported function the worker
  invokes (mirrors the existing `kcdxPlugin_Preload`/`Load`/`PostGameLoad` set in
  `include/kcdx/Interfaces.h`), receiving the read-only root pointer. *The export NAME
  is a build-time determination (§8): new export `kcdxPlugin_BeforeGame` (lean) vs.
  reuse `kcdxPlugin_Preload` — turns on Preload's CURRENT fire timing, UNVERIFIED.*

```toml
[load_order]
zone = "before_game"
[entrypoints]
lua_before = "early.lua"   # runs on the worker at the early-slot phase, pre-boot-open
lua        = "plugin.lua"  # runs game-thread first-tick (unchanged)
```

### 7.2 Self-registration — the retained expert hatch (US-6)
Unchanged from [`before-game-hooks.md`](../../../before-game-hooks.md) §5/§6: a plugin
needing a hook that fires before the worker runs installs from its own DllMain via
`src/early_hook.{h,cpp}`. The labeled expert hatch for true LDR-time native hooks the
kcdx-driven entry is too late for. Docs distinguish the two by timing need
(worker-time → §7.1; LDR-time → §7.2).

### 7.3 What's callable in the window — needs-only-kcdx vs needs-the-live-game (§3)
Because kcdx is fully initialized at the early-slot phase (§3), the author can do
anything that needs only kcdx + the mapped WHGame: register assets, register a
console command, queue a before_game hook, declare a name, register a listener, scan
memory. What stays late is what needs the LIVE GAME (fire a callback into game logic,
read a game object, call a live gameplay system) — meaningless before the game
exists, so zero capability cost. An out-of-window call fails loud (US-7).

### 7.4 The early-bind surface + the event gate
The worker binds the kcdx subsystems' author surfaces before the early slot runs
(§3). The slot's cross-thread effects (a `kcdx.assets.register` the boot open must
see) are ordered by the Phase-4 event gate: the slot signals readiness after its
calls; the boot-open path (`asset_overlay` HOOK 1/2, game-main) waits-and-blocks on
it before resolving an overlay. `RegisterRuntimeOverlay` gets the Phase-4 two-writer
CAS (a worker writer + the game-main writer).

### 7.5 The before_game apply-driver — queued intent ACTUALLY INSTALLS early (the missing half of "full control")

A `kcdx.hook` / `kcdx.bytes` called in the early slot (§7.1) writes intent to
`lua_registry` (a Kind::Hook / Kind::Bytes entry) — but writing intent is not
installing it. Today the ONE registry apply-driver (`lua_registry::ApplyZone`) is
invoked ONLY for the `AfterGame` slice (`hooks.cpp` first-tick); **there is no
`ApplyZone(BeforeGame)` call site** — so a before_game-zoned hook/patch declared
through the registry applies NOTHING (confirmed: `docs/init.md` §"STUBBED";
`ldr_notify` walks an unpopulated `patch::g_patches`). This is the "one apply-driver
unification" `docs/init.md` migration step 3 names as pending.

**This phase wires the `before_game` slice invocation** so the early slot delivers
CONTROL, not just execution:

- After the early slot runs (its `kcdx.hook`/`kcdx.bytes` calls have queued their
  registry entries) and BEFORE the engine reaches the init call those entries target,
  the worker invokes `ApplyZone(BeforeGame)` — the SAME one apply-driver, with the
  `before_game` slice of the resolved load-order list. Every Kind (Hook, Bytes) routes
  through the one driver, in load-order order, exactly as the `AfterGame` slice does.
- **This is what makes US-8 buildable:** "queue a before_game hook, it applies before
  the init call" requires the apply-driver to DRAIN the before_game slice — the early
  slot running (§7.1) only queues; this installs.
- Ordering: the before_game apply runs on the worker, in the same before-game window,
  and its install completing before the engine's init call is the cross-thread
  dependency the Phase-4 event gate orders (the same gate the boot-asset serve uses —
  the apply must complete before the game-main reaches the targeted init call). *Assumes
  the targeted init calls occur AFTER the worker's before_game apply point —
  UNVERIFIED for a specific target; the apply-driver wiring is general, but a given
  before_game hook's target must be one the engine calls after the apply (§8 claim 7;
  the BugSplat-class LDR-time targets that fire BEFORE the worker stay the
  self-registration hatch's job, §7.2).*
- Scope note: this wires the `before_game` INVOCATION of the existing one driver — it
  does NOT redesign the after_game path (live + correct) and does NOT build a separate
  before_game apply logic (the zones are two invocation points of ONE driver,
  `docs/init.md` §"The ONE apply-in-load-order flow").

---

## §8 Runtime-mechanism claims — provisional until probed (results-driven)

Per [`.claude/rules/results-driven.md`](../../../../../.claude/rules/results-driven.md),
each clause below is a checkable runtime mechanism NOT yet observed; the design is
provisional on it until its probe lands (ordered before the step that builds on it).

1. **The subsystem-init-vs-boot-open ordering + console-movability** — *PROVEN*
   (PROBE INITORDER, §3). No longer provisional; cited as settled.
2. **Worker GC-safety of each early subsystem bind** — *assumes binding each kcdx
   subsystem's author surface on the worker VM is GC-safe + PROBE-Q-silent —
   UNVERIFIED for the binds beyond what PROBE INITORDER + FIXC covered.* The probe
   that opens the build (§9) confirms a worker-bind + the relevant store write is
   PROBE-Q-silent before the binds land.
3. **C++ before-game export name** — *assumes `kcdxPlugin_Preload`'s current fire
   timing is determinable relative to the VM build + boot open — UNVERIFIED.* Settles
   §7.1's new-export-vs-reuse (read the fire site; lean new export).
4. **Boot-cvar read-in-window** — *assumes the engine reads + caches a given cvar
   during the before-game window — UNVERIFIED.* Settles whether an early cvar-set
   actually takes effect for a specific cvar (now that console+cvar are up early,
   §3). Probe per-cvar before relying.
5. **The phase-token reconciliation** — *assumes each existing lifecycle message
   (`PostLoad`/`PostPostLoad`/`InputLoaded`/`LuaReady`) maps to a determinable point
   on the timeline — a build-time READ of each message's firing site, not a guess.*
6. **The gate timeout value + degraded behavior** — bounded-timeout fallback (worker
   never signals → boot-open proceeds vanilla, fail-loud); value decided at build
   under architect-review (lean 5000 ms + vanilla-serve + WARN).
7. **The before_game apply-driver target-ordering (§7.5)** — *assumes the engine's
   init calls a before_game hook would target occur AFTER the worker's
   `ApplyZone(BeforeGame)` point — UNVERIFIED for a specific target.* The apply-driver
   WIRING is general (it drains the before_game slice through the one driver); but a
   given before_game hook only fires if its target is an init call the engine makes
   after the apply. A target the engine calls BEFORE the worker reaches the apply
   point (the BugSplat-class LDR-time case) stays the self-registration hatch's job
   (§7.2). Probe a representative before_game target's call timing vs. the apply point
   before relying on it for that target.

---

## §9 Structure (units this design introduces / touches)

| Unit | Responsibility | New / changed |
|---|---|---|
| `src/init_phase.h` | the phase enum gains the new ctx-B phases (kcdx-subsystems-ready, before-game early-slot) per §3/§4; the enum's append-discipline governs the additions; the model is PROMOTED to author-facing (a stable author token per reachable phase) | CHANGED |
| `src/dllmain.cpp` / `src/hooks.cpp` | `console::Init` + `cvar::Init` MOVE from the game-thread first-tick to the worker (after `asset_overlay::Install`, the proven point — §3); the worker advances `g_phase` through the new phases | CHANGED |
| the phase-event firing | each author-reachable phase fires its lifecycle message on the existing bus at its `AdvanceTo` point | NEW behavior |
| `kcdx.startup.*` domain (Lua) + the C++ accessor | the query API (`phase()`, `at_least()`) — a thin read over `g_phase` + `Name()` | NEW |
| `include/kcdx/Interfaces.h` | the new phase `kcdxMessageType` values + the C++ before-game export + the C++ startup-query accessor (all append-only) | CHANGED (append-only) |
| `src/config.cpp` + `src/plugin_loader.h` | the `lua_before` `[entrypoints]` key + `luaBeforeEntrypointsRel` (mirror `lua_after`) | CHANGED |
| the worker before-game runner (`src/lua_vm_build.cpp` / `src/dllmain.cpp`) | run each before_game plugin's `lua_before` + invoke each C++ before-game entry on the worker; signal the event gate | NEW behavior |
| the before_game apply-driver invocation (`src/lua_registry.cpp` `ApplyZone` + a `src/dllmain.cpp` call site) | invoke `ApplyZone(BeforeGame)` on the worker after the early slot queues its entries — the SAME one driver, before_game slice; drains Kind::Hook/Kind::Bytes so a before_game hook/patch ACTUALLY INSTALLS (§7.5). `docs/init.md` migration step 3 (the one apply-driver unification), before_game zone | NEW behavior (closes the STUBBED before_game apply path) |
| `src/lua_plugin_loader.cpp` (`RunOneEntrypointFile`) | reused for the per-file run | reused |
| `src/asset_namespace.cpp` (`RegisterRuntimeOverlay`) | two-writer CAS (Phase-4 FOUNDATION) | CHANGED (foundation) |
| `src/early_hook.{h,cpp}` | the self-registration expert hatch (US-6) | reused, unchanged |
| the event gate (`g_kcdxLuaSlotReadyEvent` + the boot-open wait) | Phase-4 FOUNDATION | reused (foundation) |

The new responsibility units: **the startup-contract surface** (the phase-event
firing + the `kcdx.startup.*` query — visibility over the existing phase model) and
**the worker before-game runner** (drives the early entries, signals the gate). The
phase model itself is promoted in place, not rebuilt.

---

## §10 UX (author-facing — first-class, cornerstone #1)

**Declare intent; the engine handles timing. Know where you are; react when you
want.** The author learns the WHOLE startup sequence from ONE timeline doc, then
places code by phase with certainty.

- **One mental model:** the startup timeline. The author reads it once, then knows
  every point their code can run, what's safe there, and how to hook it (subscribe or
  query). No guessing at timing.
- **The surfaces are the ones the author already knows:** `kcdx.on(event, fn)` for
  reacting (the existing lifecycle verb — the new phases are just new events on it);
  `kcdx.startup.phase()`/`.at_least()` as a new `kcdx.<domain>.<verb>` (per the
  author-surface law); `lua_before` parallel to `lua_after`. No novel shapes.
- **Errors teach:** an out-of-window call (US-7) fails LOUD with a message naming the
  constraint + the phase to use ("`kcdx.<verb>` needs the live game; it is not
  available at the before_game phase — call it from `plugin.lua` or subscribe to
  `kcdx.on("kcdx.input_loaded", ...)`"). Never a silent no-op (AP14). A late
  subscription fires immediately, never silently missed.
- **Full Lua+C++ parity:** every phase event, the query API, and the entries mirror
  across both surfaces (`lua_before` ↔ the C++ before-game export; `kcdx.startup.*` ↔
  the C++ accessor; same events on both buses), tested both surfaces
  ([`.claude/rules/lua-api-surface.md`](../../../../../.claude/rules/lua-api-surface.md)).
- **Discoverability + docs:** the timeline doc (§5.3) is the front door; the
  `kcdx.startup.*` calls + the new events + `lua_before` appear in
  `docs/lua/index.md` / `docs/cpp/index.md` with the NYI/parity discipline
  ([`.claude/rules/docs-discipline.md`](../../../../../.claude/rules/docs-discipline.md)).

---

## §11 Phases / roadmap (build order — for `/plan` to decompose)

Dependency-ordered; each step independently verifiable when it lands
(incremental-delivery — a dependency lands before its consumer). Runs AFTER Phase 4
(needs the foundation gate + CAS) and BEFORE the drop-static-Lua phase. (`/plan`
restructures the phase tree + the dir name to match this broadened scope; the v1
6-step decomposition is superseded by this order.)

1. **Worker GC-safety probe (§8 claim 2)** — observe a worker subsystem-bind +
   the relevant store write on the worker VM, PROBE Q silent. (The subsystem-ordering
   itself is already PROVEN — §3, PROBE INITORDER.) Opens the phase.
2. **Move console + cvar to the worker; add the kcdx-subsystems-ready phase** (§3) —
   `console::Init`/`cvar::Init` move to the worker after `asset_overlay::Install`;
   `init_phase.h` gains the kcdx-subsystems-ready phase; the worker advances `g_phase`
   to it. Test: a worker-registered console command dispatches; the phase advances on
   the worker pre-boot-open (a row reading `g_phase` + tid).
3. **Promote the phase model to author-facing: the phase events + the
   `kcdx.startup.*` query** (§5.1, §5.2) — fire a lifecycle message per
   author-reachable phase; add the `kcdx.startup.phase()`/`at_least()` Lua surface +
   the C++ accessor; reconcile the existing messages as timeline points (§8 claim 5).
   Test: a listener fires at its phase on the right thread; a late subscription fires
   immediately; the query agrees with `g_phase`; Lua/C++ parity.
4. **The before-game early-slot: `lua_before` + the worker runner** (§7.1) — the
   manifest key + parser, the worker before-game runner (run `lua_before`, signal the
   gate). Test: `lua_before` runs on the worker tid pre-boot-open; an out-of-window
   call fails loud (US-7).
5. **The before_game apply-driver: wire `ApplyZone(BeforeGame)`** (§7.5, US-8) — the
   worker invokes the one apply-driver's before_game slice after the early slot queues
   its entries, so a before_game `kcdx.hook`/`kcdx.bytes` ACTUALLY INSTALLS before the
   engine's init call (closes `docs/init.md`'s STUBBED before_game apply path). Test:
   a before_game hook queued in `lua_before` FIRES when the engine calls the target
   during init (FAILS if the queue was never drained — the apply-driver, not just the
   slot, is what makes it install).
6. **Boot-asset serve via the early slot (KI-0005, US-5)** — an early-slot
   `kcdx.assets.replace` wins a boot asset (`rt=HIT` via the gate); the AP14 warn
   narrowed per the build-time decision.
7. **The C++ before-game entry (US-4)** — the new export (name per §8 claim 3),
   invoked by the worker runner. Test: the export runs on the worker pre-boot-open.
8. **The author startup-sequence doc (§5.3)** — the timeline reference; cross-link
   `docs/init.md`. (Docs move with each surface step per docs-discipline; this step
   is the dedicated timeline doc beyond the per-call entries.)

Each step ships its permanent `test-plugins/` regression row + its doc entries
(`.claude/rules/test-suite.md`, `docs-discipline.md`); build-green is necessary, not
sufficient — confirmed by the user's launch + the agent's `kcdx-dev.log` read.

The self-registration expert hatch (US-6) is NOT a step here (retained from
before-game-hooks.md §5/§6; the BugSplat builtin rides as a consumer).

---

## §12 Out of scope / deferrals

- **The Phase-4 foundation** (the event gate + the CAS) — its own foundation step,
  reused here.
- **ctx-A author events** — excluded by design (§4): shown on the timeline doc,
  never subscribe-able (loader-lock, pre-plugin).
- **The internal-plumbing phases** (`EnabledListBuiltAndReady`, the mod-loader build)
  — stay engine-internal (shown as internal markers, no event).
- **The per-verb GC-safety, the C++ export name, the boot-cvar read, the
  phase-token reconciliation, the gate timeout** — build-time determinations gated by
  §8 probes, not settled here.
- **Full plugin migration to the startup surfaces** — authors adopt as they choose;
  v1 ships the contract + the query + regression vehicles + the doc.
