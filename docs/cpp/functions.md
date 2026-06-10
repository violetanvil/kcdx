# kcdxFunctionsInterface (↔ kcdx.functions)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.functions.*`](../lua/functions.md) — function
references that name a function (a game-engine function or another plugin's) and
carry its address and, where known, its verified signature.

Fetched via `api->QueryInterface(kcdxInterface_Functions,
kcdxFunctionsInterface_Version)` (or as `K.functions` after `K.Init(...)`).

Like Lua's `kcdx.functions`, the two populations are distinguished by stem: a
game-engine function (resolved from the reference database, carrying address +
verified signature) and a plugin function (declared via the C++ mirror of
[`kcdx.dll.declare`](dll.md), carrying the author-declared signature). The
author never writes an address or an ABI — the name is the whole input.

## The reference IS the resolution — read the fields directly

Each mint returns a `kcdxFunctionRef` **by value**. The reference carries its
own resolution in its fields — there is no separate `Resolve()` call, no opaque
handle, no two-call dance. Read what you need off the value:

```cpp
struct kcdxFunctionRef {
    bool        found;       // true on a resolvable reference; false + `reason` on a miss
    bool        isGame;      // game-engine (database) vs plugin (declared) reference
    const char* stem;        // "WHGame" / "<author>.<plugin>" / "by_id"
    const char* name;        // the bare function name ("" for a GameById reference)
    void*       address;     // the resolved address; null when !hasAddress (a raw void*, no rounding)
    bool        hasAddress;  // true when `address` is a real resolved VA
    const char* signature;   // verified ABI (game) / declared ABI (plugin); "" when none
    const char* reason;      // set when !found (name_unknown / db_not_loaded / not_declared)
};
```

The `const char*` fields point at engine-owned, process-lifetime strings — do
**not** free them; they outlive the call. The struct is returned by value, so
copy it freely (the pointers stay valid). `address` is a raw `void*`: the C++
side has no `LUA_NUMBER=float` precision hazard, so a pointer-magnitude VA is
exact (unlike the Lua side, which returns a `kcdx.memory.pointer` userdata).

## Call shape

```cpp
struct kcdxFunctionsInterface {
    // A game-engine function by name (dot-free stem) or by stable id.
    kcdxFunctionRef (*GameByName)(const char* stem, const char* name);
    kcdxFunctionRef (*GameById)(unsigned long long kcdxId);
    // A plugin function by its <author>.<plugin> stem + name.
    kcdxFunctionRef (*PluginByName)(const char* pluginNamespace, const char* name);
};
```

The mints are one-to-one with the Lua forms
([`kcdx.functions.*`](../lua/functions.md)):
`kcdx.functions.WHGame.SaveGame` ↔ `GameByName("WHGame", "SaveGame")`,
`kcdx.functions.by_id[N]` ↔ `GameById(N)`, and
`kcdx.functions["a.b"].Fn` ↔ `PluginByName("a.b", "Fn")`. The Lua
`value:resolve()` table is the C++ reference's fields.

- **Arguments.** `stem` / `pluginNamespace` and `name` are plain `const char*`;
  `kcdxId` is the stable game id (`unsigned long long`). The mints copy nothing
  from you — they resolve and return a fresh value.
- **Return.** A `kcdxFunctionRef` by value, always — even a miss returns a
  well-formed value with `found=false` and a `reason` token (never a silent
  empty).
- **Error behaviour.** A miss is `found=false` + a `reason`: `name_unknown` (the
  game name/id is not in the reference database), `db_not_loaded` (the reference
  database is not open — a pre-deploy state), or `not_declared` (the plugin
  function was never declared and has no PDB-sourced address). The miss is the
  fail-loud value, never a thrown error and never a null return.

A game reference resolves to an address + signature. A plugin reference resolves
to its declared signature; its address is filled from the owning plugin's
shipped `/DEBUG:FULL` PDB at plugin load (the same
[internal-function-from-PDB](../lua/functions.md#internal-functions-from-a-shipped-pdb)
mechanism the Lua side describes — driven engine-side at plugin load, not a C++
author call), with `hasAddress = false` until an address is available. A declared
plugin function carrying its signature with `hasAddress=false` is correct, not a
gap: a callback hook needs the signature, a static byte op needs the address.

## Minimal snippet

```cpp
#include "kcdx/Kcdx.h"
static Kcdx K;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "redmoon", "outfit")) return true;  // logs why

    // A game function by name — address AND verified ABI for free.
    kcdxFunctionRef save = K.functions->GameByName("WHGame", "SaveGame");
    if (save.found && save.hasAddress) {
        K.log.Info("MOD", "SaveGame at %p, ABI %s", save.address, save.signature);
    }

    // The same function by its stable id.
    kcdxFunctionRef byId = K.functions->GameById(144);

    // Another plugin's declared function (resolves to its declared signature).
    kcdxFunctionRef fn = K.functions->PluginByName("redmoon.outfit", "CanSwapInCombat");
    if (!fn.found) K.log.Warn("MOD", "not resolvable yet: %s", fn.reason);
    return true;
}
```

---

See also: [dll.md](dll.md) (the C++ mirror of `kcdx.dll.declare` — declare your
own DLL's functions so others resolve them here),
[`../lua/functions.md`](../lua/functions.md) (the Lua surface).
