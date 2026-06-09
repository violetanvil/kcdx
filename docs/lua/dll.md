# kcdx.dll
> Part of the [kcdx Lua API](index.md).

Declare your plugin DLL's functions so another plugin (or your own Lua) can hook
them **by name**, with no disassembly. You copy the signatures straight from your
own source — you wrote the DLL, you have the types — and the engine exposes the
functions under [`kcdx.functions["<your-namespace>"].*`](functions.md).

This is the cornerstone answer to "expose my plugin's internals for other mods to
extend": the author who owns the source declares the function once; every
consumer reaches it by name and never touches a disassembler.

## `kcdx.dll.declare(plugin_namespace, function_map)`

Declare one or more of your DLL's functions.

```lua
-- redmoon-outfit-mod/plugin.lua
kcdx.dll.declare("redmoon.outfit_mod", {
    CanSwapInCombat = { signature = "bool (ptr self)" },
    OnOutfitSwap    = { signature = "void (ptr self, i32 outfit_id)" },
})

-- Now any plugin can reach them by name:
local ref = kcdx.functions["redmoon.outfit_mod"].CanSwapInCombat
```

### Arguments

| Arg | Type | Meaning |
|---|---|---|
| `plugin_namespace` | string | **Required.** Your plugin's `<author>.<plugin>` namespace (e.g. `"redmoon.outfit_mod"`) — the dotted stem the declared functions land under in [`kcdx.functions`](functions.md). You pass the full qualified form here (this is a cross-plugin export surface — the namespace is stated, not derived). |
| `function_map` | table | **Required.** A map of `FnName = { signature = "<abi>" }`. Each key is a bare function name; each value is a table carrying the function's `signature`. |

### The signature is required — and it comes from your source

A `signature` is required on every entry because a **callback hook** on the
function needs its ABI (which register holds which argument, the return type),
and the engine cannot read an ABI from a compiled DLL — compiled C++ carries no
runtime-queryable signature. You have it for free: you wrote the function, so you
copy its signature from your own source. No Ghidra, no disassembly.

The signature uses the same ABI string the rest of kcdx uses (e.g.
`"bool (ptr self)"`, `"void (ptr self, i32 outfit_id)"`).

### Returns / Errors

Returns `true` on success. A malformed call **raises a teaching Lua error at the
call site** (an author bug surfaced where you wrote it) — never a silent drop of
a declared function:

| Error | Cause |
|---|---|
| `plugin_namespace` required | arg 1 missing / not a string / empty. |
| `function_map` required | arg 2 missing / not a table. |
| every key must be a function NAME | a non-string key in `function_map`. |
| function `X` must map to a table | a `function_map` value is not a `{ signature = … }` table. |
| function `X` is missing a `signature` | an entry's table has no `signature` string. |
| function `X` has an empty `signature` | an entry's `signature` is the empty string. |

### What enters the namespace (this step) and what is added later

A declared function resolves immediately to its **signature** — the one
irreducible thing a callback hook needs. Its **address** is filled by a separate
mechanism for plugin DLLs (reading the DLL's own symbols); until then a declared
function's [`:resolve()`](functions.md) reports `has_address = false`, which is
not a failure — a callback hook depends on the signature, which the declaration
supplies.

### Minimal snippet

```lua
-- Declare a function from your own DLL so other mods can hook it by name.
kcdx.dll.declare("redmoon.outfit_mod", {
    CanSwapInCombat = { signature = "bool (ptr self)" },
})

-- Confirm it landed.
local ref = kcdx.functions["redmoon.outfit_mod"].CanSwapInCombat
local r = ref:resolve()
-- r.found == true, r.signature == "bool (ptr self)", r.is_game == false.
```

## Glossary

- **declared function** — a plugin function exposed via `kcdx.dll.declare`, owned
  by the declaring plugin's `<author>.<plugin>` namespace, reachable by name at
  [`kcdx.functions["<author>.<plugin>"].<name>`](functions.md). The author
  supplies the signature from their own source; the consumer hooks it by name
  with no disassembly.
- **plugin namespace** — the `<author>.<plugin>` string a declaration registers
  under. You pass it explicitly to `kcdx.dll.declare` (it is a cross-plugin export
  surface, so the qualified form is stated).

---

See also: [functions.md](functions.md) (the `kcdx.functions.*` namespace declared
functions land in), [declare.md](declare.md) (the distinct `kcdx.declare` —
declaring a per-version *game-binary target* by AOB pattern, a different surface
from declaring your own DLL's functions), [`../cpp/dll.md`](../cpp/dll.md) (the
C++ mirror), and [index.md](index.md) for the surface map.
