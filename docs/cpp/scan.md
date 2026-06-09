# kcdxScanInterface (↔ kcdx.scan)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.scan`](../lua/scan.md) — the dev-time
address-discovery / AOB-pattern-validation workbench: resolve a hand-written
byte pattern against a module, log a concise diagnostic, and return an
attributed result the author branches on.

**Not yet implemented (NYI).** There is no scan interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. `kcdxScanInterface` is the **planned** mirror name; it is
tracked parity debt — both docs map a capability even when only one is built,
discharged when the C++ parity phase ships it and it is verified callable. This
entry maps the planned shape so both surfaces describe the capability while the
engine catches up.

Like Lua's `kcdx.scan`, the expert hand-written `pattern` is the **labelled
expert AOB hatch** here too, by design — not the common path. The everyday way
to intercept a function is the named-target hook (the built `kcdxHookInterface`,
↔ `kcdx.hook.before("WHGame.dll", "<name>", fn)`), where the engine resolves address **and**
verified ABI from a name. `kcdxScanInterface` is the workbench an expert uses to
discover and validate an un-named site they will then name.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options-struct, doing → typed
params), the planned mirror is a `QueryInterface`-fetched interface taking an
options struct that mirrors the Lua field set, returning the same attributed
result:

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
struct kcdxScanOptions {
    const char* name;                 // required — diagnostic log label
    const char* pattern;              // required — AOB pattern (expert hatch)
    const char* module;               // optional — default "WHGame.dll"
    int32_t     offset;               // optional — added to each hit; default 0
    const char* context;              // optional — uniqueness AOB
    const char* anchorString;         // optional — one anchor only
    const char* anchorFunctionByExport;
    const char* anchorSymbol;
    uint32_t    maxAnchorDistance;    // optional — default 4096
};

struct kcdxScanMatch  { void* addr; const char* module; uint64_t offset; };
struct kcdxScanResult { size_t count; kcdxScanMatch* matches; void* addr; };

// kcdxScanInterface::Scan(owner, const kcdxScanOptions&) -> kcdxScanResult
```

A C++ author would get the same resolve + concise diagnostic log + attributed
result (`count`, per-match `addr`/`module`/`offset`, first-match `addr`) as the
Lua surface — `count == 0` is a real result (no-match / module-not-loaded), not
an error.

## Today (the built fallback)

Until `kcdxScanInterface` lands, a C++ author does raw single-result pattern
scanning through `kcdxMemoryInterface::ScanPattern` ([memory.md](memory.md)) —
the built memory interface. It returns a single address rather than the attributed
multi-match diagnostic result the planned `kcdxScanInterface` (and Lua's
`kcdx.scan`) produce.

This is the C++ mirror of [kcdx.scan](../lua/scan.md).
