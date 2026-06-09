# kcdxLocatorInterface (↔ kcdx.locator)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.locator.*`](../lua/locator.md) — locator values
that say *where in a function* a hook or statement op applies, plus a
`Resolve(module, target)` introspection accessor that resolves a locator against
a named curated function.

**Not yet implemented (NYI).** There is no locator interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. `kcdxLocatorInterface` is the **planned** mirror name; it is
tracked parity debt — both docs map a capability even when only one is built,
discharged when the C++ parity phase ships it and it is verified callable. This
entry maps the planned shape so both surfaces describe the capability while the
engine catches up.

Like Lua's `kcdx.locator`, the common-path forms name what the author already
understands (a call to a function, a return, a matching statement); the raw-AOB
`MatchingPattern` form is the **labelled expert hatch**, not the common path.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options/factory, doing → typed
params), the planned mirror is a `QueryInterface`-fetched interface that mints a
locator value and exposes a `Resolve` accessor returning the same fields the Lua
`:resolve` table carries.

```cpp
// PLANNED — not in Interfaces.h yet.

// A locator descriptor (the C++ peer of a kcdx.locator.* value).
struct kcdxLocator;  // opaque handle minted by the factory calls below

struct kcdxStatementResolution {
    bool        found;
    long long   statementIdx;     // valid when found
    const char* kind;             // "call" / "return" / "branch" / "assign" / …
    bool        hasByteRangeLen;
    long long   byteRangeLen;
    const char* callee;           // "" when not a call
    const char* stringRef;        // "" when none
    // captures: count + array of { name, storageKind, storageDetail, dataType,
    //                              hasSizeBytes, sizeBytes }
    int                         captureCount;
    const kcdxStatementCapture* captures;
    const char* reason;           // set when !found (e.g. "call_to_ambiguous",
                                  //   "matching_pattern_not_statement_locator")
};

struct kcdxLocatorInterface {
    // Function-level
    kcdxLocator* (*FunctionEntry)();
    kcdxLocator* (*FunctionExit)();
    // Statement-content shortcuts (the common path)
    kcdxLocator* (*FirstCallTo)(const char* fn);
    kcdxLocator* (*LastCallTo)(const char* fn);
    kcdxLocator* (*CallTo)(const char* fn);          // errors-if-multiple
    kcdxLocator* (*FirstReturn)();
    kcdxLocator* (*LastReturn)();
    kcdxLocator* (*ReturnValue)(const char* operand);
    kcdxLocator* (*ReferencesString)(const char* s);
    kcdxLocator* (*FirstReadOfCvar)(const char* name);
    // General matcher (any subset of the keys; ANDed)
    kcdxLocator* (*Matching)(const kcdxLocatorMatch* keys);
    // Labelled expert raw-AOB hatch (NOT a statement-metadata locator)
    kcdxLocator* (*MatchingPattern)(const char* aob);

    // Inspect what a locator resolves to within a named curated function.
    kcdxStatementResolution (*Resolve)(kcdxLocator* loc,
                                       const char* module,
                                       const char* target);
};
```

The mirror is one-to-one with the Lua forms ([`kcdx.locator.*`](../lua/locator.md)):
each `kcdx.locator.<form>(...)` maps to the same-named factory call, and the Lua
`value:resolve(module, target)` table maps to `kcdxStatementResolution`. The
`MatchingPattern` form resolves through `Resolve` to `found = false` with
`reason == "matching_pattern_not_statement_locator"`, the same contract as the
Lua side — the AOB resolves against the binary's bytes elsewhere, not against
statement metadata.
