# Lifecycle messages — the kcdxMessage_* catalog (↔ kcdx.on lifecycle)
> Part of the [kcdx C++ API](index.md).

The engine-originated messages a `null`-sender `RegisterListener`
([on.md](on.md)) receives. **Built** — `enum kcdxMessageType` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). All engine
messages arrive with `sender == null` in the `kcdxMessage` struct; filter on
`messageType`. This is the C++ spelling of the Lua lifecycle event strings.

## The engine message catalog

| `kcdxMessage_*` | Value | `data` payload | Lua event |
|---|---|---|---|
| `kcdxMessage_PostLoad` | 1 | none | `post_load` |
| `kcdxMessage_PostPostLoad` | 2 | none | `post_post_load` |
| `kcdxMessage_InputLoaded` | 3 | none — fires every boot (the standard auto-pass trigger) | `input_loaded` |
| `kcdxMessage_NewGame` | 4 | none | `new_game` |
| `kcdxMessage_PreLoadGame` | 5 | none (fires multiple times per user load; not deduplicated) | `pre_load_game` |
| `kcdxMessage_PostLoadGame` | 6 | none | `post_load_game` |
| `kcdxMessage_SaveGame` | 7 | `const char*` save basename (e.g. `"save561.whs"`) | `save_game` |
| `kcdxMessage_DeleteGame` | 8 | `const char*` save basename | `delete_game` |
| `kcdxMessage_LuaReady` | 9 | none — `_G.kcdx` is populated and callable (once per process) | (Lua: use `kcdx.dev.on_ready`) |
| `kcdxMessage_LoadGameSelected` | 10 | `const char*` save basename — fires once per user load, before deserialization | `load_game_selected` |
| `kcdxMessage_FirstUserDefined` | `0x10000` | — (first plugin-defined message ID; use `>=` this for your own — see [publish.md](publish.md)) | (custom `"<publisher>:<event>"`) |

`kcdxMessage_LoadGameSelected` (10) is distinct from `kcdxMessage_PreLoadGame`
(5): `PreLoadGame` fires at every internal `LoadGame` invocation (including
engine bootstraps with no user-chosen file); `LoadGameSelected` fires only when
the user explicitly picked a save AND the engine resolved its on-disk filename
— prefer it for sidecar serialization workflows.

For payload-bearing messages, read `data` inside the callback (valid only for
the callback duration):

```cpp
static void OnMessage(kcdxMessage* msg) {
    switch (msg->messageType) {
        case kcdxMessage_InputLoaded:
            gLog.Info("MYMOD", "world is up");
            break;
        case kcdxMessage_SaveGame:
            gLog.Info("MYMOD", "saved to %s",
                      static_cast<const char*>(msg->data));
            break;
    }
}
```

This is the C++ mirror of [the Lua lifecycle events](../lua/lifecycle.md). To
subscribe see [on.md](on.md); for the `"ready"` event (per-plugin post-apply),
the C++ analogue is the `kcdxPlugin_PostGameLoad` export
([plugin-shell.md](plugin-shell.md)).
