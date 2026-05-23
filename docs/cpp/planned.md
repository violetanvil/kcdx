# Planned — not yet available (↔ planned)
> Part of the [kcdx C++ API](index.md).

The C++ side of the Lua [planned.md](../lua/planned.md), plus the C++-specific
NYI mirrors tracked across these files. An interface that is NYI is designed but
not yet exported — do not link against it.

## Built on the C++ side ahead of Lua — co-save

- **`kcdxSerializationInterface`** — per-save plugin data (a `.kcdx` co-save).
  **Built (Phase 6)** and declared in
  [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h); fetch via
  `QueryInterface(kcdxInterface_Serialization, kcdxSerializationInterface_Version)`.
  Modeled on SKSE's `SKSESerializationInterface`: register Save/Load/Revert
  callbacks, `SetUniqueID`, then `OpenRecord`/`WriteRecordData` (save side) and
  `GetNextRecordInfo`/`ReadRecordData` (load side). Methods:

  ```cpp
  void (*SetUniqueID)      (kcdxPluginHandle plugin, uint32_t uid);
  void (*SetSaveCallback)  (kcdxPluginHandle plugin, kcdxSerializationSaveCallback   cb);
  void (*SetLoadCallback)  (kcdxPluginHandle plugin, kcdxSerializationLoadCallback   cb);
  void (*SetRevertCallback)(kcdxPluginHandle plugin, kcdxSerializationRevertCallback cb);
  bool (*OpenRecord)       (uint32_t tag, uint32_t version);
  bool (*WriteRecordData)  (const void* buf, uint32_t len);
  bool (*GetNextRecordInfo)(uint32_t* outTag, uint32_t* outVersion, uint32_t* outLen);
  bool (*ReadRecordData)   (void* buf, uint32_t len);
  ```

  This is the C++ original that the **Lua-side `kcdx.cosave.*` will mirror** —
  on the Lua surface `kcdx.cosave.*` is listed as planned/not-built
  ([../lua/planned.md](../lua/planned.md)); on the C++ surface the capability is
  already shipped. (A dedicated `cosave.md` is not split out here because the
  Lua folder has none either; this interface is documented here, where the Lua
  parity tracking lives.)

## NYI on the C++ side — Lua-first capabilities awaiting their C++ mirror

These are built in Lua and carry a tracked C++ parity debt (full NYI entries in
their files). The debt is owned by the restructure plan's **Phase 3 — C++ DLL
API parity** ([`docs/outstanding-work/restructure-plan.md`](../outstanding-work/restructure-plan.md)
§"Phase 3"), which names every C++ sub-interface to build and sets full Lua↔C++
parity as the bar; each NYI marker below is discharged when its Phase 3
interface ships and is verified callable:

- **`kcdxHookInterface`** (↔ `kcdx.hook`) — function interception. See
  [hook.md](hook.md).
- **The locator-based deferred byte-rewrite mirror** (↔ `kcdx.bytes`) — the raw
  runtime write (`kcdxMemoryInterface::WriteBytes`) is built; the
  locator/conflict-engine registration model is NYI. See [bytes.md](bytes.md).
- **`dynamic_call` / `dynamic_hook` C++ peers** (↔ `kcdx.memory.dynamic_*`) — no
  mirror interface in the header. See [memory.md](memory.md).
- **A dev-mode `is_enabled()` accessor** (↔ `kcdx.dev.is_enabled`) — see
  [dev.md](dev.md) (the `on_ready` half is already covered by
  `kcdxMessage_LuaReady`).

## Genuinely not built on either surface

- **`kcdx.scan{...}`** as a top-level diagnostic-scan verb — tracked in the
  restructure plan; not built in Lua or C++. (Runtime scanning today goes
  through `kcdxMemoryInterface::ScanPattern` — see [memory.md](memory.md).)
- **Gameplay domains** (`kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`,
  `kcdx.quest.*`, `kcdx.inventory.*`, `kcdx.assets.*`) — roadmap (Phase 9+), not
  built on either surface.

This is the C++ mirror of [the Lua planned list](../lua/planned.md).
