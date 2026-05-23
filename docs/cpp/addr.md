# Address Library resolution (↔ kcdx.addr)
> Part of the [kcdx C++ API](index.md).

Resolve a known game address by Address Library ID or name, and resolve
cross-plugin symbols. **Built** — `kcdxInterface::ResolveAddress`,
`ResolveAddressByName`, and `ResolveSymbol`, all on the root interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) (no
`QueryInterface` needed).

This is the C++ spelling of Lua's `kcdx.addr` snapshot table. Where Lua exposes
a `pairs`-iterable table of name → pointer-userdata, C++ exposes resolver
functions on the root interface.

## Call shapes

```cpp
uintptr_t (*ResolveAddress)      (uint64_t id);        // by numeric Address Library ID
uintptr_t (*ResolveAddressByName)(const char* name);   // by human-readable name (e.g. "lua_pcall")
uintptr_t (*ResolveSymbol)       (const char* name);   // by cross-plugin exported symbol
```

| Method | Arg | Returns |
|---|---|---|
| `ResolveAddress` | `uint64_t id` — numeric Address Library ID (the stable cross-version reference). | Absolute VA, or `0` if the ID is unknown for the running game version or marked `removed`. |
| `ResolveAddressByName` | `const char* name` — the readable label (e.g. `"lua_pcall"`). Same resolution rules as `ResolveAddress`. The C++ mirror of the Lua `address_id = "name"` locator. | Absolute VA, or `0`. |
| `ResolveSymbol` | `const char* name` — a symbol published by a `[[trampoline]]`/`[[hook]]` TOML `export = "..."`. | The registered address (trampoline base, hook call-original entry, …), or `0` if unregistered. The caller owns knowing the ABI of the resolved address. |

`ResolveAddressByName` was appended after `ResolveAddress` (the header's
APPEND-ONLY marker — see [cross-cutting.md](cross-cutting.md)); numeric IDs
remain the stable cross-version reference, names are the friendlier form.

`ResolveSymbol` is constant-time and safe to call from any plugin context after
`kcdxMessage_InputLoaded` (the symbol table is populated during apply, which
runs before that message).

## Minimal snippet

```cpp
uintptr_t pcall = api->ResolveAddressByName("lua_pcall");
if (!pcall) { gLog.Warn("ADDR", "lua_pcall not named on this build"); }

uintptr_t tramp = api->ResolveSymbol("violetanvil.outfit_gate_logic");
```

This is the C++ mirror of [kcdx.addr](../lua/addr.md).
