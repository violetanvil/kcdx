# kcdxPluginInfoInterface (↔ kcdx.plugin)
> Part of the [kcdx C++ API](index.md).

The C++ mirror of Lua's [`kcdx.plugin`](../lua/plugin.md) — plugin introspection
from C++: query the engine's view of another plugin (was it rejected at load
time?) so your DLL can degrade gracefully when an expected dependency didn't
load, rather than installing hooks against functions a missing peer plugin was
supposed to provide.

**Not yet implemented (NYI).** There is no plugin-info interface in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today — do not
link against it. `kcdxPluginInfoInterface` is the **planned** mirror name; it
is tracked parity debt — both docs map a capability even when only one is built,
discharged when the C++ parity phase ships it and it is verified callable. This
entry maps the planned shape so both surfaces describe the capability while the
engine catches up.

## Planned mirror shape (NYI)

Following the C++ surface model (configuring → options-struct, doing → typed
params; introspection takes positional typed params), the planned mirror is a
`QueryInterface`-fetched interface whose accessors take a plugin handle and
return the same predicate + reason pair the Lua surface produces:

```cpp
// PLANNED — NOT in Interfaces.h yet. Sketch of the mirror shape.
struct kcdxPluginInfoInterface {
    // Was the named plugin rejected by zone_gate this session?
    // `plugin` is a kcdxPluginHandle resolved by the existing
    // kcdxInterface::GetPluginHandle("<author>.<plugin>") — the same
    // identity surface every other C++ call takes a plugin via, so
    // the mirror does NOT re-introduce a string-name parameter at
    // the introspection layer.
    bool        (*IsRejected)  (kcdxPluginHandle plugin);

    // The teaching reason the gate recorded for this rejection, or
    // null if the plugin was not rejected (covers loaded, disabled,
    // and unknown — the predicate above is the source of truth).
    // Pointer is stable for the session; copy if you need to hold
    // it across other engine calls.
    const char* (*RejectReason)(kcdxPluginHandle plugin);
};
```

A C++ author would get the same `(bool, reason_or_null)` pair the Lua surface
produces:

```cpp
// PLANNED usage — NOT callable yet.
auto* info = static_cast<const kcdxPluginInfoInterface*>(
    api->QueryInterface(kcdxInterface_PluginInfo,
                        kcdxPluginInfoInterface_Version));
if (info) {
    kcdxPluginHandle dep = api->GetPluginHandle("redmoon.outfit");
    if (dep != kcdxInvalidPluginHandle && info->IsRejected(dep)) {
        api->Log(self, kcdxLogLevel_Warn, "MYMOD",
                 info->RejectReason(dep));
        return;  // skip the integration cleanly
    }
}
```

## Today (the built fallback)

Until `kcdxPluginInfoInterface` lands, a C++ author has no direct
introspection accessor: `GetPluginHandle` returns `kcdxInvalidPluginHandle` for
both *unknown* and *user-disabled* plugins, and currently for a *gate-rejected*
plugin too — but the C++ surface does not yet distinguish the three cases or
expose the gate's recorded teaching reason. Treat a missing handle as "the
dependency isn't live" and gate dependent work accordingly; the
distinguishing-and-reason capability lands with the mirror above.

This is the C++ mirror of [kcdx.plugin](../lua/plugin.md).
