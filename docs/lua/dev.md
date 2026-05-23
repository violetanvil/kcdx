# kcdx.dev
> Part of the [kcdx Lua API](index.md).

| Call | Args | Returns |
|---|---|---|
| `kcdx.dev.is_enabled()` | none | bool — whether engine dev mode is on. |
| `kcdx.dev.on_ready(fn)` | function | Invokes `fn()` immediately if `kcdx.*` is fully populated; returns `true` if it ran, `false` if not yet ready. (Sugar — by the time your Lua can call this, kcdx is ready.) |

```lua
if kcdx.dev.is_enabled() then
    kcdx.log.debug("MYMOD", "dev diagnostics on")
end
```
