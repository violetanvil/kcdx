# The plugin DLL shell (↔ the Lua plugin shell)
> Part of the [kcdx C++ API](index.md).

A C++ plugin is a folder with a `kcdx.toml` and a sibling DLL that
`#include`s [`kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) and exports
at least one entry point kcdx calls at load. **Built** — the export contract
and the interface-query handshake are defined in the header.

The minimal working plugin:

`kcdx.toml`:

```toml
[plugin]
name    = "kcdx.my-first-plugin"
version = "0.1.0"

[entrypoints]
dll = "bin/my-plugin.dll"
```

`my-plugin.cpp`:

```cpp
#include "kcdx/Interfaces.h"

static kcdxLogger gLog;

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    gLog = kcdxLogger(api, api->GetPluginHandle("kcdx.my-first-plugin"));
    gLog.Info("MYMOD", "hello from my first plugin");
    return true;
}
```

## The export contract

A C++ plugin exports one to three functions. All are `extern "C"`. At least one
of `Preload`/`Load` must be present. The header declares their types:

```cpp
typedef bool (*kcdxPlugin_Preload_t)     (const kcdxInterface* api);  // preload wave
typedef bool (*kcdxPlugin_Load_t)        (const kcdxInterface* api);  // load wave (before slot)
typedef bool (*kcdxPlugin_PostGameLoad_t)(const kcdxInterface* api);  // after-game slot
```

| Export | When it runs | Lua mirror |
|---|---|---|
| `kcdxPlugin_Preload` | Preload wave, before any plugin's `Load`. For registering symbols/state another plugin's `Load` depends on. Most plugins omit it. | (no Lua slot) |
| `kcdxPlugin_Load` | Load wave, after every `Preload` returned. All other plugins are visible (`GetPluginInfo` works). Register listeners, install hooks, call peers here. | the `lua` (before/default) entrypoint slot |
| `kcdxPlugin_PostGameLoad` | After-game phase at the first update tick — after all before-game work is applied, before `kcdxMessage_InputLoaded`. Runs in load-order priority. | the `lua_after` entrypoint slot |

Return `true` on success. A `false` return is logged but the DLL is **not**
unloaded (kcdx, like SKSE, does not `FreeLibrary`). All three exports live on
the **same** DLL — there is no separate "after" DLL file.

`[entrypoints] dll` is relative to the plugin folder. If omitted, kcdx
auto-discovers exactly one `*.dll` in the plugin folder root; a multi-DLL
plugin **must** set `dll` explicitly. Plugin identity, version, dependencies,
and compatibility come from `kcdx.toml` `[plugin]` — parsed BEFORE the DLL is
loaded — exactly as for a Lua plugin (see the shared manifest keys in
[the Lua plugin shell](../lua/plugin-shell.md)).

## The interface-query handshake

`kcdxPlugin_Load` receives a `const kcdxInterface* api` — the read-only root
interface. Always-available calls hang off it directly; every capability domain
is fetched once via `QueryInterface`:

```cpp
void* (*QueryInterface)(uint32_t interfaceID, uint32_t version);
```

Pass a `kcdxInterfaceID` and the matching `kcdx<Name>Interface_Version`. Returns
`null` if the ID is unknown or the requested version is newer than the engine
supports — **always null-check the result**.

```cpp
bool kcdxPlugin_Load(const kcdxInterface* api) {
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (!msg) return false;   // engine too old / interface unavailable
    // ... use msg->RegisterListener(...)
    return true;
}
```

The interface IDs (`enum kcdxInterfaceID`): `kcdxInterface_Messaging` (1),
`kcdxInterface_Trampoline` (2), `kcdxInterface_Task` (3),
`kcdxInterface_Scripting` (4), `kcdxInterface_Serialization` (5),
`kcdxInterface_Memory` (6), `kcdxInterface_Console` (7).

## Self-identity

At load time your DLL does not yet know its own handle. Resolve it from your
manifest `name`, then cache it:

```cpp
kcdxPluginHandle self = api->GetPluginHandle("kcdx.my-first-plugin");
```

`GetPluginHandle` returns `kcdxInvalidPluginHandle` on a miss (a name mismatch
against your `kcdx.toml`).

This is the C++ mirror of [the Lua plugin shell](../lua/plugin-shell.md).
