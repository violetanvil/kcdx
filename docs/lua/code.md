# kcdx.code
> Part of the [kcdx Lua API](index.md).

Allocate a region of executable memory and (optionally) fill it with machine
code and publish its address as a named symbol. Succeeds the v0.1
`[[trampoline]]` TOML schema. Use it to inject code (not just rewrite
equal-length bytes — that is `kcdx.bytes`): build a routine other hooks branch
to, or reserve a NOP region other plugins patch into by symbol.

**Call shape:** a single named-field table. Unlike `kcdx.hook`/`kcdx.bytes`,
`kcdx.code` is **not deferred** — it allocates *immediately at the call* and
returns a live [`kcdx.memory.pointer`](memory.md) userdata to the region (so
you can write code into it with `:set_byte`/`:set_dword`/… or pass it as a hook
target right away). Returns `(nil, err)` on a bad call.

## Fields

| Field | Type | Meaning |
|---|---|---|
| `name` | string | **Required.** Label used in logs and export-collision diagnostics. |
| `bytes` | string | Optional initial machine code as a hex string, e.g. `"48 83 EC 28"`. Copied to the head of the region. |
| `size` | integer | Optional total bytes to allocate. Defaults to the length of `bytes`. If larger than `bytes`, the tail is **NOP-padded** (`0x90`) so other plugins can patch into the unused space. Must be ≥ the length of `bytes`. |
| `pool` | string | Optional. `"branch"` (default) places the region within ±2 GB of `WHGame.dll`'s code so a `rel32` branch can reach it; `"local"` places it anywhere (use when ±2 GB reachability is not required). |
| `export` | string | Optional. Bare symbol name to publish the allocated address under; the engine stamps your `<author>.<plugin>` prefix (from `[plugin].author` + `[plugin].name`) automatically — you never type it. A later `kcdx.hook{ target_symbol = ... }` or `kcdx.bytes{ target_symbol = ... }` resolves to it: bare from your own plugin (self), prefixed `<author>.<plugin>.<name>` from anywhere else. |

You must declare `bytes` or `size` (or both).

## Returns / Errors

A live `kcdx.memory.pointer` userdata to the region. Returns `(nil, err)` when:
the argument is not a table; `name` is missing; `bytes` fails to parse; `size`
is not a positive integer or is smaller than `bytes`; `pool` is not
`"branch"`/`"local"`; the pool cannot allocate (out of space, or no
`rel32`-reachable region for `"branch"`); or `export` collides with a symbol
another plugin already registered (the error names the prior owner — the region
is still allocated, but is unreachable by that symbol name).

## Minimal snippet

```lua
-- reserve a 256-byte region, publish it, write a RET into it.
-- In a plugin with [plugin].author = "walkabout", [plugin].name = "violetanvil":
local region = kcdx.code{
    name   = "outfit_gate_logic",
    size   = 256,
    pool   = "branch",
    export = "outfit_gate_logic",                -- bare; engine stamps the prefix
}
region:set_byte(0xC3)   -- write a RET into the base (the region is live now)
-- elsewhere (another plugin):
--   kcdx.hook{ target_symbol = "walkabout.violetanvil.outfit_gate_logic", ... }
-- in this same plugin a bare reference also works:
--   kcdx.hook{ target_symbol = "outfit_gate_logic", ... }
```

The returned pointer is a live hook target right away — you can mid-hook *into*
the allocated code by passing `region:add(N)` (a [`kcdx.memory.pointer`](memory.md))
as the [`kcdx.hook`](hook.md) `address` locator. `pool = "branch"` is required
for that: a `mid` hook writes a `rel32` jmp into the region, which must sit
within ±2 GB of `WHGame.dll` for the branch to reach. (The `cap-04-midhook`
test plugin composes exactly this — allocate via `kcdx.code`, mid-hook the
allocation, call it to observe the hook took effect.)
