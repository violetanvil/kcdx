# kcdxMessagingInterface — Dispatch (↔ kcdx.publish)
> Part of the [kcdx C++ API](index.md).

Broadcast a custom message to subscribers in any plugin (the counterpart to
`RegisterListener` with your name as `sender`). **Built** —
`kcdxMessagingInterface::Dispatch` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Same interface
as [on.md](on.md); fetch it via
`QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version)`.

This is the C++ spelling of Lua's `kcdx.publish(event, payload)`.

## Call shape

```cpp
bool (*Dispatch)(kcdxPluginHandle sender,
                 uint32_t         messageType,
                 const void*      data,
                 uint32_t         dataLen,
                 const char*      receiver);
```

| Arg | Type | Meaning |
|---|---|---|
| `sender` | `kcdxPluginHandle` | Your handle. Listeners that subscribed to your stable name see this. |
| `messageType` | `uint32_t` | Your message type. Use values `>= kcdxMessage_FirstUserDefined` (`0x10000`) to avoid colliding with the engine catalog. |
| `data` | `const void*` | Payload, or `null`. Passed **by pointer** to each listener — valid only for the callback duration; listeners copy what they keep. |
| `dataLen` | `uint32_t` | Payload byte length; `0` if `data` is null. |
| `receiver` | `const char*` | `null` = broadcast to everyone subscribed to this sender. Non-null = deliver only to listeners that subscribed specifically to `sender`. |

**Returns:** `bool` — `true` if at least one listener fired (`false` means
nobody listened; that is not an error, the mirror of Lua `kcdx.publish`
returning `0`).

## Minimal snippet

```cpp
// in plugin "violetanvil":
enum { Msg_OutfitChanged = kcdxMessage_FirstUserDefined + 1 };
struct OutfitPayload { int slot; const char* name; };

OutfitPayload p{ 2, "Noble" };
msg->Dispatch(self, Msg_OutfitChanged, &p, sizeof(p), /*receiver=*/nullptr);

// in another plugin, inside a RegisterListener(self, "violetanvil", cb) callback:
if (m->messageType == Msg_OutfitChanged) {
    auto* op = static_cast<const OutfitPayload*>(m->data);
    gLog.Info("MOD", "outfit -> %s", op->name);
}
```

This is the C++ mirror of [kcdx.publish](../lua/publish.md). To subscribe, see
[on.md](on.md).
