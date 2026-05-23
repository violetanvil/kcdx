# kcdxMessagingInterface — listeners (↔ kcdx.on)
> Part of the [kcdx C++ API](index.md).

Subscribe to engine lifecycle messages and to custom messages from other
plugins. **Built** — `kcdxMessagingInterface::RegisterListener` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Fetch the
interface via
`QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version)`.

This is the C++ spelling of Lua's `kcdx.on(event, fn)`: where Lua names an
event string, C++ subscribes a callback to a `sender` and filters the
`messageType` inside the callback.

## Call shape

```cpp
bool (*RegisterListener)(kcdxPluginHandle      listener,
                         const char*           sender,
                         kcdxMessagingCallback callback);
```

| Arg | Type | Meaning |
|---|---|---|
| `listener` | `kcdxPluginHandle` | Your handle. |
| `sender` | `const char*` | Whose messages to receive. `null` = engine-originated lifecycle messages (the `kcdxMessage_*` catalog — the mirror of Lua's lifecycle events). A specific plugin's stable name = only that plugin's broadcasts (the mirror of Lua's `"<publisher>:<event>"`). |
| `callback` | `kcdxMessagingCallback` | `void (*)(kcdxMessage* msg)`. |

**Returns:** `bool` — `true` on success, `false` on invalid arguments (e.g. an
unknown listener handle). Multiple listeners for the same sender are allowed;
each gets a copy.

The callback receives a `kcdxMessage*`:

```cpp
typedef struct kcdxMessage {
    const char* sender;       // stable plugin name; null = engine-originated
    uint32_t    messageType;  // a kcdxMessage_* value, or plugin-defined (>= 0x10000)
    const void* data;         // message-specific payload, null if none
    uint32_t    dataLen;      // payload byte length, 0 if data is null
} kcdxMessage;
```

The pointer is valid only for the duration of the callback — copy anything you
need to keep. Filter on `messageType` to react to a specific event. The
`kcdxMessage_*` catalog is documented in [lifecycle.md](lifecycle.md).

**Threading:** lifecycle messages fire on the main thread (see
[cross-cutting.md](cross-cutting.md)).

## Minimal snippet

```cpp
static void OnMessage(kcdxMessage* msg) {
    if (msg->messageType == kcdxMessage_InputLoaded) {
        gLog.Info("MYMOD", "world is up");
    } else if (msg->messageType == kcdxMessage_SaveGame) {
        gLog.Info("MYMOD", "saved to %s", static_cast<const char*>(msg->data));
    }
}

bool kcdxPlugin_Load(const kcdxInterface* api) {
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (!msg) return false;
    kcdxPluginHandle self = api->GetPluginHandle("my.plugin");
    msg->RegisterListener(self, /*sender=*/nullptr, &OnMessage);  // engine lifecycle
    return true;
}
```

This is the C++ mirror of [kcdx.on](../lua/on.md). For the engine message
catalog see [lifecycle.md](lifecycle.md); to broadcast your own message see
[publish.md](publish.md).
