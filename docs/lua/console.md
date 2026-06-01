# kcdx.console
> Part of the [kcdx Lua API](index.md).

| Call | Args | Returns |
|---|---|---|
| `kcdx.console.print(text)` | string line | `true` if the line was accepted; `false` if the console surface isn't ready or the print path is unavailable on this build; `(nil, err)` on a non-string argument. |
| `kcdx.console.execute(commandLine)` | string command line | `true` on success; `false` if IConsole isn't ready; `(nil, err)` on a non-string argument. |

## `kcdx.console.print(text)` — show the user a line

The everyday "put a line in front of the user" call. Prints one plain line to
the in-game `~` console overlay, verbatim — no command-syntax wrapper, no
prefix. The engine owns the trailing newline, so pass your text without one.

```lua
local ok = kcdx.console.print("hello")
```

| Arg | Type | Meaning |
|---|---|---|
| `text` | string | The line to print to the `~` console overlay. |

**Returns:** `true` when the surface accepted the line; `false` if the console
surface isn't ready (before `kcdx.on("input_loaded")` fires) or the print path
could not be resolved on this build. A `false` is the surface refusing — the
engine logs the refusal, it is never a silent no-op.

**Bad argument:** a non-string `text` returns `(nil, err)` — `err` is a string
naming the expected `string` argument so you can find and fix the call. This is
the standard kcdx-binder error shape: a wrong call shape returns `(nil, err)`, a
valid call shape that the surface refuses returns `false`.

The console surface is armed at `input_loaded`, so call `print` from a
`kcdx.on("input_loaded", ...)` callback (or later) when you need the line to
actually paint:

```lua
kcdx.on("input_loaded", function()
    kcdx.console.print("my mod loaded")
end)
```

## `kcdx.console.execute(commandLine)` — run a command line

Runs a command line exactly as if typed into the `~` console, through the same
synchronous main-thread dispatch path. A command registered with `kcdx.command`
fires same-stack before `execute` returns.

```lua
local ok = kcdx.console.execute("cap26_cmd 42 hello")
```

This is the C++ mirror of [kcdxConsoleInterface](../cpp/console.md), which
carries both `Print` (↔ `print`) and `ExecuteString` (↔ `execute`).
