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
| `export` | string | Optional. Publishes the allocated address under this symbol name; a later `kcdx.hook{ target_symbol = ... }` or `kcdx.bytes{ target_symbol = ... }` (in this or any plugin) resolves to it. |

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
-- reserve a 256-byte region, publish it, write a RET into it:
local region = kcdx.code{
    name   = "outfit_gate_logic",
    size   = 256,
    pool   = "branch",
    export = "violetanvil.outfit_gate_logic",
}
region:set_byte(0xC3)   -- write a RET into the base (the region is live now)
-- elsewhere: kcdx.hook{ target_symbol = "violetanvil.outfit_gate_logic", ... }
```
