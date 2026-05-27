# Author-declared targets (↔ targets.toml)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [author-declared targets](../lua/targets.md) — a plugin
names a code site itself (a bare `name` + one locator + optional `signature`),
the engine stamps the `<author>.<plugin>` prefix (from `[plugin].author` +
`[plugin].name`), and the site is then hookable / patchable **by name**
(self > engine > other precedence), shareable across plugins without anyone
re-deriving the hex — one expert names a site once, and every other author
consumes it by name without ever touching the hex.

**Not yet implemented (NYI).** There is no author-target registration interface
in [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. This is tracked parity debt — both docs map a capability even
when only one is built, discharged when the C++ parity phase ships it and it is
verified callable. This entry maps the planned shape so both surfaces describe
the capability while the engine catches up.

The Lua surface has two halves; the C++ mirror owes both:

1. **Declaring a target.** Lua reads a `targets.toml` sidecar; the C++ author
   declares targets through a registration call on a planned interface (the C++
   spelling of the sidecar — same model, declared in code instead of TOML).
2. **Referring to one by name.** The built [`kcdxHookInterface`](hook.md) and the
   NYI locator-based byte-rewrite mirror ([bytes.md](bytes.md)) take a
   `target = "<name>"` locator that resolves an author target exactly as Lua's
   `kcdx.hook{ target }` / `kcdx.bytes{ target }` do.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options-struct, doing → typed
params):

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
enum kcdxAuthorLocatorKind {
    kcdxAuthorLocator_Pattern,       // AOB string (expert hatch) — carry a signature
    kcdxAuthorLocator_Rva,           // raw module RVA          — carry a signature
    kcdxAuthorLocator_AddressId,     // Address Library id
    kcdxAuthorLocator_TargetSymbol,  // another known target name
};

struct kcdxAuthorTargetOptions {
    const char*              name;        // required — bare name; engine stamps the <author>.<plugin> prefix
    kcdxAuthorLocatorKind    kind;        // which locator below is meaningful
    const char*              locatorStr;  // pattern / target_symbol
    uint64_t                 locatorNum;  // rva / address_id
    const char*              signature;   // optional ABI (required in practice for pattern/rva)
};

// kcdxTargetInterface::RegisterTarget(owner, const kcdxAuthorTargetOptions&) -> bool
//   false + an engine log line (category "TARGETS") on a bad row / invalid plugin name.
```

The resolution side (`target = "<name>"`) is the same string-locator field the
planned `kcdxHookInterface` / byte-rewrite mirror carry — see those entries.
A name that resolves to a `pattern`/`rva` target carries its declared ABI, so a
named pattern site needs no separate signature at the hook call (parity with Lua).

## Related built call — `ResolveAddressByName`

The address half of by-name resolution is **already built**:
`kcdxInterface::ResolveAddressByName(const char* name)` resolves an engine
Address Library name to a runtime VA ([addr.md](addr.md)). What is NYI on the
C++ side is (a) registering an *author* target so the name table also knows
*your* sites, and (b) a name-resolves-address-**and**-ABI overload for hooks —
the planned `ResolveAddressByNameAs(handle, name)` form that resolves with the
calling plugin's namespace for self > engine > other precedence (tracked in
[planned.md](planned.md)).

This is the C++ mirror of [author-declared targets](../lua/targets.md).
