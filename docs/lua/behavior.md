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

This page covers the registry's **declare and read side**: `declare`, `get`,
`list`. **`kcdx.behavior.set` is not callable yet** — recording a value and the
apply boundary that invokes implementations ship next; until then a behavior's
value is always its declared `default` and no `implementation` is invoked.

| Call | Args | Returns |
|---|---|---|
| `kcdx.behavior.declare(name, spec)` | bare name string; spec table (`description`, `default`, `implementation`, optional `revert`) | nothing on success; **raises** a teaching error on a bad spec, a duplicate name, or a post-load call. |
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
        -- boundary with the final settled value (once `set` ships)
    end,
})
```

| Arg | Type | Meaning |
|---|---|---|
| `name` | string | The BARE behavior name (no dots). The engine stamps `<author>.<plugin>.<name>` from your `[plugin]` manifest — e.g. author `redmoon`, plugin `realism` ⇒ `redmoon.realism.hardcore_combat`. |
| `spec.description` | string, required | One human line; surfaced by `list()`. |
| `spec.default` | any non-`nil` value, required | What `get()` returns while the behavior was never set. Any Lua type (bool, number, string, table, function) EXCEPT `nil` — `nil` is the engine's unset sentinel, never a value. |
| `spec.implementation` | function, required | `function(value)` — invoked once at the apply boundary with the final settled value. |
| `spec.revert` | function, optional | `function(old_value)` — its presence makes the behavior runtime-togglable after load. |

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

## `kcdx.behavior.get(name)` — read a behavior's current value

Returns the behavior's recorded value once one exists, else the spec's
`default`. Truthful by construction: the value changes only on a successful
set, and nothing has set anything yet — today every `get` answers the default.

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
