# kcdxFindInterface (↔ kcdx.find)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.find`](../lua/find.md) — the dev-time
function-discovery workbench: search the dev reference DB for game functions
matching what you already know about them (a string they reference, a CVar they
read, a function they call or are called by, a name substring), and get back the
matching function **headers** (name, module, rva, decompile-quality, and a
statement count). Like the Lua surface, find returns lean headers — a function's
actual statements are inspected one at a time via the dev-inspect path, not
carried in the find result.

**Not yet implemented (NYI).** There is no find interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not link
against it. `kcdxFindInterface` is the **planned** mirror name; it is tracked
parity debt — both docs map a capability even when only one is built, discharged
when the C++ parity phase ships it and it is verified callable. This entry maps
the planned shape so both surfaces describe the capability while the engine
catches up.

Like Lua's `kcdx.find`, this is a **dev tool** and a **dev-mode-only** one: it
searches the separate dev reference DB (the full game corpus), which the shipped
product does not carry, gated on dev mode plus that file's presence. When the
gate fails it returns an empty result and reports the same teaching message — it
never errors and never crashes a shipped plugin in a player's non-dev install.
Discovery is an authoring-time activity: find a function here, then write your
`kcdxStatementInterface` / `kcdxLocator` code against it.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options-struct, doing → typed
params), the planned mirror is a `QueryInterface`-fetched interface taking a
criteria struct that mirrors the Lua field set (every field optional; at least
one required, AND-ed), returning the same record set:

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
struct kcdxFindCriteria {
    const char* string;               // functions referencing this string literal
    const char* cvar;                 // functions reading this CVar by name
    const char* callersOf;            // callers of the named function
    const char* callee;               // functions calling the named function
    const char* nameContains;         // functions whose name contains this substring
    const char* calleeInSubsystem;    // functions calling into this subsystem prefix
    // each is optional (null = unset); at least one required, multiple AND-ed.
};

struct kcdxFindRecord {           // a LEAN function header — no statement bodies
    const char* function; const char* module; uint64_t rva;
    int decompileQuality;
    int64_t statementCount;       // how many statements (a count, not the rows)
};
struct kcdxFindResult {
    const kcdxFindRecord* records; size_t recordCount;
    bool truncated;          // result is a capped prefix (cap 500)
    int64_t totalMatches;    // full match count when truncated
    bool unavailable;        // dev gate failed (dev mode off / dev DB absent)
};

// kcdxFindInterface::Find(const kcdxFindCriteria&) -> kcdxFindResult
```

A C++ author would get the same record set as the Lua surface — `recordCount == 0`
is a real result (no match **or** dev-tool-unavailable; `unavailable` distinguishes
them), never an error; an over-500 search sets `truncated` + `totalMatches`. The
statement DETAIL (kind, pseudo-text, captures, applicable ops) is the planned
dev-inspect mirror for a single function, not part of the find record — the same
two-tool division as the Lua surface (`kcdx.find` discovers which function;
`kcdx_dev_inspect` inspects one function's body).

## Today (the built fallback)

Until `kcdxFindInterface` lands, a C++ author uses the **Lua** `kcdx.find` surface
(or the [`kcdx_find` console command](../lua/find.md#the-in-game-console-peer))
for discovery, then writes the resulting names into their C++ plugin's
[`kcdxStatementInterface`](statement.md) / `kcdxHookInterface` calls. Discovery is
an authoring-time step, so the absence of the C++ discovery interface does not
block a C++ plugin — the author discovers once (in Lua or the console) and codes
against the name thereafter.

This is the C++ mirror of [kcdx.find](../lua/find.md).
