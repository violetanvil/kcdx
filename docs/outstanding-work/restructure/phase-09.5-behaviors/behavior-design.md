# Phase 9.5 design — `kcdx.behavior.*` named-behavior catalog

**Status:** v1 (settled 2026-06-10)
**Supersedes:** [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5" (the
two-tier model and verbs carry forward; the `behaviors` SQLite table, the
`behaviors.toml` build-time import, and eager implementation invocation are
**dropped** — see §12).
**Consumed by:** the Phase 9.5 step tree ([`README.md`](README.md)) once re-planned
against this doc.

Key changes from the original-plan section:

- Implementation calls are **deferred to a single apply boundary** (collect-then-apply,
  the ApplyZone precedent) instead of firing eagerly per `set`.
- The declare spec gains an optional **`revert`** field; post-load `set` is legal only
  when it is present.
- `set` on an undeclared name is an **immediate discriminating teaching error**
  (reorder vs not-installed), backed by persisted dependency edges and a callable
  **auto-order method** (no UI in this phase).
- The engine catalog is **plain `.lua` files loaded as a builtin pack** — no
  `behaviors.toml`, no SQLite `behaviors` table; `list()` reads the one runtime
  registry.
- C++ parity (`kcdxBehaviorInterface`) ships **in-phase**, like 9.3.
- The version story is re-grounded on the shipped three-track model — the baseline's
  `authored_against_game_version` field was superseded 2026-05-27 and never built (§9).
- 2026-06-11 — probe 9.5-S1 discharged the marked runtime assumptions (boundary
  point + self-drain, C++ stops, call-time §9 enforcement, loader-placement
  catalog pin, invoke half-reuse); two rulings encoded: the command pump stays
  unbounded with a shared high-water teaching warn (never rations authors), and
  VM adoption gains an explicit wave-end gate (gated-never-timed).

## §1 Vision

One line for the simple modder: `kcdx.behavior.set("name", value)` — never a function
name, statement, op, or address. A **behavior** is a named effect with an
engine-managed apply contract: a cvar whose setter is mod-authored. Two tiers —
engine-shipped (`kcdx.behavior.*`) and plugin-declared (`<author>.<plugin>.<bare>`) —
are one model and one code path; each TC plugin that declares behaviors grows the
named pool for downstream consumers.

v1 success criteria:

- A consumer plugin is complete in two lines (`set` × 2) with zero statement-level
  knowledge and no version declaration of any kind (§9).
- Every failure mode an author or end user can hit produces a teaching error or warn
  that names the fix (§6, §10) — no silent no-op, no surface that lies.
- The shipped catalog carries 5–10 working behaviors, each backed by a verified
  game-binary fact with its evidence tier (§7).
- Full Lua/C++ parity, exercised from both surfaces by the test plugin (§8).

## §2 Glossary

- **Behavior** — a named, settable unit of intent: a value plus the declarer's
  `implementation` that reconfigures the game to match it, under the engine's apply
  contract. NOT a shared variable (plain cross-plugin data is `kcdx.publish`/`kcdx.on`).
- **Declarer** — the plugin (or the engine catalog) that owns a behavior's name and
  implementation.
- **Consumer** — a plugin that calls `set`/`get` against a behavior it does not
  declare. The simple-modder role.
- **Declare spec** — the table passed to `declare` (§4).
- **Apply boundary** — the single point, after all plugins have loaded, where the
  engine invokes each set behavior's implementation once with the final value (§5).
- **Dependency edge** — an observed "consumer plugin → declarer plugin" relation
  recorded when a `set` resolves (or fails on ordering); persisted across launches (§6).
- **Catalog pack** — the engine-shipped behaviors: one `.lua` file per behavior under
  `data/behavior-catalog/`, loaded as a builtin ahead of user plugins (§7).

## §3 User stories & acceptance criteria

- **US-1 — consumer tweak plugin.** A player writes a two-line plugin:
  `kcdx.behavior.set("redmoon.realism.hardcore_combat", true)` +
  `kcdx.behavior.set("redmoon.realism.npc_essential_list", {"Henry", "Hans"})`.
  **Acceptance:** both behaviors take effect at the apply boundary; the plugin loads
  with no version declaration of any kind (§9); the player never wrote a function
  name or address.
- **US-2 — declarer.** A TC plugin declares `hardcore_combat` whose implementation
  calls `kcdx.statement.replace_with`. **Acceptance:** the declare registers at load
  under the stamped name; a consumer's set fires the implementation exactly once with
  the final value; `list("redmoon.")` shows it with its description.
- **US-3 — catalog promotion.** A community author's behavior proves broadly useful;
  promotion is moving the `.lua` file into `data/behavior-catalog/` (a PR).
  **Acceptance:** the file's declare code is unchanged by promotion; the behavior now
  registers as `kcdx.behavior.<bare>` and is available to every plugin regardless of
  load order.
- **US-4 — end user with a wrong order.** A user installs the tweak plugin ABOVE the
  TC mod it tweaks. **Acceptance:** the set fails at the call site with an error
  naming the exact reorder ("`redmoon.realism` loads after you — move `player-tweak`
  below it"); the edge persists, so the next launch recognizes the conflict; invoking
  the auto-order method produces and applies a corrected order. If the TC mod is not
  installed at all, the error instead names the missing plugin and suggests no reorder.
- **US-5 — runtime toggle.** A declarer ships a behavior with `revert`; a console
  command (or future settings UI) sets it mid-session. **Acceptance:** the engine
  calls `revert(old)` then `implementation(new)`; `get()` tracks. A post-load set on
  a revert-less behavior errors: "applies at load; cannot change mid-session."
- **US-6 — C++ plugin.** A C++ DLL declares and consumes behaviors via
  `kcdxBehaviorInterface`. **Acceptance:** all four verbs reachable from C++'s
  main-stop surfaces; a behavior declared in C++ is settable from Lua and vice
  versa (the cross-language sets exercised at main stops per §6's window law); an
  early-stop set on a plugin-tier behavior gets the out-of-window teaching error in
  BOTH languages — the same wall, symmetrically (the Lua leg's early stop is P5's
  `lua_before`; its acceptance lands with that trigger, §14).

## §4 The behavior noun

Author-supplied declare spec — `kcdx.behavior.declare(name, spec)`:

| Field | Required | Meaning |
|---|---|---|
| `name` | yes (positional arg) | Bare name; engine stamps `<author>.<plugin>.<bare>` (catalog files: `kcdx.behavior.<bare>`) per `naming-namespaces.md` |
| `description` | yes | One human line; surfaced by `list()` |
| `default` | yes | Any Lua value; what `get()` returns when never set |
| `implementation` | yes | `function(value)` — invoked once at the apply boundary with the final settled value |
| `revert` | optional | `function(old_value)` — presence makes the behavior runtime-togglable (§5); absence makes post-load `set` a teaching error |

(`declare`'s required fields ride in the spec table rather than positionally — an
inherited shape from the approved baseline, reconciled with `lua-api-surface.md`
rule 4 by the immediate missing-field teaching error at the declare site (§10),
which preserves the rule's protective property; precedent: `kcdx.bytes{...}` /
`kcdx.code{...}` / `kcdx.command{...}`.)

`value` accepts any Lua type (bool, number, string, table, function) EXCEPT `nil` —
`nil` is the engine's unset sentinel, never a value; `set(name, nil)` is a teaching
error ("to leave a behavior unset, don't set it"). This holds uniformly for
last-wins comparison (§5.2) and the C++ value builders (§8). Beyond that, the
implementation validates per its own logic — **no engine type-gating**. Every value
type is reachable from both surfaces via the §8 handle model — none is Lua-only. No
version field on the behavior: version verification rides the primitives the
implementation calls (§9).

**Duplicate declare of the same stamped full name** — only producible as an
intra-plugin authoring bug or a catalog QA miss, since the prefix is engine-derived
from the declarer's own manifest (a plugin cannot declare under another plugin's
prefix, `lua-api-surface.md` rule 5) — is a teaching error against the SECOND
declare: the first declaration stands, the load continues. (A future hot-reload's
same-plugin re-declare is a reload-scoped reset, not a duplicate — out of scope,
§13.)

Engine-tracked state per behavior (not author-supplied): the declaring plugin, the
current value, the set-edges (which plugins set it — feeds the conflict warn, the
persisted dependency edges, and the auto-order method), and applied/not-yet-applied.

Verbs beside `declare`:

- `kcdx.behavior.set(name, value)` — record a value (load) or toggle (post-load,
  `revert` declarers only). All-positional per `lua-api-surface.md` rule 4.
- `kcdx.behavior.get(name)` — the current recorded value, else the spec's `default`.
  Truthful by construction: the value changes only on a successful `set` (§5).
- `kcdx.behavior.list([prefix])` — all behaviors (both tiers, one registry), each
  with name/description/default/current value/declarer; optional prefix filter
  (`list("kcdx.")` = engine catalog; `list("redmoon.")` = redmoon's).

## §5 Set semantics — record at load, apply once at the boundary, opt-in runtime toggle

The collect-then-apply model (the ApplyZone precedent, `hook-engine.md`):

1. **During plugin load, `set` validates and records.** Name resolution is immediate
   (§6 errors fire at the call site). The value lands in the pending table;
   the implementation is NOT called yet.
2. **Conflicts: last-wins + one teaching warn.** Two plugins setting the same
   behavior to different values → the later plugin (load order) wins; the engine
   logs one warn naming both plugins, the behavior, and both values. The conflict is
   also an edge pair the auto-order method can see. A load never breaks on a
   behavior conflict.
3. **At the apply boundary — after all plugins have loaded — the engine invokes each
   set behavior's implementation exactly once with the final value.** No
   apply-then-unapply churn during load; no undo obligation on declarers for the
   load-time story. Behaviors whose value was never set do not have their
   implementation invoked (the default is a `get()` answer, not an applied state).
   Implementations run in load order of their declaring plugins; an implementation
   error is logged against the DECLARING plugin and does not abort the boundary
   (remaining behaviors still apply). **A boundary raise clears that behavior's
   recorded value to unset and its applied-flag to not-applied** — `get()` returns
   the default (truthful: the intended state was not applied), and a later toggle
   never `revert`s a state the implementation did not create. (A *partially*-applied
   world cannot be un-done by clearing the record; the cleared record is the
   truthful-est available statement, not an undo.)
4. **Post-load `set` is legal only for `revert` declarers.** With `revert`: the
   engine calls `revert(old_value)` then `implementation(new_value)`, then records.
   **Never-applied gate:** if the behavior's implementation has never run (nothing
   set it at load, so the boundary skipped it), the engine SKIPS `revert` and calls
   `implementation(new_value)` only — `revert` is never handed a state the
   implementation did not create; the engine's applied-flag (§4) gates this.
   Without `revert`: a teaching error — "`<name>` applies at load; it cannot change
   mid-session" — and the recorded value does NOT change (`get()` never lies).
   **Toggle failure paths:** if `revert(old)` succeeds but `implementation(new)`
   raises, the engine clears the recorded value to unset AND the applied-flag to
   not-applied — the world is in the reverted state and `get()` returns the
   default, so the surface stays truthful — and logs the error against the declarer
   naming both calls. If `revert(old)` ITSELF raises, the recorded value and
   applied state stay as they were and the error is logged against the declarer —
   the engine cannot know how far a failed revert got, so the standing record is
   the least-lying one. **Thread contract — a set is a COMMAND, uniformly:** `set`
   is never a synchronous apply anywhere on the timeline (a load-time set records
   intent the boundary applies). Post-load, a `Set` from the game main thread
   executes the toggle inline (the degenerate immediate case); a `Set` from ANY
   other thread is QUEUED and executed on the main thread at the next apply point,
   riding the engine's existing off-thread→main dispatch machinery
   (`lua-callback-threading.md` — no new dispatch path bypasses the guard). The
   queue contract: FIFO arrival order, each queued set executing as its own toggle
   (no coalescing). Pump reuse CONFIRMED (FIFO, no coalescing, re-entrant-safe —
   observed `src/task.cpp`); the pump is UNBOUNDED BY RULING (2026-06-11): the
   engine never rations authors — no cap, no rejection. A high-water teaching
   warn at the SHARED pump (attributed, depth-named, generous threshold, one
   integer comparison at enqueue) makes a runaway diagnosable; the warn covers
   ALL pump producers (behavior commands, hook marshaling, plugin AddTask)
   (`.claude/rules/concurrency.md`). A queued toggle's failure logs
   attributed to the declarer (asynchronous — not returned at the call site);
   `get()` flips only when the toggle actually executes, so the surface never lies
   (an off-thread setter may briefly read the prior value — the same
   applies-at-the-next-apply-point semantics load-time sets have). **Failure
   attribution on the queued path is per disposition:** a consumer-misuse failure
   (a revert-less post-load set, `set(name, nil)`, an unresolvable name) logs
   attributed to the SETTING plugin; a declarer-code raise (`revert`/
   `implementation`) logs attributed to the declarer — all asynchronous on the
   queued path. **A set issued from INSIDE an implementation during the boundary
   drain joins a WORKLIST drain:** the boundary keeps draining until no pending
   entry remains — a not-yet-applied behavior's pending value updates (last-wins
   continues); a late-pended behavior whose slot already passed applies after the
   current pass (declaring-plugin order among late entries); **each behavior
   applies at most once per boundary** (once applied, a further mid-drain set hits
   the post-load toggle rules), so the drain terminates and `get()` never carries
   an unapplied value. **`declare` post-load is a teaching error** — declares are
   a load-time act (the window law's own premise); hot-reload revisits this (§13).

The apply boundary's exact placement in the load sequence (a step after the last
plugin's script executes, before the suite/ready events) is fixed at build.
OBSERVED (probe 2026-06-11, `_research/behavior-startup-recon/`): the
point exists — post-`RunPostGameLoad`, pre-`InputLoaded`, on the game main
thread. The boundary pass TRIGGERS ITS OWN ApplyZone drain: a registration
queued at that point otherwise drains only post-InputLoaded (probe F2 — the
in-block ApplyZone passes cover only post-Lua-wave registrations, not
registrations queued at the boundary candidate itself).

## §6 Ordering — discriminating errors, persisted edges, the callable auto-order method

The unified load order is final before any plugin script executes — `load_order::
Resolve` runs synchronously inside DllMain's config parse (`src/dllmain.cpp:424`;
the call at `src/config.cpp:1254`), before the worker even spawns — so a failing
`set` can be diagnosed by lookup at the call site:

**The window law — when a `set` can resolve at all.** Plugin execution is a
timeline with stops, not one wave: every C++ plugin's `kcdxPlugin_Load` runs in the
early worker wave (the dispatch loop at `src/plugin_loader.cpp:820-866`, invoked on
the worker via `kcdx::plugins::DiscoverAndLoad` — `src/dllmain.cpp:303`), while every `plugin.lua` runs
at the game-thread first tick (`src/hooks.cpp:386`) — and Phase 11 P5's settled
startup contract keeps the main Lua entry there while formalizing the stops
([`../phase-11-shim-vm/phase-05-startup-sequence-contract/bring-forward-design.md`](../phase-11-shim-vm/phase-05-startup-sequence-contract/bring-forward-design.md)
§7.3: what needs the live game stays late — zero capability cost). Behaviors adopt
the timeline's own out-of-window law (§7.3's "an out-of-window call fails loud")
rather than any bespoke deferral:

- **Plugin-tier behaviors are a main-stop surface.** Their declares come into
  existence when the declaring plugins' main entries run. A `set` from an EARLIER
  stop (a C++ `kcdxPlugin_Load`, a future `lua_before` slot) against a plugin-tier
  behavior is **out-of-window — a loud teaching error** ("plugin behaviors resolve
  at the main stop; set from your main entry"), the same law every other
  early-window overreach gets. Identical in both languages — the wall is
  early-consumer→late-declarer, not C++-vs-Lua.
- **Catalog-tier behaviors (`kcdx.behavior.*`) are settable from any stop** — the
  engine pack declares them before any plugin runs (§7).
- OBSERVED (probe 2026-06-11, `_research/behavior-startup-recon/`):
  `kcdxPlugin_Load` is an EARLY stop (worker thread, pre-Lua-wave);
  `kcdxPlugin_PostGameLoad` is a MAIN stop (game-main, post-Lua-wave,
  pre-InputLoaded); PostLoad/PostPostLoad subscribers fire on the WORKER at wave
  end. The early-stop interleave half STAYS PROVISIONAL — the P5 tree is silent,
  and the shipped precedent is two sequential passes, not interleaved; until
  `lua_before` lands, §6 does not rest on intra-stop interleaving.

Within the main stop, the resolution-error branches:

- **Prefixed name, owning plugin later in the list** → error names the exact fix:
  "`redmoon.realism` loads after you — move `player-tweak` below it." (First-launch
  wording is calibrated to what the engine actually knows: the prefix's plugin loads
  later, not that it declares that specific name. From the second launch the
  persisted edge confirms the declaration and the error may name the behavior.)
- **Prefixed name, owning plugin already loaded, no such bare name** → the error
  consults the declarer's load outcome first: if the owner's script ERRORED before
  its declares ran, the error says so ("`redmoon.realism` failed to load — fix or
  remove it; your set cannot resolve until it loads"); otherwise "`redmoon.realism`
  is loaded but declares no behavior `hardcore_combta` — check the name against
  `kcdx.behavior.list(\"redmoon.realism.\")`" (a typo, or a behavior the declarer's
  new version removed). No reorder suggestion — none fixes either.
- **Prefixed name, owning plugin absent, disabled, or engine-rejected** →
  "`hardcore_combat` belongs to `redmoon.realism`, which is not installed" — or
  "is installed but disabled (`load_order.toml`)" / "was rejected by the engine
  (<reject reason>)" when the engine sees the row (it knows enabled state and the
  reject reason). No reorder suggestion — none fixes it.
- **Bare name, no declarer found** → a bare name (legal cross-plugin per
  `naming-namespaces.md` self > engine > other resolution) carries no
  `<author>.<plugin>` prefix to discriminate with: "no plugin loaded so far declares
  `<bare>`; if it belongs to another plugin, use its full
  `<author>.<plugin>.<bare>` name." (A persisted edge from a prior launch upgrades
  this to the discriminating form.)

(Within the main stop, resolution against a not-yet-executed-but-earlier plugin
cannot occur: main-stop scripts run sequentially in load order
(`src/lua_plugin_loader.cpp:161-192` + `RunAll`'s sequential loop at `:356-364`),
so an earlier-ordered declarer's declares exist by the time a later consumer runs.)

The error is a normal Lua error in the consumer's script — standard plugin error
handling applies; the consumer plugin fails loudly, the load continues.

**Edges persist; the store self-invalidates.** Each resolved or ordering-failed
`set` records a consumer→declarer edge to a small engine-data file (e.g.
`kcdx-engine/data/behavior_edges.toml` — exact path fixed at build, grep-replace).
At the next launch the engine re-checks known edges against the current order BEFORE
plugin execution and logs the recognized conflicts up front — the user learns about
a bad order even before the failing plugin runs again. The store is rebuilt from
each launch's observed sets (a consumer updated to no longer set a behavior drops
its edge); an edge whose consumer or declarer is absent from the discovered plugin
set is ignored and pruned — no stale edge ever drives a warn or an auto-order
constraint.

**The auto-order method — passive, callable, no UI in 9.5.** An engine function that
computes a corrected load order satisfying the known edges (consumer below declarer;
minimal displacement of unrelated rows; a cycle is reported, never silently broken)
and applies it through a new order write-back path in the `load_order` unit (the
unit is read/resolve-only today; the write-back is part of §11's "existing unit,
extended"). It NEVER fires on its own —
a future UI button is the intended caller — naturally a PRE-LAUNCH surface (the
launcher), since the load order is consumed at boot and a mid-session apply could
only take effect next launch. Until the UI exists the method's only callers are the
engine-internal seam and the test plugin (the §14 gate drives it programmatically
per `headless-testable.md`); no console command — by console-time the session's
order is already consumed, and the persisted edges already surface the conflict at
next open.

## §7 The engine catalog — plain `.lua` files as a builtin pack

`data/behavior-catalog/` holds one `.lua` file per behavior. Each file is a normal
Lua source calling `kcdx.behavior.declare(...)` — written EXACTLY as a plugin would
write it (real files: highlighting, linting, line-numbered errors). The engine loads
the catalog as a builtin behavior pack; its declares are stamped under the reserved
`kcdx.behavior.*` root. The pack's concrete shape — existing builtins are
one-plugin dirs (`kcdx.toml` + script) under `kcdx-engine/builtin/`, while the
catalog is a directory of bare per-behavior `.lua` files — plus its repo→install
deploy mapping is fixed at build via a **catalog-aware loader path** (an
engine-stamped registration, the engine-identity route): a manifest-fronted pack is
structurally blocked, since `[plugin].author = "kcdx"` is hard-rejected by author
validation (the check at `src/config.cpp:373-380`; the reserved-root rejection body
at `src/address_library.cpp:915-924`) and any other author value would stamp the
wrong root.

- **Ordering guarantee:** the catalog pack loads ahead of every user plugin, so
  `kcdx.behavior.*` names are always declared before any user `set` runs — the §6
  immediate-error model never false-positives on engine behaviors. The
  zone/priority mechanism is DISPROVEN as the pin (`RunAll` sorts by priority
  only; the `before_game` ApplyZone slice has no call site) — the ordering
  guarantee is the catalog-aware loader's CALL-SITE PLACEMENT: the engine runs
  the catalog files before `lua_plugin_loader::RunAll`
  (`_research/behavior-startup-recon/FINDINGS.md` §2d).
- **Promotion = file move.** A plugin-tier behavior is promoted by moving its `.lua`
  file into the catalog dir (a PR); the declare code is unchanged — only the
  stamping root differs.
- **No DB, no TOML import.** `list()` reads the one runtime registry, which both
  tiers register through. (The original plan's `behaviors` SQLite table + 
  `behaviors.toml` build-step are dropped — nothing at runtime needs them once
  declare-at-load is the single registration path.)
- **Shipped entries: 5–10 minimum**, all referencing functions with verified RE data.
  The catalog ships on the PUBLIC allowlist, so each catalog file's header comment is
  a **self-contained, public-safe statement of the verified fact** (the function,
  the statement site, the verification method in its own words) — never a private
  provenance pointer (`public-private-boundary.md`; no `_research/` paths, no
  internal scheme tokens). The full evidence trail stays in the private tree where
  the entry was developed. The concrete entry list is selected at `/plan` time
  against the verified corpus; the canonical case study is
  `kcdx.behavior.outfit_swap_in_combat`.
- A malformed catalog file is a boot-time Lua error against the builtin pack —
  surfaced like any builtin failure, gated by the build/test pass (every shipped
  entry is exercised by the test plugin, §14).

## §8 C++ parity — in-phase

`kcdxBehaviorInterface` ships in this phase (the 9.3 precedent), appended to the
interface roster (append-only ABI, `skse-parity.md` / interface versioning rules):

- `Declare(name, desc, default, impl_fn, revert_fn /*nullable*/, user_ctx)` ·
  `Set(name, value)` · `Get(name, out_value)` · `List(prefix, callback)`.
- **Value model: an engine-owned handle into the one VM — values are never
  marshalled out.** The engine already pins a behavior's current value to serve
  `get()`/`list()`; C++ receives an **opaque value handle** valid while that value
  is the behavior's recorded value (the value itself stays in the engine-owned Lua
  VM — Phase 11 P3). **Invalidation contract:** when the recorded value is
  replaced, an outstanding handle goes stale — every accessor on a stale handle
  returns a generation-checked teaching error through the interface's error
  channel; a handle never dangles into the VM. One uniform concept for EVERY Lua
  type, no barred type:
  - **Coercion accessors** for the everyday scalars — `AsBool` / `AsInt64` /
    `AsDouble` / `AsString` (one call for the common case);
  - **Table traversal accessors** for table values;
  - **`Invoke(handle, args…)`** for callable values — half-confirmed reuse of the
    engine's existing C++→Lua call machinery: the pcall harness (ref-invoke,
    error attribution, the string-lifetime arena, the main-thread discipline)
    reuses; the argument-marshal layer is NEW code (the hook path's marshal is
    ABI-typed — `_research/behavior-startup-recon/FINDINGS.md` §2b).
  C++-side value CONSTRUCTION (for `Set` and `default`): typed builders for
  scalars/strings/tables; a C function pointer + context registers as a callable
  value. Exact accessor/builder set fixed at build.
- **Thread contract — commands queue, queries are main-thread.** One law, two
  halves:
  - **Queries** (`Get` and every handle accessor + `Invoke`) need the live VM:
    legal during the load waves under the GATED guarantee (ruling 2026-06-11):
    the loader signals C++-wave end (`DiscoverAndLoad` end) and the engine's
    VM-adoption intercept WAITS on that signal — one-shot, boot-only, never a
    hot path (observed margin ~5.6 s, so the typical wait is zero); an
    order-inversion regression rides the gate's build step — and **post-load
    only on the game main thread**. An off-thread post-load query returns a teaching error naming
    the two sanctioned patterns: capture the value in your `implementation` at
    apply, or copy it out on the main thread. No query hides a blocking marshal.
  - **Commands** (`Set`) work from any thread — off-thread post-load sets queue per
    §5.4's command semantics. Off-thread value CONSTRUCTION stages engine-side as
    the queued command's payload (scalars/strings/function-pointers trivially;
    a table payload as an engine-side description materialized on the main thread
    at execution) — a queued command carries its payload by nature, not a second
    access regime.
- A C++ `implementation`/`revert` is a C function pointer + context; the engine
  invokes it at the same boundary/toggle points as a Lua one, handing it the value
  handle.
- Parity is tested from both surfaces (§14), per `lua-api-surface.md` §"FULL FEATURE
  PARITY" — the handle model exists precisely so no value type is Lua-only.

## §9 Version story — the consumer needs none; the declarer rides the existing tracks

(The original-plan §9.5 text predicated this on the `authored_against_game_version`
manifest field — SUPERSEDED 2026-05-27 by the three-track model and never built
([`../00-original-plan.md`](../00-original-plan.md) item 7); this section is
re-grounded on the model that actually shipped.)

- **A behavior-only consumer needs NO version declaration of any kind** — no
  manifest field, no `kcdx.declare`. Its script touches only
  `kcdx.behavior.set/get/list` and inert surfaces (log, on, publish); it reaches no
  hash-checked verb, so there is nothing version-bound in it. Game-version failures
  surface as teaching errors under the behavior name, attributed to the declarer.
- **A declarer's version story is the existing Track-1/Track-2 model** — curated
  refdb resolution for engine-known targets, `kcdx.declare` per-version specs for
  author-declared ones. **Enforcement point: the hash-checked call sites its
  implementation reaches at the apply boundary** (call-time — CONFIRMED by probe
  9.5-S1: the running build's version row is cached at refdb open and consumed at
  the bind/apply/interface call sites;
  `_research/behavior-startup-recon/FINDINGS.md` §2c), NOT a load-time manifest rejection — a declarer's
  hash-checked calls execute only when its implementation runs, so load-time
  determination is structurally impossible and is not claimed.

## §10 Author UX — the error catalog

Every behavior failure mode teaches (`lua-api-surface.md` rule 3). The shipped error
texts (final wording at build; each names the actor, the cause, and the fix):

| Situation | Surface |
|---|---|
| `set` on a plugin-tier behavior from an early stop | Out-of-window teaching error: names the main-stop rule (§6 window law) |
| Post-load toggle: `revert` succeeds, `implementation(new)` raises | Value + applied-flag cleared to unset, error logged against the declarer (§5.4) |
| Post-load toggle: `revert` itself raises | Record unchanged, error logged against the declarer (§5.4) |
| `set(name, nil)` | Teaching error: nil is the unset sentinel, not a value (§4) |
| Accessor on a stale C++ value handle | Generation-checked teaching error via the interface error channel (§8) |
| Off-thread post-load QUERY (Get/accessor/Invoke) | Teaching error naming the two sanctioned patterns (§8) — a `Set` (command) queues instead, never errors on thread |
| `set` on a prefixed name whose plugin loads later | Error: names both plugins + the exact reorder (§6) |
| `set` on a prefixed name whose loaded plugin declares no such bare name | Error: failed-declarer state when known, else the absent name + the `list()` lookup (§6) |
| `set` on a prefixed name whose plugin is absent, disabled, or rejected | Error: names the missing/disabled/rejected state as the engine knows it |
| `declare` post-load | Teaching error: declares are a load-time act (§5.4) |
| `set` on an undeclared bare name | Error: no declarer found; points at the full `<author>.<plugin>.<bare>` form (§6) |
| Duplicate declare of the same stamped full name | Error against the SECOND declare; first stands (§4) |
| `declare` missing a required spec field | Error at the declare call site: names the missing field + the spec shape |
| C++ value construction/coercion mismatch (e.g. `AsInt64` on a table) | Teaching error via the interface's error channel, names the actual type (§8) |
| Two plugins set the same behavior | Warn: both plugins, both values, who won (§5) |
| Post-load `set` without `revert` | Error: "applies at load; cannot change mid-session" (§5) |
| Implementation raises at the apply boundary | Error logged against the DECLARING plugin, boundary continues (§5) |
| Recognized stale edge at launch | Warn before plugin execution: the known conflict + the auto-order pointer (§6) |
| Catalog file malformed | Builtin-pack boot error (§7) |

Discoverability: `list()` is the browse surface (both tiers, descriptions, current
values); `docs/lua/behavior.md` + `docs/cpp/` interface doc + glossary entries land
in the same change (`docs-discipline.md`).

## §11 Structure — responsibility units

| Unit | Responsibility |
|---|---|
| `src/behavior_registry.{h,cpp}` | The one runtime registry: declares (both tiers), values, edges, applied-state; the apply-boundary pass; the post-load toggle path |
| `src/lua_bind_behavior.cpp` | The four Lua verbs → registry (thin binder, consistent with the `lua_bind_*` roster) |
| `src/behavior_interface.cpp` + `include/kcdx/Interfaces.h` addition | `kcdxBehaviorInterface` (C++ mirror) + value marshalling |
| `load_order` (existing unit, extended) | Edge persistence + the auto-order method + the new order write-back path (it owns order computation today) |
| `lua_plugin_loader.cpp:170-176` (comment update, same change) | The "no entrypoint body depends on another's having run" rationale goes stale — behaviors are the first cross-entrypoint dependency; the comment must state the new model (errors + edges + auto-order, not auto-topo) |
| `data/behavior-catalog/` | The shipped catalog pack (one `.lua` per behavior + an index README) |
| `test-plugins/cap-NN-behavior/` | The regression plugin: Lua + C++ legs (§14) |

Reference doc: `docs/lua/behavior.md` (author-facing) lands with the code; the
subsystem doc rides the existing docs tree per `structure-by-responsibility.md` §6.

## §12 Decision table

| Concern | Pick | Rejected — why |
|---|---|---|
| Set on undeclared name | Immediate discriminating teaching error | Deferred binding — breaks set-then-get coherence, hides the load-order model the ecosystem already teaches; two-phase apply of *resolution* — changes set timing semantics |
| Load-order help | Persisted edges + passive callable auto-order method (programmatic seam only; pre-launch UI later) | Active auto-reorder — engine silently mutates the user-owned order; error-only — user fixes one edge per boot; an in-game console trigger — the session's order is already consumed by console-time |
| Apply semantics | Record at load, apply once at boundary; optional `revert` unlocks runtime toggle | Eager per-set apply — needs undo even for conflict-free loads; pure load-time config — `get()` can lie after a post-load set; mandatory revert — undo burden on every declarer |
| Conflicts | Last-wins + teaching warn | First-wins — inverts the ecosystem's later-overrides-earlier norm; hard error — breaks a load on a tweak overlap (user-UX cornerstone) |
| Catalog format | Plain `.lua` files as a builtin pack | Lua-in-TOML strings — unlintable, line numbers lost; declarative recipe schema — a second implementation model + capped expressiveness; SQLite `behaviors` table + import — no runtime consumer left once declare-at-load is the single registration path |
| C++ parity timing | In-phase (`kcdxBehaviorInterface`) | Phase-12 backfill — tracked-debt window where the capability is Lua-only |
| Value typing | None (any Lua type; implementation validates) | Engine type-gating — per original plan, unchanged |
| Version story | Consumer: none at all; declarer: existing Track-1/Track-2, enforced at hash-checked call sites (§9) | Reviving `authored_against_game_version` — superseded 2026-05-27, never built; load-time rejection contradicts call-time determination |
| C++ value model | Engine-owned handle into the one VM + coercion/traversal/invoke accessors (§8) | Tagged-variant marshalling — copies values out, makes `function` unrepresentable; barring function values at the boundary — a Lua-only capability, against the parity law |
| Early-stop set on a plugin-tier behavior | The timeline's out-of-window law: fails loud, main-stop rule taught (§6) | A deferred-resolve pending queue — bespoke machinery + two resolution timings on one verb; restricting the C++ surface only — a parity hole; unifying the waves in 9.5 — re-opens P5's settled design + live-game capability physics makes one wave impossible (P5 design §7.3) |
| Thread contract | Command-query split: a `Set` queues from any thread (engine marshals, async, attributed failures); queries (Get/accessors/Invoke) are load-wave + main-thread-only, off-thread fails loud with pattern pointers (§5.4/§8) | Main-thread-only Set — hands the engine's scheduling job to the author (no marshal primitive exists; the workaround is a hook installed to borrow the main thread — the lifecycle disassembler-test failure); blocking marshal-everything — invisible blocking, deadlock surface, hot-path cost; scalar-cache two-tier — splits "one uniform concept" |
| Duplicate declare (same full name) | Teaching error against the second declare; first stands | Last-wins + warn — swaps the implementation under recorded sets (the surface lies); only producible as an authoring bug since prefixes are engine-derived |

## §13 Out of scope

- The auto-order UI/button and any in-game settings surface (the method + its
  programmatic seam are the 9.5 deliverable; the future button is a pre-launch
  surface).
- Presentation metadata on the declare spec (value-kind hints, ranges, display
  labels for a future settings UI) — additive later; nothing in the current shape
  blocks it. Explicitly NOT specced now.
- High-level gameplay namespaces (`kcdx.player.*` etc.) — TD-0005.
- Promoting the edge store beyond behaviors (general inter-plugin dependency
  declaration) — not this phase.
- Hot-reload semantics (a same-plugin re-declare as a reload-scoped reset) — no
  hot-reload exists today; specced when one does.

## §14 Verification gate (whole phase)

- `set` against a catalog entry → the underlying rewrite applies at the boundary
  (observable via the entry's effect), not at the call site.
- Undeclared-name `set` → all three error branches (reorder fixture + missing
  fixture + bare-name fixture).
- Conflict fixture (two setters) → last value applied once; warn names both.
- Post-load `set` without `revert` → teaching error, value unchanged; with `revert`
  → revert+implementation called in order, `get()` tracks; on a never-applied
  behavior → revert skipped, implementation only (§5.4).
- Behavior-only plugin with no version declaration of any kind loads cleanly; a
  declaring implementation reaching a hash-checked verb is verified at the call
  site/boundary per the existing Track-1/Track-2 enforcement (§9).
- `list()` returns both tiers; prefix filter works.
- Cross-plugin: A declares `a.test.foo`; B sets it; A's implementation fires with
  the value at the boundary.
- C++ leg: all four verbs via `kcdxBehaviorInterface`; a C++-declared behavior set
  from Lua and vice versa.
- Duplicate-declare fixture → error against the second declare; the first behavior
  still functions. Missing-spec-field fixture → teaching error at the declare site.
- Window-law fixtures: an early-stop set on a plugin-tier behavior → the
  out-of-window error; an early-stop set on a catalog behavior → resolves and
  applies. The C++ leg (`kcdxPlugin_Load`) is buildable now; **the Lua leg's named
  trigger is Phase 11 P5's `lua_before` slot landing** (no Lua early stop exists
  before then) — its fixture row lands with that trigger per
  `.claude/rules/test-discipline.md` §"Bucket 2".
- Boundary-raise fixture: an implementation that raises at the apply boundary → the
  error logs against the declarer, remaining behaviors still apply.
- Toggle-failure fixtures: `revert` succeeds, `implementation(new)` raises → value
  reads unset, error attributed; `revert` itself raises → record unchanged, error
  attributed. `set(name, nil)` fixture → teaching error.
- C++ value-model fixtures: a coercion mismatch (`AsInt64` on a table) → teaching
  error; an accessor on a stale handle (value replaced) → generation-checked error,
  no crash; an off-thread post-load QUERY → the thread-contract teaching error, no
  crash; an off-thread post-load SET → queues, the toggle executes on the main
  thread at the next apply point, `get()` flips after execution.
- Resolution-branch fixtures beyond the three above: the typo branch (owner loaded,
  no such bare name); the disabled-owner variant; a failed-declarer fixture (the
  owner's script errors → the consumer's error names the failed load). Post-load
  `declare` fixture → teaching error.
- Boundary-drain set fixtures (both branches): an implementation setting a
  not-yet-applied behavior → pending value updates, applies once; an implementation
  setting an already-passed never-set behavior → applies in the worklist pass, once.
- Queued-path failure fixture: an off-thread set hitting a consumer-misuse
  disposition → async error attributed to the SETTER; a declarer-code raise →
  attributed to the declarer.
- Off-thread table-payload set fixture: a table value built off-thread → staged
  description materializes at execution, implementation receives the table.
- Auto-order: a deliberately mis-ordered fixture's persisted edges → the method
  yields and applies a corrected order; a cycle fixture → reported, not applied.
- Launch-time recognition: a persisted stale edge from a prior run → the up-front
  warn fires before plugin execution (§6/§10).
- Malformed catalog file fixture → the builtin-pack boot error surfaces (§7/§10).
- Every shipped catalog entry exercised (at minimum: declares cleanly + applies
  against the live binary in the suite run).
