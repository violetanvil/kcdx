# kcdxSerializationInterface (↔ kcdx.cosave)
> Part of the [kcdx C++ API](index.md).

Persist your plugin's state across saves — a counter, a settings blob, a set of
flags — written when the player saves and read back when they load, tied to the
specific save file (a `.kcdx` co-save next to the game's `.whs`). **Built**
(Version 2) — `kcdxSerializationInterface` in
[`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h). Fetch via
`QueryInterface(kcdxInterface_Serialization, kcdxSerializationInterface_Version)`.

This is the C++ spelling of Lua's [`kcdx.cosave.*`](../lua/cosave.md). Where Lua
gives you `on_save`/`on_load` registration functions and `write`/`records` data
calls, C++ exposes the same model as callback setters plus a write/read
record API.

## The two moments — write/read from your callbacks, NOT a save lifecycle message

Your write/read logic goes inside the `SaveCallback` / `LoadCallback` you
register — **not** in a `kcdxMessage_SaveGame` listener. The write window
(`OpenRecordNamed` / `WriteRecordData`) is open only inside the `SaveCallback`,
which fires while the engine is writing the co-save; a `kcdxMessage_SaveGame`
listener fires after the file is already written, so a write there persists
nothing. (This mirrors the Lua `on_save`-vs-`kcdx.on("save_game")` split.)
`OpenRecord*` / `WriteRecordData` / `GetNextRecordInfo` / `ReadRecordData` consult
thread-local engine state valid only inside the callback — call them nowhere
else. Both callbacks fire on the main thread.

## Call shape

```cpp
void (*SetUniqueID)(kcdxPluginHandle plugin, uint32_t uid);

void (*SetSaveCallback)  (kcdxPluginHandle plugin, kcdxSerializationSaveCallback   cb);
void (*SetLoadCallback)  (kcdxPluginHandle plugin, kcdxSerializationLoadCallback   cb);
void (*SetRevertCallback)(kcdxPluginHandle plugin, kcdxSerializationRevertCallback cb);

// write side — call from your SaveCallback
bool        (*OpenRecordNamed)(const char* tag, uint32_t version);   // common path
bool        (*WriteRecordData)(const void* buf, uint32_t len);

// read side — call from your LoadCallback
bool        (*GetNextRecordInfo)(uint32_t* outTag, uint32_t* outVersion, uint32_t* outLen);
const char* (*GetRecordTagName)();                                    // common path
bool        (*ReadRecordData)   (void* buf, uint32_t len);

// expert / interop — numeric tag instead of a string
bool        (*OpenRecord)(uint32_t tag, uint32_t version);
```

| Call | Meaning |
|---|---|
| `SetUniqueID(plugin, uid)` | **Required.** Identifies your plugin's section in the co-save (a non-zero u32; a `0` uid is dropped silently). See the parity note below. |
| `SetSaveCallback` / `SetLoadCallback` / `SetRevertCallback` | Register the bodies. `RevertCallback` fires on a new game, or when loading a save with no section for your UID — reset in-memory state to "fresh game" there. Pass `null` to clear; re-registering replaces. |
| `OpenRecordNamed(tag, version)` | **Common path.** Start a chunk under a human-readable string `tag` (`"counter"`); the engine hashes it to the stored u32 *and* records the string for read-back. `version` is your per-tag schema version. Follow with `WriteRecordData`. |
| `WriteRecordData(buf, len)` | Append `len` bytes to the chunk last opened. |
| `GetNextRecordInfo(&tag, &version, &len)` | Advance to your next chunk; `false` when none remain (loop until then). |
| `GetRecordTagName()` | **Common path.** After a successful `GetNextRecordInfo`, the string tag the chunk was opened under (`""` for a numeric-`OpenRecord` chunk or a pre-named-format cosave). Never null; the pointer is transient — copy it before the next `GetNextRecordInfo`/`ReadRecordData`. |
| `ReadRecordData(buf, len)` | Pull the current chunk's `len` bytes into `buf`. |
| `OpenRecord(tag, version)` | **Expert/interop.** The numeric-tag write side; the author hand-packs a u32 FourCC, and the chunk stores no name (`GetRecordTagName` returns `""`). Prefer `OpenRecordNamed` — the named path is the disassembler-test fix for the hand-packed tag. |

**Returns:** the write/read calls return `false` if `SetUniqueID` wasn't called
or you're outside a save/load phase; `OpenRecordNamed` also returns `false` (and
logs, naming both tags + your plugin) if two *different* string tags collide to
the same hash within one save — a silent data-merge hazard the engine refuses
rather than merges.

> **Parity note (tracked debt).** The Lua binder
> auto-derives the co-save section UID from the plugin's *name*: the common Lua
> path calls **no** `set_uid`, and the engine carries the identity from the name.
> The C++ surface does **not** do this yet — a C++ author must call `SetUniqueID`
> **explicitly** (a u32, e.g. a FourCC of the plugin's short name). This is the
> current parity gap, not a permanent single-surface difference: the end-state is
> the C++ mirror offering the same name-derived default, with `SetUniqueID` kept
> as the explicit override. Until then, treat the explicit `SetUniqueID` as
> required.

## Minimal snippet

A counter persisted under the string tag `"counter"`, read back on load. Pin the
UID explicitly (the C++ path today).

```cpp
constexpr uint32_t kUID = 0x53323143;  // 'C','1','2','S' — a FourCC of your name
uint64_t g_counter = 0;

void OnSave(kcdxPluginHandle) {
    g_counter += 1;
    g_ser->OpenRecordNamed("counter", 1);          // common path: string tag
    g_ser->WriteRecordData(&g_counter, sizeof(g_counter));
}

void OnLoad(kcdxPluginHandle) {
    uint32_t tag = 0, version = 0, len = 0;
    while (g_ser->GetNextRecordInfo(&tag, &version, &len)) {
        const char* name = g_ser->GetRecordTagName();   // read back by string
        if (name && strcmp(name, "counter") == 0 && len == sizeof(uint64_t)) {
            g_ser->ReadRecordData(&g_counter, sizeof(g_counter));
        }
    }
}

void OnRevert(kcdxPluginHandle) { g_counter = 0; }      // new game / no section

bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_ser = static_cast<const kcdxSerializationInterface*>(
        api->QueryInterface(kcdxInterface_Serialization,
                            kcdxSerializationInterface_Version));
    if (!g_ser) return true;

    kcdxPluginHandle self = api->GetPluginHandle("my.plugin");
    g_ser->SetUniqueID(self, kUID);                 // explicit — see parity note
    g_ser->SetSaveCallback  (self, OnSave);
    g_ser->SetLoadCallback  (self, OnLoad);
    g_ser->SetRevertCallback(self, OnRevert);
    return true;
}
```

This is the C++ mirror of [kcdx.cosave](../lua/cosave.md); the
[`test-plugins/cap-12-serialization`](../../test-plugins/cap-12-serialization)
plugin demonstrates the named-tag path end-to-end.
