# kcdxDllInterface (↔ kcdx.dll)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.dll.declare`](../lua/dll.md) — declare your plugin
DLL's functions (signatures copied from your own source, no disassembly) so other
plugins can hook them by name through [`kcdx.functions["<your-namespace>"].*`](functions.md).

**Not yet implemented (NYI).** There is no DLL-declare interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not link
against it. `kcdxDllInterface` is the **planned** mirror name; it is tracked
parity debt — both docs map a capability even when only one is built, discharged
when the C++ parity phase ships it and it is verified callable. This entry maps
the planned shape so both surfaces describe the capability while the engine
catches up.

Like Lua's `kcdx.dll.declare`, you declare a function with the signature from
your own source — the engine cannot read an ABI from a compiled DLL, and you have
the types for free because you wrote the function.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → an array of typed entries), the
planned mirror takes the plugin namespace plus an array of name+signature
entries.

```cpp
// PLANNED — not in Interfaces.h yet.

struct kcdxDeclaredFn {
    const char* name;        // the bare function name
    const char* signature;   // the function's ABI, from your source (e.g. "bool (ptr self)")
};

struct kcdxDllInterface {
    // Declare `count` functions under the plugin's <author>.<plugin> namespace.
    // Returns true on success; a malformed entry is rejected with a logged
    // teaching diagnostic (the C++ peer of the Lua call's raised error).
    bool (*Declare)(const char* pluginNamespace,
                    const kcdxDeclaredFn* fns, int count);
};
```

The mirror is one-to-one with the Lua form ([`kcdx.dll.declare`](../lua/dll.md)):
the Lua `function_map` (`{ FnName = { signature = "…" } }`) maps to the
`kcdxDeclaredFn[]` array, and each declared function lands under
[`kcdx.functions["<pluginNamespace>"].<name>`](functions.md) exactly as the Lua
call's do. A `signature` is required on every entry for the same reason as Lua —
a callback hook needs the ABI, which the compiled DLL does not carry.

---

See also: [functions.md](functions.md) (the C++ mirror of the namespace declared
functions land in), [`../lua/dll.md`](../lua/dll.md) (the built Lua surface).
