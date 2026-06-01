# kcdxConsoleInterface — Print + ExecuteString (↔ kcdx.console)
> Part of the [kcdx C++ API](index.md).

Print a line to, or run a command line through, the in-game `~` console
programmatically. **Built** — `kcdxConsoleInterface::Print` and
`kcdxConsoleInterface::ExecuteString` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h)
(`kcdxConsoleInterface_Version == 2`).

> **Note — shared interface.** The C++ header bundles command *registration*,
> command *execution*, and line *printing* into one `kcdxConsoleInterface`,
> whereas the Lua surface splits registration across `kcdx.command` and puts
> execution + printing under `kcdx.console`. This file documents `Print` and
> `ExecuteString` to keep the C++ and Lua folders structurally parallel; the
> registration half of the same interface lives in [command.md](command.md).
> Fetch the interface once via
> `QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version)` — or use
> the pre-fetched `K.console` field on the [`Kcdx.h`](wrapper.md) handle.

## `Print` — show the user a line

The everyday "put a line in front of the user" call. Prints one plain line to
the `~` console overlay, verbatim — no command-syntax wrapper, no prefix. The
engine owns the trailing newline, so pass your text without one. The C++ mirror
of Lua's [`kcdx.console.print`](../lua/console.md).

### The common path — `kcdx::console::print(K, text)` (the wrapper floor)

The everyday C++ print path is the **empowered wrapper** in
[`include/kcdx/Kcdx.h`](../../include/kcdx/Kcdx.h) — the C++ peer of Lua's
`kcdx.console.print(text)`. It null-guards the interface + slot and forwards to
`K.console->Print(text)`, returning a safe `false` (rather than crashing) if a
plugin built against a newer header runs on an older engine whose
`kcdxConsoleInterface` has no `Print` slot.

```cpp
#include "kcdx/Kcdx.h"

static Kcdx K;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    if (!K.Init(api, "redmoon", "outfit")) return true;   // logs why
    bool ok = kcdx::console::print(K, "hello");
    return true;
}
```

The wrapper takes no options struct and emits no codegen — a console print is a
single one-arg call — so its value is the null-guard and the namespace symmetry
with `kcdx::hook::` / `kcdx::bytes::` (an author scanning the `kcdx::<domain>::`
namespaces finds console there too). Reach for the raw floor below only when you
want the interface slot directly.

### The raw floor (drop-down) — `kcdxConsoleInterface::Print`

```cpp
bool (*Print)(const char* text);
```

| Arg | Type | Meaning |
|---|---|---|
| `text` | `const char*` | The line to print to the `~` console overlay. The engine adds the trailing newline. |

**Returns:** `bool` — `true` when the surface accepted the line; `false` if the
console surface isn't ready (before `kcdxMessage_InputLoaded` fires), if the
underlying print path could not be resolved on this game build, or for a
null/empty string. A refusal is logged — it is never a silent no-op. The raw
slot carries no null-guard; call it only on a `K.console` you have confirmed is
non-null.

```cpp
bool ok = K.console->Print("hello");
```

The console surface is armed at `kcdxMessage_InputLoaded`, so print from an
`InputLoaded` listener (or later — e.g. `kcdxPlugin_PostGameLoad`) when you need
the line to actually paint.

## `ExecuteString` — run a command line

Run a console command line programmatically, as if typed into the `~` console.
The C++ mirror of Lua's [`kcdx.console.execute`](../lua/console.md).

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

```cpp
bool ok = K.console->ExecuteString("cap26_cmd 42 hello");
```

---

This is the C++ mirror of [kcdx.console](../lua/console.md) (`Print` ↔ `print`,
`ExecuteString` ↔ `execute`). To register a command, see
[command.md](command.md).
