# kcdx.lua
> Part of the [kcdx Lua API](index.md).

VM-introspection helpers.

| Call | Args | Returns |
|---|---|---|
| `kcdx.lua.cfunction_address(fn)` | a Lua C function value | A `kcdx.memory.pointer` userdata of the backing C function pointer, or `(nil, err)` if the argument is not a C function. |
| `kcdx.lua._probe_numbers()` | none | nothing — a dev-mode numeric-precision diagnostic that logs to `kcdx-dev.log` under category `LUA / NUMBER_PROBE`. Diagnostic only. |

`cfunction_address` returns a pointer userdata (never an integer) so you can
hand it straight to `kcdx.memory.dynamic_hook` as `target`.

```lua
local addr = kcdx.lua.cfunction_address(System.LogAlways)
kcdx.memory.dynamic_hook{ name = "log_hook", target = addr,
                          pre_callback = function() end }
```
