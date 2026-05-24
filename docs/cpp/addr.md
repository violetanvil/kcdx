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

// Identity-carrying forms — pass YOUR OWN plugin handle as `owner`.
uintptr_t (*ResolveAddressByNameAs)(kcdxPluginHandle owner, const char* name);
uintptr_t (*ResolveSymbolAs)       (kcdxPluginHandle owner, const char* name);
```

| Method | Arg | Returns |
|---|---|---|
| `ResolveAddress` | `uint64_t id` — numeric Address Library ID (the stable cross-version reference). | Absolute VA, or `0` if the ID is unknown for the running game version or marked `removed`. |
| `ResolveAddressByName` | `const char* name` — the readable label (e.g. `"lua_pcall"`). Resolves **engine-seed + explicit-prefix only** — no `self` tier (the anonymous thunk carries no per-call identity). The C++ mirror of the Lua `address_id = "name"` locator. | Absolute VA, or `0`. |
| `ResolveSymbol` | `const char* name` — a symbol published by a `[[trampoline]]`/`[[hook]]` TOML `export = "..."` or `kcdx.code{ export = }`. | The registered address (trampoline base, hook call-original entry, …), or `0` if unregistered. The caller owns knowing the ABI of the resolved address. |

`ResolveAddressByName` was appended after `ResolveAddress` (the header's
APPEND-ONLY marker — see [cross-cutting.md](cross-cutting.md)); numeric IDs
remain the stable cross-version reference, names are the friendlier form.

`ResolveSymbol` is constant-time and safe to call from any plugin context after
`kcdxMessage_InputLoaded` (the symbol table is populated during apply, which
runs before that message).

## `ResolveAddressByNameAs(owner, name)`

The identity-carrying form of `ResolveAddressByName` — it resolves an
author-target / Address Library name with the **caller's own plugin as the
`self` tier**, so resolution is full `self > engine > other` precedence
(`naming-namespaces.md`). This is the C++ mirror of the Lua
`kcdx.hook{ target = "<name>" }` path, which threads the calling plugin's
identity natively.

```cpp
uintptr_t (*ResolveAddressByNameAs)(kcdxPluginHandle owner, const char* name);
```

| Arg | Meaning |
|---|---|
| `kcdxPluginHandle owner` | The CALLING plugin's own handle (the `self` tier). Pass `kcdxInvalidPluginHandle` (`0`) to resolve **anonymously** — no `self` tier, identical to `ResolveAddressByName(name)` (engine-seed + explicit-prefix only). |
| `const char* name` | A BARE name (resolved `self > engine > other`) OR an explicit `"<author>.<plugin>.<name>"` (resolved directly, unambiguous from anywhere). Engine seed names live under the reserved `kcdx` author at the 1-dot `<kcdx>.<seedname>` form. |

**Returns** the absolute VA, or `0` if the name resolves to nothing on this
build. Same error behavior as `ResolveAddressByName` — a `null` name or an
unresolved name yields `0`; an unknown/invalid `owner` degrades to the anonymous
(engine-seed-only) path, never a mis-attribution to the wrong owner.

```cpp
// A plugin resolving a name it (or another plugin) declared, with its own
// namespace as the self tier:
uintptr_t va = api->ResolveAddressByNameAs(K.self, "outfit_gate");
if (!va) { K.log.Warn("ADDR", "outfit_gate unresolved on this build"); }
```

> **Single-surface (`docs-discipline.md` §3): no Lua `*As` mirror, by design.**
> Lua threads the caller's identity natively — the Lua runtime knows which
> plugin is calling — so there is no Lua `*As` form. The `As` form exists ONLY
> because the single shared C++ `kcdxInterface` (`g_api`) is handed by-pointer to
> every plugin and so has no per-call identity for the anonymous
> `ResolveAddressByName` to read. This is a sanctioned single-surface marker, NOT
> a NYI debt. See [naming-namespaces.md](../../.claude/rules/naming-namespaces.md).

## `ResolveSymbolAs(owner, name)`

The identity-carrying form of `ResolveSymbol` — it resolves a cross-plugin
exported symbol (a `kcdx.code{ export = }` / `[[trampoline]]` export) with the
**caller's own plugin as the `self` tier**, so a bare symbol name resolves
`self > other` (there is no engine-seed tier for symbols — only plugins publish
symbols). The C++ counterpart to consuming a `kcdx.code{ export }` symbol by
name.

```cpp
uintptr_t (*ResolveSymbolAs)(kcdxPluginHandle owner, const char* name);
```

| Arg | Meaning |
|---|---|
| `kcdxPluginHandle owner` | The CALLING plugin's own handle (the `self` tier). Pass `kcdxInvalidPluginHandle` (`0`) to resolve **anonymously** — no `self` tier, identical to `ResolveSymbol(name)`. |
| `const char* name` | A BARE symbol name (resolved `self > other`) OR an explicit `"<author>.<plugin>.<name>"` (resolved directly). |

**Returns** the registered address, or `0` if the symbol is unregistered. Same
error behavior as `ResolveSymbol` — a `null` or unknown `name` yields `0`; an
unknown/invalid `owner` degrades to the anonymous path, never a mis-attribution.
The caller owns knowing the ABI of the resolved address.

```cpp
// Resolve a symbol another plugin published, with the caller's own space first:
uintptr_t tramp = api->ResolveSymbolAs(K.self, "outfit_gate_logic");
```

> **Single-surface (`docs-discipline.md` §3): no Lua `*As` mirror, by design.**
> Lua threads the caller's identity natively (the Lua runtime knows which plugin
> is calling), so there is no Lua `*As` form. The `As` form exists ONLY because
> the single shared C++ `kcdxInterface` (`g_api`) has no per-call identity for
> the anonymous `ResolveSymbol` to read. Sanctioned single-surface marker, NOT a
> NYI debt. See [naming-namespaces.md](../../.claude/rules/naming-namespaces.md).

## Minimal snippet

```cpp
uintptr_t pcall = api->ResolveAddressByName("lua_pcall");
if (!pcall) { gLog.Warn("ADDR", "lua_pcall not named on this build"); }

// Resolve another plugin's exported symbol by its full <author>.<plugin>.<name>:
uintptr_t tramp = api->ResolveSymbol("walkabout.violetanvil.outfit_gate_logic");
```

This is the C++ mirror of [kcdx.addr](../lua/addr.md).
