# kcdxFunctionsInterface (↔ kcdx.functions)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.functions.*`](../lua/functions.md) — function
references that name a function (a game-engine function or another plugin's) and
carry its address and, where known, its verified signature, plus a `Resolve`
introspection accessor.

**Not yet implemented (NYI).** There is no functions interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not link
against it. `kcdxFunctionsInterface` is the **planned** mirror name; it is tracked
parity debt — both docs map a capability even when only one is built, discharged
when the C++ parity phase ships it and it is verified callable. This entry maps
the planned shape so both surfaces describe the capability while the engine
catches up.

Like Lua's `kcdx.functions`, the two populations are distinguished by stem: a
game-engine function (resolved from the reference database, carrying address +
verified signature) and a plugin function (declared via the C++ mirror of
[`kcdx.dll.declare`](../lua/dll.md), carrying the author-declared signature). The
author never writes an address or an ABI — the name is the whole input.

## Planned mirror shape (NYI)

Following the C++ surface model (a `QueryInterface`-fetched interface that mints
values), the planned mirror returns a `kcdxFunctionRef` for a name (or stable id)
and exposes a `Resolve` accessor returning the same fields the Lua `:resolve()`
table carries.

```cpp
// PLANNED — not in Interfaces.h yet.

// A function reference (the C++ peer of a kcdx.functions.* value).
struct kcdxFunctionRef;  // opaque handle minted by the calls below

struct kcdxFunctionResolution {
    bool        found;
    bool        isGame;       // game-engine (database) vs plugin (declared) reference
    const char* stem;         // "WHGame" or "<author>.<plugin>"
    const char* name;         // the bare function name
    const char* signature;    // verified ABI (game) / declared ABI (plugin); "" when none
    bool        hasAddress;
    void*       address;      // the resolved address; null when !hasAddress
    const char* reason;       // set when !found (name_unknown / db_not_loaded / not_declared)
};

struct kcdxFunctionsInterface {
    // A game-engine function by name (dot-free stem) or by stable id.
    kcdxFunctionRef* (*GameByName)(const char* stem, const char* name);
    kcdxFunctionRef* (*GameById)(unsigned long long kcdxId);
    // A plugin function by its <author>.<plugin> stem + name.
    kcdxFunctionRef* (*PluginByName)(const char* pluginNamespace, const char* name);

    // Inspect what a reference points at.
    kcdxFunctionResolution (*Resolve)(kcdxFunctionRef* ref);
};
```

The mirror is one-to-one with the Lua forms ([`kcdx.functions.*`](../lua/functions.md)):
`kcdx.functions.WHGame.SaveGame` maps to `GameByName("WHGame", "SaveGame")`,
`kcdx.functions.by_id[N]` to `GameById(N)`, and
`kcdx.functions["a.b"].Fn` to `PluginByName("a.b", "Fn")`. The Lua
`value:resolve()` table maps to `kcdxFunctionResolution`. A game reference
resolves to an address + signature; a plugin reference resolves to its declared
signature, and its address is filled from the owning plugin's shipped `/DEBUG:FULL`
PDB at plugin load (the same
[internal-function-from-PDB](../lua/functions.md#internal-functions-from-a-shipped-pdb)
mechanism the Lua side describes — it is driven engine-side at plugin load, not a
C++ author call), with `hasAddress = false` until an address is available — the
same contract as the Lua side.

---

See also: [dll.md](dll.md) (the C++ mirror of `kcdx.dll.declare`),
[`../lua/functions.md`](../lua/functions.md) (the built Lua surface).
