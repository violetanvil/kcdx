# kcdx.behavior
> Part of the [kcdx Lua API](index.md).

Named behaviors: a behavior is a named, settable unit of intent — a value plus
the declarer's `implementation` that reconfigures the game to match it, under
the engine's apply contract. Think of it as a CVar whose setter is mod-authored.
Two tiers register through one model and one code path:

- **engine catalog** — engine-shipped behaviors under the reserved
  `kcdx.behavior.<bare>` names (the catalog pack is not shipped yet; until it
  lands, `list()` shows plugin-declared behaviors only);
- **plugin-declared** — behaviors a plugin declares, stamped
  `<author>.<plugin>.<bare>` from the declaring plugin's manifest. You write
  the bare name; the engine stamps the prefix. You never type your own prefix.

This page covers the four verbs — `declare`, `set`, `get`, `list` — the
**apply-boundary model** that makes a load-time `set` work, and the **post-load
toggle** a `set` performs after load on a behavior whose declarer shipped a
`revert`. A post-load `set` on a behavior *without* a `revert` raises a teaching
error (it applies once at load; it cannot change mid-session).

| Call | Args | Returns |
|---|---|---|
| `kcdx.behavior.declare(name, spec)` | bare name string; spec table (`description`, `default`, `implementation`, optional `revert`) | nothing on success; **raises** a teaching error on a bad spec, a duplicate name, or a post-load call. |
| `kcdx.behavior.set(name, value)` | behavior name (bare or full); any non-`nil` value | nothing on success. At **load** the value is **recorded** and the implementation runs once at the apply boundary; **post-load** on a `revert` declarer it **toggles** (revert-then-implementation, recorded). **Raises** a teaching error on an unknown name, a `nil` value, or a post-load set on a behavior with no `revert`. |
| `kcdx.behavior.get(name)` | behavior name (bare or full) | the behavior's current value — the recorded value once one exists, else the spec's `default`; **raises** a teaching error on an unknown name. |
| `kcdx.behavior.list([prefix])` | optional stamped-name prefix string | an array of entry tables `{ name, description, default, current, declarer }`. |

Errors here **raise** (a normal Lua error at your call site, naming the cause
and the fix) rather than returning `(nil, err)` — a behavior misdeclaration is
an authoring bug the load should fail loudly on; your plugin errors, the rest
of the load continues.

## `kcdx.behavior.declare(name, spec)` — declare a behavior you own

Registers a named behavior under your plugin's stamped namespace. Declaring is
a **load-time act**: call it from your `plugin.lua` (or `lua_after`) body — a
declare arriving after the load waves finish raises a teaching error.

```lua
kcdx.behavior.declare("hardcore_combat", {
    description    = "lock fast-travel and timed saves while in combat",
    default        = false,
    implementation = function(value)
        -- reconfigure the game to match `value`; invoked once at the apply
        -- boundary with the final settled value
    end,
})
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | The BARE behavior name (no dots). The engine stamps `<author>.<plugin>.<name>` from your `[plugin]` manifest — e.g. author `redmoon`, plugin `realism` ⇒ `redmoon.realism.hardcore_combat`. |
| `spec.description` | string, required | One human line; surfaced by `list()`. |
| `spec.default` | any non-`nil` value, required | What `get()` returns while the behavior was never set. Any Lua type (bool, number, string, table, function) EXCEPT `nil` — `nil` is the engine's unset sentinel, never a value. |
| `spec.implementation` | function, required | `function(value)` — invoked once at the apply boundary with the final settled value. |
| `spec.revert` | function, optional | `function(old_value)` — its presence makes the behavior **runtime-togglable** after load. A post-load `set` then calls `revert(old_value)` to undo the prior applied state, then `implementation(new_value)` to apply the new one (see [The post-load toggle](#the-post-load-toggle--runtime-change-on-revert-declarers)). Omit it and the behavior applies once at load; a post-load `set` raises a teaching error. |

**Returns:** nothing. The behavior is registered and immediately resolvable by
`get`/`list`.

**Errors (each raises at the declare site, naming the field and the fix):**

- a missing/wrong-typed required field (`description` not a string, `default`
  missing or `nil`, `implementation` not a function) — the error names the
  missing field and the spec shape;
- an unrecognised spec key (a typo is never silently dropped);
- a dotted `name` (write the bare name; the engine stamps your prefix);
- a **duplicate declare of the same stamped full name** — the error fires
  against the SECOND declare; the first declaration stands and keeps working.
  Only your own plugin can produce this collision (the prefix is
  engine-derived), so it is an in-plugin authoring bug to remove;
- a post-load declare (declares are a load-time act).

## `kcdx.behavior.set(name, value)` — set a behavior's value

Sets a value for a declared behavior. At **load** (your `plugin.lua` /
`lua_after` body) the value is recorded immediately (`get` reads it back at
once) and the behavior's `implementation` runs **once, at the apply boundary**
(below) with the final recorded value — never at the `set` call site.
**After load**, a `set` is a runtime **toggle** — but only for a behavior whose
declarer shipped a `revert` (see [The post-load toggle](#the-post-load-toggle--runtime-change-on-revert-declarers));
on a behavior without a `revert` a post-load `set` raises a teaching error.

```lua
-- your whole plugin can be these two lines:
kcdx.behavior.set("redmoon.realism.hardcore_combat", true)
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | A bare name (resolved **self > engine > other**) or the explicit full `<author>.<plugin>.<bare>` form — setting another plugin's behavior uses its full name. |
| `value` | any non-`nil` value | The setting. Any Lua type the declarer's `implementation` accepts (bool, number, string, table, function). `nil` is the engine's unset sentinel, never a value — to leave a behavior unset, don't set it. `false` is a valid setting. |

**Returns:** nothing. The value is recorded; `get(name)` now answers it.

**Conflicts — last-wins, one teaching warn.** When two plugins set the same
behavior to different values, the **later plugin in load order wins** and the
engine logs one `set_conflict` warn naming both plugins, both values, and the
winner. A load never breaks on a behavior conflict. A plugin re-setting its
own value, or a second plugin recording an equal value, records silently.

**Errors (each raises at the set site, naming the cause and the fix):**

- an unknown name — a **discriminating** teaching error (see "Resolution
  errors" below) that tells you the exact fix — reorder, install, enable, or a
  name typo — never a bare "not found";
- `set(name, nil)` — nil is the unset sentinel, not a value;
- a post-load set on a behavior **with no `revert`** — after the apply boundary
  has run (or on a behavior the boundary already applied), a set on a behavior
  whose declarer shipped no `revert` raises a teaching error ("applies at load;
  it cannot change mid-session"). A behavior **with** a `revert` toggles instead
  — see [The post-load toggle](#the-post-load-toggle--runtime-change-on-revert-declarers).

## Resolution errors — when a `set` can't find the behavior

A `set` on a name that does not resolve to a declared behavior does NOT fail
with a generic "not found" — it tells you the **exact** fix. The engine knows
the load order, which plugins are installed/enabled, and which declared what,
so it names the right correction. The branches:

| What you did | What the engine knows | The error |
|---|---|---|
| Set a prefixed `<author>.<plugin>.<bare>` whose owning plugin **loads later than you** | the owner is installed + enabled, but its declares run after your set | "`<owner>` loads after you — move `<you>` below it" (the exact reorder; or use the auto-order method below). |
| Set a prefixed name whose owning plugin **is loaded but declares no such bare name** | the owner ran, registered behaviors, but not this one | "`<owner>` is loaded but declares no behavior `<bare>` — check the name against `kcdx.behavior.list("<owner>.")`" (a typo, or a name a newer version removed). No reorder — none fixes a wrong name. |
| Set a prefixed name whose owning plugin **failed to load** | the owner's script errored before its declares ran | "`<owner>` failed to load — fix or remove it; your set cannot resolve until it loads." No reorder. |
| Set a prefixed name whose owning plugin is **not installed / disabled / engine-rejected** | the engine sees the install + enabled state + any reject reason | "`<bare>` belongs to `<owner>`, which is not installed" / "is installed but disabled (`load_order.toml`)" / "was rejected by the engine (`<reason>`)". No reorder. |
| Set a **bare** name no plugin declares | a bare name carries no `<author>.<plugin>` prefix to discriminate with | "no plugin loaded so far declares `<bare>`; if it belongs to another plugin, use its full `<author>.<plugin>.<bare>` name." |

The error is a normal Lua error in your script (the call site fails loudly; the
load continues). Each set is also **recorded as a dependency edge** — "this
consumer set this behavior" — which kcdx **persists across launches** (in
`kcdx-engine/behavior_edges.toml`, an engine-managed file; don't hand-edit it).
Two things you SEE because of the persisted store:

- **The next launch recognizes a bad order up front.** When kcdx records that
  you set a behavior whose declarer loads *after* you, it re-checks that edge at
  the very next launch — **before any plugin runs** — and logs a warn naming
  both plugins, the behavior, and the fix. You learn about the wrong order even
  before the failing plugin runs again.
- **From the second launch the error names the behavior confidently.** On the
  first launch the engine only knows "that prefix's plugin loads later." Once an
  edge is persisted, a later reorder error — or a bare-name "no declarer" error —
  **names the exact behavior and declarer** the prior launch resolved, instead of
  the hedged first-launch wording.

The store is **self-invalidating**: it is rebuilt from each launch's observed
sets, so a consumer you updated to no longer set a behavior drops its edge; an
edge whose consumer or declarer is no longer installed is ignored and pruned (no
stale edge ever drives a warn).

### The window law — plugin behaviors resolve at the main stop

A **plugin-tier** behavior (`<author>.<plugin>.<bare>`) comes into existence
when the declaring plugin's **main entry** runs (your `plugin.lua` /
`lua_after` / a C++ `kcdxPlugin_PostGameLoad`). A `set` on a plugin-tier
behavior from an **earlier stop** (a C++ `kcdxPlugin_Load`, or a future
`lua_before` slot) is **out-of-window** — it fails loud: "plugin behaviors
resolve at the main stop; set from your main entry." The declarer's behaviors
do not exist yet at the early stop, so the engine teaches the fix rather than
silently missing. **Engine-catalog names (`kcdx.behavior.*`) are settable from
any stop** — the engine declares them before any plugin runs, so they are
always available. (The early-stop callers — the C++ `kcdxPlugin_Load` set and
the Lua `lua_before` set — arrive with their own surfaces; from a normal
`plugin.lua`, every set is already at the main stop.)

### Fixing a bad order — the auto-order method

When the engine recognizes a wrong order (a consumer set above its declarer),
the fix is a **callable auto-order method** that computes a corrected load
order satisfying the recorded dependencies (consumer below declarer) and writes
it back to `load_order.toml` — no engine ever silently reorders your list. It is
invoked on demand (a future launcher button is the intended caller); the
persisted edges (above) surface the conflict up front at the next launch. It
moves only the rows that must move (an unrelated plugin keeps its position), and
if the dependencies form a **cycle** (two plugins each needing to load after the
other) it **reports** the cycle and leaves your order unchanged rather than
guessing. The correction takes effect at the **next launch** (the load order is
consumed at boot — a mid-session apply only matters next time you start). The
full model is in [load-order.md](../load-order.md) ("Auto-order — the engine can
FIX a bad order, on demand").

## The apply boundary — record at load, apply once

Behaviors follow a **collect-then-apply** model:

1. **During plugin load, `set` records.** Last-wins across all setters; the
   implementation is not called yet.
2. **At the apply boundary — after every plugin has loaded** (after all
   `plugin.lua` / `lua_after` / C++ post-load entries have run), **before the
   `input_loaded` lifecycle event** — the engine invokes each set behavior's
   `implementation` exactly once with the final value, in the declaring
   plugins' load order. No apply-then-unapply churn during load.
3. **Never-set behaviors are skipped.** The `default` is what `get` answers,
   not an applied state — an implementation never runs for a behavior nobody
   set.

Inside the boundary, the engine keeps draining until everything settles
(the **worklist**): an implementation may itself `set` other behaviors —
a not-yet-applied behavior's pending value updates (last-wins continues); a
behavior whose turn had already passed is picked up by a follow-up pass. Each
behavior applies **at most once per boundary** — once applied, a further set
follows the post-load rules above.

**If an implementation raises** at the boundary, the error is logged against
the **declaring** plugin, that behavior's recorded value is cleared back to
unset (`get` returns the default — the surface never claims a state the
implementation did not deliver), and the remaining behaviors still apply.

An implementation may register intent (`kcdx.hook.*`, `kcdx.bytes`,
`kcdx.statement.*`) like any load-time code: registrations made at the
boundary are installed before `input_loaded` fires.

## The post-load toggle — runtime change on `revert` declarers

After the apply boundary, a behavior is settled for the session **unless its
declarer shipped a `revert`**. A `revert` makes the behavior **runtime-togglable**:
a post-load `set` re-runs the declarer's code to move from the old value to the
new one.

```lua
kcdx.behavior.declare("hud_scale", {
    description    = "HUD scale multiplier",
    default        = 1.0,
    implementation = function(value) apply_hud_scale(value) end,
    revert         = function(old_value) clear_hud_scale(old_value) end,
})
-- … later, after load (e.g. from a settings handler):
kcdx.behavior.set("redmoon.ui.hud_scale", 1.5)   -- toggles: revert(old) → implementation(1.5)
```

A post-load `set` on a `revert` declarer:

1. **Was the behavior applied at load?** If the behavior **was** applied (a
   load-time set ran its implementation at the boundary), the engine calls
   `revert(old_value)` first — undoing the applied state — then
   `implementation(new_value)`, then records the new value. `get` now answers it.
2. **Never applied?** If nothing set it at load (the boundary skipped it), the
   engine **skips `revert`** and calls `implementation(new_value)` only — `revert`
   is never handed a state the implementation did not create — then records.

**`get` stays truthful, even on a failure.** If a toggle's declarer code raises,
the record reflects what actually happened — never a value that was not applied:

| The toggle | What the engine does | What `get` reads after |
|---|---|---|
| `revert(old)` succeeds, `implementation(new)` **raises** | the record **and** applied flag clear to unset; the world is in the reverted state | the spec's `default` (the new state was not created) |
| `revert(old)` **itself raises** | the record and applied state are kept **unchanged** (the engine cannot know how far the failed revert got — the standing record is the least-lying one) | the prior value (unchanged) |

A declarer-code raise (in `revert` or `implementation`) is logged against the
**declaring** plugin — not the plugin that called `set` — and the `set` does
**not** raise at your call site (the failure is the declarer's, surfaced in the
log). A behavior **without** a `revert` raises a teaching error on a post-load
`set` ("applies at load; it cannot change mid-session") and its record is
untouched.

**Thread + queue (today):** a post-load `set` runs **inline on the game main
thread**. Off-thread post-load sets — queued and executed on the main thread at
the next apply point — arrive with the C++ command-queue step.

## `kcdx.behavior.get(name)` — read a behavior's current value

Returns the behavior's recorded value once one exists, else the spec's
`default`. Truthful by construction: the value changes only on a successful
set, and a boundary raise clears it back to unset — `get` never reports a
state the implementation did not (or will not) receive.

```lua
local hardcore = kcdx.behavior.get("hardcore_combat")
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | A bare name — resolved **self > engine > other** (your own declaration first, then an engine `kcdx.behavior.<bare>` name, then another plugin's) — or the explicit full form: `"redmoon.realism.hardcore_combat"` / `"kcdx.behavior.<bare>"`, unambiguous from anywhere. |

**Returns:** the behavior's value (any Lua type the declarer chose).

**Errors:** an unknown name raises a teaching error pointing you at
`kcdx.behavior.list()` and the full `<author>.<plugin>.<bare>` form — never a
silent `nil` (so a typo cannot read as "the behavior is unset").

```lua
-- reading another plugin's behavior: use its full stamped name
local essential = kcdx.behavior.get("redmoon.realism.npc_essential_list")
```

## `kcdx.behavior.list([prefix])` — browse the registered behaviors

Returns every registered behavior (both tiers, one registry) as an array of
entry tables, in stamped-name order. The browse surface: what exists, what each
does, what it currently reads as, and who declared it.

```lua
for _, b in ipairs(kcdx.behavior.list("redmoon.realism.")) do
    kcdx.log.info("MYMOD", b.name .. " = " .. tostring(b.current)
        .. " — " .. b.description .. " (by " .. b.declarer .. ")")
end
```

| Arg | Type | Meaning |
|---|---|---|
| `prefix` | string, optional | A stamped-name prefix filter: `list("redmoon.")` = everything redmoon publishes; `list("redmoon.realism.")` = one plugin's; `list("kcdx.behavior.")` = the engine catalog. Omit for everything. |

**Returns:** an array of `{ name, description, default, current, declarer }`
tables — `name` is the stamped full name, `current` is what `get(name)` would
return (the recorded value, else `default`), `declarer` is
`"<author>.<plugin>"` (or `"kcdx"` for an engine-catalog entry). An empty
array means nothing matches the prefix.

**Errors:** a non-string `prefix` raises a teaching error.

This is the Lua surface of the named-behavior registry; the C++ mirror is the
planned [kcdxBehaviorInterface](../cpp/behavior.md) (not yet implemented).
