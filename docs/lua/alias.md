# kcdx.alias
> Part of the [kcdx Lua API](index.md).

Give a long, prefixed shared name a short **local handle** scoped to your
plugin. After declaring an alias, you can write the short handle anywhere a
target name is expected (a [`kcdx.hook`](hook.md) / [`kcdx.bytes`](bytes.md)
`target =`), and the engine substitutes the full name before resolving.

**Call shape:** positional (a "do a thing" call — two obvious args). Returns
`true` on success, or `(false, err)` on a bad call.

```lua
kcdx.alias("inv", "redmoon.open_inventory")
kcdx.hook{ name = "log_open", target = "inv", before = function() ... end }
```

## Arguments

| Position | Type | Meaning |
|---|---|---|
| 1 — `short` | string | The local handle to declare. A bare name (`[a-z0-9_]`, 2–32 chars). |
| 2 — `target` | string | The full name it aliases — another plugin's prefixed name (`"redmoon.open_inventory"`) or your own long/awkward name. Non-empty. |

## Returns

`true` on success. On bad input returns `(false, err)` where `err` is a teaching
string — wrong type, an invalid `short` charset/length, an empty `target`, or
an **anonymous caller** (console / pak Lua with no owning plugin: an alias has
no plugin to scope to, so it is rejected).

## Behaviour

- **Plugin-scoped, local.** The alias resolves **only** inside the declaring
  plugin. Another plugin's `target = "inv"` does not see it.
- **Cannot shadow.** An alias only *adds* a handle — it never displaces
  resolution. It cannot shadow an engine name, another plugin's bare name, or
  the reserved `kcdx.` root. It sits on top of the
  [self > engine > other](targets.md#resolving-a-name--self--engine--other)
  model as pure local convenience.
- **Launch-time only.** Recorded once at the `kcdx.alias` call (plugin-load
  time) and read only during the apply pass — never on a hook-fire / per-frame
  path, so it adds zero runtime cost.

## Minimal snippet

```lua
-- alias another plugin's named target, then hook it through the short handle
kcdx.alias("inv", "redmoon.open_inventory")

kcdx.hook{
    name   = "log_inventory_open",
    target = "inv",                       -- substituted to redmoon.open_inventory
    before = function() kcdx.log.info("INV", "opened") end,
}
```

## Glossary

- **alias** — a local, plugin-scoped handle for a full shared name, declared
  with `kcdx.alias(short, target)`. Adds a name; never shadows or displaces
  resolution. See [author-declared targets](targets.md).
