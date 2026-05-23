# kcdxConsoleInterface — RegisterCommand (↔ kcdx.command)
> Part of the [kcdx C++ API](index.md).

Register a console command runnable from the in-game `~` console. **Built** —
`kcdxConsoleInterface::RegisterCommand` plus the arg accessors, in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Fetch the
interface via
`QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version)`.

This is the C++ spelling of Lua's `kcdx.command{...}`.

## Call shape

```cpp
bool (*RegisterCommand)(kcdxPluginHandle           owner,
                        const char*                name,
                        const char*                help,
                        kcdxConsoleCommandCallback cb);
```

| Arg | Type | Meaning |
|---|---|---|
| `owner` | `kcdxPluginHandle` | Your handle. |
| `name` | `const char*` | Command name, unique across the process. kcdx refuses a name already registered by a different plugin. |
| `help` | `const char*` | Shown for `help <name>`. |
| `cb` | `kcdxConsoleCommandCallback` | `void (*)(const kcdxConsoleCmdArgs* args)`. Called on the main thread when the command fires. |

**Returns:** `bool` — `true` on success. **Lifetime:** the engine retains
`name`, `help`, and `cb` for the process lifetime — pass string literals or own
the storage yourself. kcdx registers with CryEngine's `VF_RESTRICTEDMODE` flag
automatically so the in-game `~` console can dispatch the command.

## Reading args inside the callback

The callback receives an opaque `kcdxConsoleCmdArgs*`; read it through the
interface accessors (kcdx wraps CryEngine's `IConsoleCmdArgs` vtable so you
never touch its layout). These mirror the Lua `args` table:

```cpp
int         (*GetArgCount)   (const kcdxConsoleCmdArgs* args);  // 1 + N (arg 0 is the command name)
const char* (*GetArg)        (const kcdxConsoleCmdArgs* args, int nIndex);  // nIndex=0 is the name; 1..count-1 are user args; null if out of bounds
const char* (*GetCommandLine)(const kcdxConsoleCmdArgs* args);  // the raw line (the mirror of Lua args.raw)
```

`GetArgCount` returns `1 + N` (arg 0 is the command name itself, matching
CryEngine's convention). Returned pointers are valid only for the callback
duration.

## Minimal snippet

```cpp
static void Cmd_OutfitDump(const kcdxConsoleCmdArgs* args) {
    int argc = gConsole->GetArgCount(args);
    gLog.Info("CMD", "argc=%d", argc);
    if (argc > 1) gLog.Info("CMD", "first=%s", gConsole->GetArg(args, 1));
}

bool kcdxPlugin_Load(const kcdxInterface* api) {
    gConsole = static_cast<kcdxConsoleInterface*>(
        api->QueryInterface(kcdxInterface_Console, kcdxConsoleInterface_Version));
    if (!gConsole) return false;
    gConsole->RegisterCommand(api->GetPluginHandle("my.plugin"),
                              "outfit_dump", "Dump outfit state to the log.",
                              &Cmd_OutfitDump);
    return true;
}
```

This is the C++ mirror of [kcdx.command](../lua/command.md). To run a command
programmatically, see [console.md](console.md) (`ExecuteString`, same interface).
