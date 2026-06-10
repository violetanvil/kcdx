# kcdx locator values (↔ kcdx.locator)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.locator.*`](../lua/locator.md) — locator values
that say *where in a function* a hook or statement op applies.

**The locator VALUE is built.** `kcdxLocator` is a small author-filled **value
struct** in [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) — a
`kcdxLocatorKind` tag plus operand fields (null = key not provided) — consumed
by [`kcdxStatementInterface`](statement.md) (the `InsertBefore`/`InsertAfter`
locator param and the `ReplaceWith` opts locator). There is no factory interface
and no opaque handle: the author fills the struct directly, the C++ spelling of
calling a `kcdx.locator.<form>(...)` constructor. The kind tags map one-to-one
to the Lua constructors:

| Lua constructor | `kcdxLocator.kind` | operand field(s) |
|---|---|---|
| `kcdx.locator.function_entry()` | `kcdxLocator_FunctionEntry` | — |
| `kcdx.locator.function_exit()` | `kcdxLocator_FunctionExit` | — |
| `kcdx.locator.first_call_to(fn)` | `kcdxLocator_FirstCallTo` | `calleeOrFn` |
| `kcdx.locator.last_call_to(fn)` | `kcdxLocator_LastCallTo` | `calleeOrFn` |
| `kcdx.locator.call_to(fn)` (errors-if-multiple) | `kcdxLocator_CallTo` | `calleeOrFn` |
| `kcdx.locator.first_return()` | `kcdxLocator_FirstReturn` | — |
| `kcdx.locator.last_return()` | `kcdxLocator_LastReturn` | — |
| `kcdx.locator.return_value(operand)` | `kcdxLocator_ReturnValue` | `returnValueOperand` |
| `kcdx.locator.references_string(s)` | `kcdxLocator_ReferencesString` | `stringArg` |
| `kcdx.locator.first_read_of_cvar(name)` | `kcdxLocator_FirstReadOfCvar` | `stringArg` |
| `kcdx.locator.matching{...}` (keys ANDed) | `kcdxLocator_Matching` | `matchKind` / `matchCallee` / `matchConditionContains` / `matchReadsCvar` / `matchReferencesString` |
| `kcdx.locator.matching_pattern(aob)` | `kcdxLocator_MatchingPattern` | `aobPattern` |

Like Lua's `kcdx.locator`, the common-path forms name what the author already
understands (a call to a function, a return, a matching statement); the raw-AOB
`kcdxLocator_MatchingPattern` form is the **labelled expert hatch**, not the
common path. The full as-built usage is documented with the consuming surface:
[`statement.md`](statement.md).

## The introspection accessor — not yet implemented (NYI)

The Lua `value:resolve(module, target)` introspection accessor (resolve a
locator against a named curated function and inspect the statement it lands on)
has no C++ peer yet — tracked parity debt, discharged when the introspection
mirror ships and is verified callable. Its result will carry the same fields the
Lua `:resolve` table does:

```cpp
// PLANNED result shape — the accessor itself is not in Interfaces.h yet, and
// where it lives is settled when it is built (it will take the BUILT
// kcdxLocator value struct above, not an opaque handle).
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
```

The `kcdxLocator_MatchingPattern` form will resolve to `found = false` with
`reason == "matching_pattern_not_statement_locator"`, the same contract as the
Lua side — the AOB resolves against the binary's bytes elsewhere, not against
statement metadata.
