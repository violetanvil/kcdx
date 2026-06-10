# kcdxDllInterface (↔ kcdx.dll)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.dll.declare`](../lua/dll.md) — declare your plugin
DLL's functions (signatures copied from your own source, no disassembly) so other
plugins can hook them by name through
[`kcdx.functions["<your-namespace>"].*`](functions.md).

Fetched via `api->QueryInterface(kcdxInterface_Dll, kcdxDllInterface_Version)`
(or as `K.dll` after `K.Init(...)`).

Like Lua's `kcdx.dll.declare`, you declare a function with the signature from
your own source — the engine cannot read an ABI from a compiled DLL, and you have
the types for free because you wrote the function. The declared functions land in
the SAME store the Lua `kcdx.dll.declare` binder writes, so a function you
declare here resolves identically through `PluginByName` (C++) and
`kcdx.functions["<ns>"]` (Lua) — one store, both surfaces.

## Call shape

```cpp
struct kcdxDeclaredFn {
    const char* name;        // the bare function name (required, non-empty)
    const char* signature;   // the function's ABI, from your source (e.g. "bool (ptr self)")
};

struct kcdxDllInterface {
    // Declare `count` functions under the plugin's <author>.<plugin> namespace.
    bool (*Declare)(const char* pluginNamespace,
                    const kcdxDeclaredFn* fns, int count);
};
```

The mirror is one-to-one with the Lua form ([`kcdx.dll.declare`](../lua/dll.md)):
the Lua `function_map` (`{ FnName = { signature = "…" } }`) maps to the
`kcdxDeclaredFn[]` array, and each declared function lands under
[`kcdx.functions["<pluginNamespace>"].<name>`](functions.md) exactly as the Lua
call's do.

- **Arguments.** `pluginNamespace` is your `<author>.<plugin>` string (e.g.
  `"redmoon.outfit_mod"`); `fns` is an array of `count` `kcdxDeclaredFn` rows.
  Every entry requires a non-null, non-empty `name` **and** `signature` — a
  `signature` is required for the same reason as Lua: a callback hook needs the
  ABI, which the compiled DLL does not carry. The signatures are copied at
  `Declare` time, so you need not retain the array or its strings afterward.
- **Return.** `true` when every entry was accepted.
- **Error behaviour.** A malformed entry (null/empty `name` or `signature`, an
  empty `pluginNamespace`, a null array, or `count <= 0`) is rejected with a
  logged teaching diagnostic (category `DLL_DECLARE` in the dev log — the C++
  peer of the Lua call's raised error) and `Declare` returns `false`. The whole
  batch is validated before any of it is written — a malformed entry rejects the
  whole `Declare` (no partial accept), never a silent drop of one declared
  function while the others land.

`Declare` is launch-time — call it from `kcdxPlugin_Load`, the same phase the Lua
`kcdx.dll.declare` runs from.

## Minimal snippet

```cpp
#include "kcdx/Kcdx.h"
static Kcdx K;

bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "redmoon", "outfit_mod")) return true;  // logs why

    const kcdxDeclaredFn fns[] = {
        { "CanSwapInCombat", "bool (ptr self)" },
        { "OnOutfitSwap",    "void (ptr self, i32 outfit_id)" },
    };
    if (!K.dll->Declare("redmoon.outfit_mod", fns, 2)) {
        // a malformed entry — the teaching reason is in the dev log (DLL_DECLARE)
        return true;
    }
    // Now any plugin (Lua or C++) resolves them by name:
    //   K.functions->PluginByName("redmoon.outfit_mod", "CanSwapInCombat")
    //   kcdx.functions["redmoon.outfit_mod"].CanSwapInCombat
    return true;
}
```

---

See also: [functions.md](functions.md) (the C++ mirror of the namespace declared
functions land in), [`../lua/dll.md`](../lua/dll.md) (the Lua surface).
