# Cross-plugin extension
> Part of the [kcdx Lua API](index.md).

How plugins extend each other. Two directions: **make your own plugin
extensible** so other mods can build on it, and **extend another plugin** that
exposed extension points. Both lead with the **disassembly-free** paths — events,
behaviors, and plain Lua — which need no reverse engineering on either side. A
disassembler is the boundary of supported extension, never the common path.

The mental model: an author who *wants* their plugin extended publishes the
extension points (events, behaviors, declared functions); an author extending it
consumes those points by name. When both authors use these surfaces, neither
touches a disassembler.

---

## Make your plugin extensible (author A)

You are author A: you want other mods to hook into, reconfigure, or build on
your plugin. Expose extension points, highest-leverage first.

### 1. Publish events at your extension points — `kcdx.publish`

The recommended primary surface, and the lowest-friction one for your
consumers. Wherever something interesting happens in your plugin, broadcast a
named event; any other plugin subscribes by name with no knowledge of your
internals. This is the established mod-event pattern.

```lua
-- in your plugin, when the player swaps outfit:
kcdx.publish("outfit_changed", { slot = 2, name = "Noble" })
```

The engine stamps your `<author>.<plugin>` namespace in front, so a consumer
hears it as `"<your_author>.<your_plugin>.outfit_changed"`. See
[publish.md](publish.md). Events are the cleanest extension point: you decide
what to broadcast, consumers decide what to do, and neither side sees the other's
code.

### 2. Declare behaviors for configurable rules — `kcdx.behavior.declare`

When your plugin has a setting another mod (or a config) should be able to
change, declare it as a named [behavior](behavior.md) instead of burying it in a
local variable. A behavior is a value plus your `implementation` that
reconfigures the game to match it; consumers set it by name.

```lua
kcdx.behavior.declare("hardcore_combat", {
    description    = "lock fast-travel and timed saves while in combat",
    default        = false,
    implementation = function(value) --[[ reconfigure to match `value` ]] end,
})
```

Another plugin then calls `kcdx.behavior.set("<your_author>.<your_plugin>.hardcore_combat", true)`
— no function name, no address. See [behavior.md](behavior.md).

### 3. Write extensible logic in Lua

A Lua function in your `plugin.lua` is wrappable by any other plugin **natively**
— plain Lua, no engine involvement, no disassembly. If you expose your logic as
Lua functions (rather than only as compiled C++), an author B can wrap, replace,
or chain them with ordinary Lua. The most extensible plugin is one whose
behavior lives in Lua that others can read and wrap.

### 4. Declare your DLL's public functions — `kcdx.dll.declare`

If your plugin ships a C++ DLL and you want other plugins to hook specific
functions in it **by name**, declare those functions with their signatures —
copied from your own source, no disassembly:

```lua
kcdx.dll.declare("redmoon.outfit", {
    CanSwapInCombat = { signature = "bool (ptr self)" },
    OnOutfitSwap    = { signature = "void (ptr self, i32 outfit_id)" },
})
```

The declared functions become reachable at
`kcdx.functions["redmoon.outfit"].*`, so any plugin hooks them with
`kcdx.hook.*` and zero disassembly — the signature the consumer needs came free
from your source. See [dll.md](dll.md).

### 5. Ship your `.pdb` if you want your internals statically modifiable

If you want other plugins to make **static** ([`kcdx.statement.*`](statement.md))
changes to your DLL's *internal* (non-exported) functions, ship the `.pdb` your
compiler already produces next to the DLL. kcdx reads it at load and exposes
every internal function's name and address — making static ops on your internals
zero-friction for others. Purely additive: ship no PDB and you lose nothing; it
just keeps your internals private. (A callback hook on an internal still needs a
signature, which a release PDB does not carry — but the address, the harder
half, comes free.)

---

## Extend another plugin (author B)

You are author B: plugin A exposed extension points and you want to build on
them. Use the matching consumer surface — all disassembly-free.

### Subscribe to A's events — `kcdx.on`

If A publishes events, subscribe by name. No knowledge of A's code:

```lua
kcdx.on("redmoon.outfit.outfit_changed", function(payload)
    kcdx.log.info("MYMOD", "outfit -> " .. payload.name)
end)
```

See [on.md](on.md). This is the cleanest way to react to another plugin.

### Reconfigure A's behaviors — `kcdx.behavior.set`

If A declared a behavior, set it by name to change A's rules — you name a value,
never a function or an address:

```lua
kcdx.behavior.set("redmoon.outfit.hardcore_combat", true)
```

See [behavior.md](behavior.md).

### Wrap A's Lua functions — plain Lua

If A's logic is in Lua, wrap it with ordinary Lua — capture the original and call
through. No engine surface, no disassembly:

```lua
local original = redmoon_outfit.can_swap
redmoon_outfit.can_swap = function(...)
    -- your logic, then defer to the original
    return original(...)
end
```

### Hook A's declared C++ functions by name — `kcdx.hook.*`

If A declared its DLL functions with `kcdx.dll.declare` (or shipped a PDB), hook
them by name — A already supplied the signature, so you write none:

```lua
kcdx.hook.before("redmoon_outfit.dll",
    kcdx.functions["redmoon.outfit"].OnOutfitSwap,
    function(self, outfit_id) kcdx.log.info("MYMOD", "swap " .. outfit_id) end)
```

See [hook.md](hook.md) and [functions.md](functions.md).

---

## The one boundary case — A's stripped, undeclared, compiled internal

Every path above is disassembly-free. There is exactly **one** case that is not:
you want a **callback hook** on an internal C++ function in A's DLL that A
**neither declared** (`kcdx.dll.declare`) **nor shipped a PDB for**. The address
is not exposed and the signature is not declared anywhere, so this is the rare
expert fallback — you reverse-engineer it yourself, **or** (the better fix) ask
author A to add a one-line `kcdx.dll.declare` for that function so you and every
other consumer can hook it by name.

This case is "B is doing something A never designed for" — and is the explicit
**boundary of supported extension**. It is the only path where a disassembler
enters; everything an author A reasonably exposes, an author B consumes by name.
