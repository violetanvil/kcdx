# Phase 5 — bring-forward early capability (the before-game author surface)

**Status:** v1 (settled 2026-06-05)
**Owns:** the GENERAL early-capability author surface for the kcdx restructure —
the before-game execution window for both Lua and C++ plugins, and the rule for
what is callable there. The Phase-11 VM design ([`../lua-vm-design.md`](../lua-vm-design.md))
built the one VM + the worker-run early slot + the event gate as MECHANISM, scoped
narrowly to the boot-asset case; this design generalizes that slot into the full
early author surface (the `declare-vs-act` rule below) and adds the early C++ entry
+ the early notification.
**Consumes:** [`../lua-vm-design.md`](../lua-vm-design.md) §5 (the worker-run slot +
the event gate — the mechanism this surface rides), [`before-game-hooks.md`](../../../before-game-hooks.md)
§3/§5/§6 (the before_game-hook model + the self-registration install machinery +
the BugSplat consumer), the Phase-4 FOUNDATION (the cross-thread event gate + the
`RegisterRuntimeOverlay` two-writer CAS — see [`../RESUME-STATE.md`](../RESUME-STATE.md)).
**Builds on:** the keystone (the worker builds + publishes the one VM; the early
slot reads `g_L`, never calls `SetLuaState` — PROBE FIXC, prior session).

---

## §1 Vision

**kcdx exposes a general EARLY author surface — for both Lua and C++ — at the
worker's before-game window (post-VM-build, pre-boot-open), so a plugin can DECLARE
everything the engine consumes during init BEFORE it is consumed.** A
total-conversion author replaces boot/menu assets, hooks init-time game functions,
and registers for the before-game point without owning a native-DllMain detour or
hitting a "too late" wall.

**The bar (settled this session):** a surface is brought forward iff leaving it
late costs **capability, user UX, or performance**. "It would break if moved" is an
engineering task to solve (the gate, the CAS, decouple), NEVER a reason to keep
something late. The complement also holds: a surface with no before-game consumer
is NOT moved (moving it buys nothing on the three axes and widens the worker-side
hazard surface).

**v1 success criteria (measurable):**

- A `before_game`-zoned Lua plugin's `lua_before` entrypoint runs on the worker,
  before the engine's boot-asset open, on the published VM.
- A C++ plugin's before-game entry is invoked by the worker in the same window
  (the kcdx-driven entry), AND a true-load-time native detour still installs via
  self-registration from the plugin's own DllMain (the expert hatch).
- A plugin that owns no early entrypoint can REGISTER for the before-game point
  (a new `kcdxMessage_BeforeGame` listener — Lua and C++) and kcdx FIRES it at the
  gated point, on the right thread.
- An early DECLARATIVE call (`kcdx.assets.register`, before_game `kcdx.hook`
  intent, a name declaration, a listener registration) succeeds early; an early
  IMPERATIVE call (fire a callback / call live game state) fails LOUD with a
  teaching error, never a silent no-op.
- The full test suite stays green; PROBE Q stays silent (the early surface adds no
  kcdx-image sentinel — it rides the one VM).

**Top-level architecture decision (settled):** the early surface is scoped by the
`declare-vs-act` rule (§4), not a fixed verb allowlist — so a TC need we cannot
enumerate today is early iff it is declarative. Rejected: a fixed early allowlist
(assets + before_game hook + listener-register, expand on request) — it
under-scopes and forces a later rebuild of the early surface the moment a TC author
needs a declarative verb we did not list, which is the exact rebuild this phase
exists to prevent.

---

## §2 Glossary

| Term | Meaning |
|---|---|
| **The before-game window** | the worker-thread interval AFTER the VM is built + published (`lua_vm_build`) and BEFORE the engine's game-main boot-asset open. The only point early enough to precede the boot open (PROBE P4: worker VM-publish precedes the boot open by ~1.5s, cross-thread). |
| **The early Lua slot** | a new worker-run `lua_before` `[entrypoints]` key — a plugin's early Lua, run on the published VM in the before-game window (VM design §5, candidate B). Mirrors the existing `lua_after`. |
| **The early C++ entry** | a NEW kcdx-driven export the worker invokes in the before-game window (the common path), symmetric with the Lua slot. Distinct from **self-registration** (the plugin's OWN DllMain installs an LDR-time native detour — the expert hatch for hooks the worker entry is too late for). |
| **`declare-vs-act`** | the rule scoping the early surface (§4): a verb is callable early iff it DECLARES intent to a store kcdx applies at the gated point (a store-write); a verb that ACTS (fires a callback / calls or reads live game state) stays late. |
| **The event gate** | the Phase-4 cross-thread happens-before edge: the early slot/entry signals readiness (release); the boot-open path waits-and-blocks (acquire). Orders every cross-thread effect of an early declaration. (VM design §5; the FOUNDATION step builds it.) |
| **`kcdxMessage_BeforeGame`** | a NEW lifecycle message fired at the before-game point on the messaging bus, for a plugin that wants notification without owning an entrypoint (Lua: `kcdx.on`/on-ready-style; C++: `RegisterListener`). Mirrors `kcdxMessage_LuaReady`. |

---

## §3 User stories & acceptance criteria

Organized by author concern, not phase.

### US-1 — A Lua plugin replaces a boot/menu asset (KI-0005)
A `before_game`-zoned plugin declares a `lua_before` entrypoint that calls
`kcdx.assets.replace("Libs/UI/Textures/KCDLogo.dds", ...)`. The replacement wins
the boot-asset open.
**Acceptance:** the boot asset opens with `rt=HIT` from the early-registered
overlay (agent reads the dev log); the user sees the replaced asset render
(perceptual confirm); the boot-open path observed the slot's readiness event
SIGNALED before resolving (the gate held — the falsifiable order-inversion row).

### US-2 — A Lua plugin hooks an init-time game function
A `before_game`-zoned plugin's `lua_before` entrypoint calls `kcdx.hook` on a
function the engine calls during init. The hook intent is queued early and applied
before the engine reaches that call.
**Acceptance:** the hook fires when the engine calls the target during init (a
self-reporting test row that FAILS if the hook never fired); the apply ordered
before the init call via the gate.

### US-3 — A C++ plugin does early work via the kcdx-driven entry (the common path)
A C++ plugin exports the before-game entry; the worker invokes it in the
before-game window with a live root pointer. The plugin does declarative early work
(`kcdx.assets.register`, before_game hook intent, a name declaration) WITHOUT
writing a DllMain detour.
**Acceptance:** the export is invoked on the worker pre-boot-open (a test row
self-reports it ran + on the worker tid); its declarations take effect at the gated
point exactly as the Lua slot's do.

### US-4 — A C++ plugin installs a true-load-time native detour (the expert hatch)
A plugin needing to hook a foreign DLL's function that fires BEFORE the worker runs
(BugSplat's ctor at LDR-notification time) self-registers from its own DllMain via
the `early_hook` primitive ([`before-game-hooks.md`](../../../before-game-hooks.md) §5).
**Acceptance:** unchanged from before-game-hooks.md §6 — the LDR notification arms,
fires when the foreign DLL maps, installs the detour (PROBE T-confirmed). This story
is RETAINED, not rebuilt; this design only adds US-3 alongside it.

### US-5 — A plugin is notified at before-game without owning an entrypoint
A plugin (Lua or C++) already loaded registers a listener for the before-game
point. kcdx fires `kcdxMessage_BeforeGame` at the gated point, on the right thread.
**Acceptance:** the listener fires once, at before-game, before the boot-asset open;
registering is a store-write (early-safe); firing is kcdx's, on the correct thread.

### US-6 — An imperative early call fails loud
A plugin's early entrypoint calls an IMPERATIVE verb (fires a callback, calls live
game state). The call fails with a teaching error naming the before-game constraint
and the late slot to use.
**Acceptance:** a structured error (AP14 — never a silent no-op); the message teaches
(use the late slot / this is before-game); a test row asserts the reject is loud +
reads the actual reject path, not a tautology (AP15).

---

## §4 The early-surface scoping rule — `declare-vs-act` (the load-bearing principle)

**The early surface exposes the whole DECLARATIVE subset of `kcdx.*`; the IMPERATIVE
subset stays late.** This is the rule, not a fixed list — a verb (existing or future)
is early iff it is declarative.

### Declarative (callable early) — writes intent to a store kcdx applies at the gated point
A declarative verb writes to a kcdx-owned store and returns; kcdx applies the stored
intent at the right time (the gated point / the apply pass). It embeds no
game-thread semantics, so it is GC-safe on the worker by the same reasoning that
cleared the asset write (PROBE FIXC: a store-write on the adopted state embeds no
kcdx-image sentinel — `setnodevector` makes kcdx's dummynode a heap allocation). The
declarative set:

- **`kcdx.assets.register` / `.replace`** — writes the runtime-overlay store (KI-0005).
- **`kcdx.hook` (before_game intent)** — queues a hook into the registry; the apply
  pass installs it before the engine's init call.
- **name / namespace declaration** (`kcdx.assets.declare`, `kcdx.publish`'s
  name-declaration leg) — writes the published-name store.
- **listener registration** (`kcdx.on` / C++ `RegisterListener`, incl. for
  `kcdxMessage_BeforeGame`) — stores a registry ref; the LISTENER FIRES later, on
  the right thread.
- **boot-cvar set** — *assumes the engine reads the cvar during the before-game
  window — UNVERIFIED, probe before relying on it (§6).* Where the engine reads + caches
  a cvar at init, setting it early is declarative (write a value the engine reads).
  Provisional until the probe confirms a given cvar is read in the window.

### Imperative (stays late) — fires a callback, or calls/reads live game state
An imperative verb acts on the running game: it fires a registered callback, calls
into a live engine subsystem, or reads a live game object. These are **meaningless
before the game exists** — so excluding them costs the author ZERO capability — and
they are the off-thread re-entrancy hazard (firing a listener on the worker
re-enters game-thread-assuming code, the same reason `SetLuaState` / `LuaReady` stay
game-thread). They stay at the game-thread first-tick / runtime.

### Why a rule, not a list
A TC author's need we cannot enumerate today is correctly classified by the rule:
declarative → early, imperative → late. The rule matches the kcdx mental model
(*declare intent; the engine applies it at the right time*) and keeps the worker
surface GC-safe by construction (store-writes only).

### Per-verb classification is settled at build, by the GC-safety probe
The declarative/imperative LABEL on each concrete verb is confirmed at build by the
worker GC-safety probe (§6) — the design fixes the RULE; the probe confirms each
verb's worker-bind is GC-safe + PROBE-Q-silent before that verb is bound early. A
verb that cannot be made GC-safe early is treated as imperative for v1 and surfaced.

---

## §5 The author surfaces

### 5.1 Lua — the `lua_before` entrypoint
A new `[entrypoints]` key, `lua_before`, mirroring the existing `lua_after`
(`src/config.cpp` allowlist + parser; string-or-array). A `before_game`-zoned plugin
declares it; the worker runs it on the published VM in the before-game window. The
existing `lua` (first-tick) + `lua_after` keys are untouched.

```toml
[load_order]
zone = "before_game"

[entrypoints]
lua_before = "early.lua"   # runs on the worker, pre-boot-open
lua        = "plugin.lua"  # runs game-thread first-tick (unchanged)
```

### 5.2 C++ — the kcdx-driven before-game entry
A NEW exported function the worker invokes in the before-game window, mirroring the
existing `kcdxPlugin_Preload` / `kcdxPlugin_Load` / `kcdxPlugin_PostGameLoad` set
(`include/kcdx/Interfaces.h`). It receives the read-only root pointer like the other
entry points. The plugin does declarative early work from it.

> **The export NAME is a build-time determination, not settled here** — a new export
> (lean: `kcdxPlugin_BeforeGame`) vs. reusing the existing `kcdxPlugin_Preload`
> (currently "early-phase setup, optional"). *This turns on Preload's CURRENT fire
> timing relative to the VM build + boot open — a checkable fact, UNVERIFIED here
> (§6). Lean: a NEW export (no risk to existing Preload users' timing assumptions);
> reuse only if the probe shows Preload already fires in the before-game window.*

### 5.3 Self-registration — the retained expert hatch (US-4)
Unchanged from [`before-game-hooks.md`](../../../before-game-hooks.md) §5/§6:
a plugin needing a hook that fires before the worker runs installs from its own
DllMain via `src/early_hook.{h,cpp}` (module + export + signature + detour). This is
the labeled expert hatch for true LDR-time native hooks the kcdx-driven entry (§5.2)
is structurally too late for. The docs distinguish the two by timing need
(worker-time → §5.2; LDR-time → §5.3).

### 5.4 The early notification — `kcdxMessage_BeforeGame`
A new lifecycle message (an append-only addition to `kcdxMessageType` — AP11-safe,
new value at the END), fired at the before-game gated point. A plugin that owns no
early entrypoint registers for it: Lua via `kcdx.on` / an on-ready-style helper, C++
via `RegisterListener`. **Registration is declarative (early-safe, a store-write);
kcdx FIRES the message at the gated point on the right thread.** Owning a `lua_before`
entrypoint or the C++ before-game export is ALSO a notification (it is invoked at the
same point) — the message serves the listen-without-entrypoint case.

---

## §6 Runtime-mechanism claims — provisional until probed (results-driven)

Per [`.claude/rules/results-driven.md`](../../../../../.claude/rules/results-driven.md),
each clause below asserts a RUNTIME MECHANISM that is NOT observed this session. Each
is a probe target, not a settled requirement; the design is provisional on each until
its probe lands (ordered before the step that builds on it — the incremental-delivery
discipline: a dependency lands before its consumer).

1. **Worker GC-safety of each early DECLARATIVE bind** — *assumes a `kcdx.assets`
   subset bind + a `RegisterRuntimeOverlay` write (and each other declarative verb's
   worker-bind) on the worker VM is GC-safe + PROBE-Q-silent — UNVERIFIED.* The
   keystone proved build+adopt on the worker; it did NOT prove subset-bind + RCU-write
   on the worker. This probe gates the per-verb declarative classification (§4) AND the
   Phase-4 foundation's CAS-on-worker assumption. (Recorded in
   [`../RESUME-STATE.md`](../RESUME-STATE.md).)
2. **C++ `kcdxPlugin_Preload` current fire timing** — *assumes Preload fires at a
   determinable point relative to the VM build + boot open — its CURRENT timing is
   UNVERIFIED here.* Settles §5.2's new-export-vs-reuse determination (read the fire
   site / probe; lean new export if not pre-boot-open).
3. **Boot-cvar read-in-window** — *assumes the engine reads + caches a given cvar
   during the before-game window — UNVERIFIED.* Settles whether boot-cvar-set is
   genuinely early-declarative (§4) for a specific cvar. Probe per-cvar before relying.
4. **The gate timeout value + degraded behavior** — the bounded-timeout fallback
   (worker never signals → boot-open proceeds vanilla, fail-loud) — value decided at
   build under architect-review (VM design §5; lean 5000ms + vanilla-serve + WARN).

A step whose first action rests on one of these opens with the probe that proves it
(`.claude/rules/results-driven.md` §"a design clause asserting a runtime mechanism is
a probe target").

---

## §7 Structure (units this design introduces / touches)

| Unit | Responsibility | New / changed |
|---|---|---|
| `src/config.cpp` (`[entrypoints]` parser + allowlist) | recognize the `lua_before` key (string-or-array), mirror `lua_after` | CHANGED |
| `src/plugin_loader.h` (`PluginManifest`) | a `luaBeforeEntrypointsRel` field, mirroring `luaAfterEntrypointsRel` | CHANGED |
| the worker before-game runner (in `src/lua_vm_build.cpp` / `src/dllmain.cpp`, post-VM-publish) | run each before_game plugin's `lua_before` entrypoint + invoke each C++ before-game entry, on the worker; signal the gate | NEW behavior |
| `src/lua_plugin_loader.cpp` (`RunOneEntrypointFile`) | reused for the per-file run (SEH-guard, owner-attribution) — invoked from the worker runner for `lua_before` | reused |
| the early-bind surface (a worker-bound declarative `kcdx.*` subset) | bind ONLY the declarative subset on the worker before the slot runs (§4); the full table stays game-thread | NEW |
| `src/asset_namespace.cpp` (`RegisterRuntimeOverlay`) | two-writer CAS (the Phase-4 FOUNDATION step — a worker writer + the game-main writer) | CHANGED (foundation) |
| `include/kcdx/Interfaces.h` | the C++ before-game export declaration + `kcdxMessage_BeforeGame` (append-only) | CHANGED (append-only) |
| the messaging bus | fire `kcdxMessage_BeforeGame` at the gated point | CHANGED |
| `src/early_hook.{h,cpp}` | the self-registration expert hatch (US-4) | reused, unchanged |
| the event gate (`g_kcdxLuaSlotReadyEvent` + the boot-open wait) | the Phase-4 FOUNDATION; this surface signals it after its declarations | reused (foundation) |

The new responsibility unit is **the worker before-game runner** — its single job:
in the before-game window, drive the declarative early entries (Lua `lua_before` + the
C++ before-game export), then signal the gate. It is a coordinator (it sequences the
existing per-file runner + the bind surface), not new business logic.

---

## §8 UX (author-facing — first-class, cornerstone #1)

**Declare intent; the engine handles timing.** The author declares a `lua_before`
entrypoint, exports the C++ before-game entry, or registers for
`kcdxMessage_BeforeGame` — and kcdx runs it at the right point. The author never
wires the timing, never touches a thread, never writes a DllMain detour for the
common case (that is the §5.3 expert hatch, explicitly labeled).

- **What the author sees / does:** one new TOML key (`lua_before`) or one new C++
  export, parallel to the keys/exports they already know (`lua_after`, `kcdxPlugin_Load`).
  The declarative surface inside it is the SAME `kcdx.assets` / `kcdx.hook` /
  `kcdx.on` they use elsewhere — no new vocabulary for the verbs, only a new TIMING.
- **The teaching error (US-6) — the non-happy path:** an imperative call attempted
  early fails LOUD with a message that names the before-game constraint and points at
  the late slot ("`kcdx.<verb>` acts on the running game and is not available in the
  before_game window; call it from `plugin.lua` / a lifecycle listener"). Never a
  silent no-op (AP14). This is the disassembler-test posture rendered in errors: the
  engine teaches the author the timing model rather than failing opaquely.
- **Consistency:** the Lua + C++ surfaces MIRROR (`lua_before` ↔ the C++ before-game
  export; both fire `kcdxMessage_BeforeGame`); the early surface reuses the existing
  verb spellings; docs land in the same per-call files (`docs/lua/`, `docs/cpp/`) with
  the NYI/parity discipline (`.claude/rules/docs-discipline.md`).
- **Discoverability:** the new key + export + message appear in `docs/lua/index.md` /
  `docs/cpp/index.md` maps; the declarative-vs-imperative rule is documented as the
  author's mental model for "what can I do early."

---

## §9 Phases / roadmap (build order — for `/plan` to decompose)

Dependency-ordered; each step independently verifiable when it lands (the
incremental-delivery discipline — a dependency lands before its consumer). This phase
runs AFTER Phase 4 (it needs the foundation gate + CAS) and BEFORE the drop-static-Lua
phase.

1. **Worker GC-safety probe (§6 claim 1)** — observe a declarative worker-bind +
   RCU-write on the worker VM, PROBE Q silent. Settles the per-verb classification +
   the CAS-on-worker assumption. (Probe; archive to `_research/probe-archive/`.) Opens
   the phase because §4's classification + the binds rest on it.
2. **The `lua_before` entrypoint + the worker runner** — the manifest key + parser
   (`config.cpp`), the worker before-game runner (run `lua_before` on the published
   VM, signal the gate), the declarative early-bind subset bound on the worker. Test:
   a `before_game` plugin's `lua_before` runs on the worker tid, pre-boot-open; an
   imperative call fails loud (US-6).
3. **Boot-asset serve via the early slot (KI-0005, US-1)** — an early-slot
   `kcdx.assets.replace` wins a boot asset (`rt=HIT` via the gate); the AP14 warn
   narrowed/removed per the VM-design §7.1 build-time decision. (This is the
   capability the deferred P4-step-2 delivers, now on the settled slot shape.)
4. **The C++ before-game entry (US-3)** — the new export (name per §6 claim 2's
   probe), invoked by the worker runner; a C++ plugin does declarative early work.
   Test: the export runs on the worker pre-boot-open; its declarations take effect.
5. **`kcdxMessage_BeforeGame` (US-5)** — the append-only message + the gated fire
   site + the Lua/C++ registration path. Test: a listener-only plugin fires once at
   before-game, on the right thread.
6. **before_game `kcdx.hook` intent + apply (US-2)** — the early hook-intent queue +
   the apply pass ordered before the engine's init call. (May fold into step 2/4 if
   the hook intent rides the same declarative bind; `/plan` decides the grain.)

Each step ships its permanent `test-plugins/` regression row
(`.claude/rules/test-suite.md`); build-green is necessary, not sufficient — the
matrix is confirmed by the user's launch + the agent's `kcdx-dev.log` read.

The self-registration expert hatch (US-4) is NOT a step here — it is retained from
before-game-hooks.md §5/§6 unchanged; its first consumer (the BugSplat builtin) rides
this phase as a consumer, not a step (VM design §10).

---

## §10 Out of scope / deferrals

- **The Phase-4 foundation itself** (the event gate + the `RegisterRuntimeOverlay`
  CAS) — its own foundation step (Phase 4), reused here, not rebuilt.
- **The per-verb GC-safety probe is a build step (§9 step 1), not design** — the
  design fixes the `declare-vs-act` rule; the probe confirms each verb.
- **The AP14 warn narrowing** — carried from VM design §7.1, decided at build.
- **Full plugin migration to the early surface** beyond the test vehicles — authors
  adopt the before-game window as they choose; v1 ships the capability + regression
  vehicles.
- **The C++ export-name + boot-cvar-read + Preload-timing** — build-time
  determinations gated by §6 probes, not settled here.
