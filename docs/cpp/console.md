# kcdxConsoleInterface — ExecuteString (↔ kcdx.console)
> Part of the [kcdx C++ API](index.md).

Run a console command line programmatically, as if typed into the `~` console.
**Built** — `kcdxConsoleInterface::ExecuteString` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h).

> **Note — shared interface.** The C++ header bundles command *registration* and
> command *execution* into one `kcdxConsoleInterface`, whereas the Lua surface
> splits them across `kcdx.command` and `kcdx.console`. This file documents
> `ExecuteString` to keep the C++ and Lua folders structurally parallel; the
> registration half of the same interface lives in [command.md](command.md).
> Fetch the interface once via
> `QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version)`.

## Call shape

```cpp
bool (*ExecuteString)(const char* commandLine);
```

| Arg | Type | Meaning |
|---|---|---|
| `commandLine` | `const char*` | Everything the user would type, including the command name and any quoted args. |

**Returns:** `bool` — `true` on success, `false` if the IConsole surface isn't
ready (i.e. before `kcdxMessage_InputLoaded` fires). Goes through the same
`IConsole::ExecuteString` dispatch path user input uses, so a command you
registered fires **synchronously** on the same thread before `ExecuteString`
returns.

## Minimal snippet

```cpp
bool ok = gConsole->ExecuteString("cap26_cmd 42 hello");
```

This is the C++ mirror of [kcdx.console](../lua/console.md). To register a
command, see [command.md](command.md).
