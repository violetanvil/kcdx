# kcdx.console
> Part of the [kcdx Lua API](index.md).

| Call | Args | Returns |
|---|---|---|
| `kcdx.console.execute(commandLine)` | string command line | `true` on success; `false` if IConsole isn't ready; `(nil, err)` on a non-string argument. |

Runs a command line exactly as if typed into the `~` console, through the same
synchronous main-thread dispatch path. A command registered with `kcdx.command`
fires same-stack before `execute` returns.

```lua
local ok = kcdx.console.execute("cap26_cmd 42 hello")
```
