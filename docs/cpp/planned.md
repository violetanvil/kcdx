# Planned — not yet available (↔ planned)
> Part of the [kcdx C++ API](index.md).

The C++ side of the Lua [planned.md](../lua/planned.md), plus the C++-specific
NYI mirrors tracked across these files. An interface that is NYI is designed but
not yet exported — do not link against it.

## co-save — built on BOTH surfaces

`kcdxSerializationInterface` (per-save plugin data, a `.kcdx` co-save) is built
and now documented in its own [cosave.md](cosave.md), alongside the Lua
[../lua/cosave.md](../lua/cosave.md) — both surfaces ship it. (It is no longer
tracked here: it was listed under "built ahead of Lua" while the Lua mirror was
planned; the Lua `kcdx.cosave.*` binder has since landed, so the symmetric
per-call file is the home, not this planned list.) Its one remaining parity
debt — the C++ surface does not yet auto-derive the cosave UID from the plugin
name the way the Lua binder does — is recorded in [cosave.md](cosave.md).

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
- **`kcdxScanInterface`** (↔ `kcdx.scan`) — the diagnostic AOB-scan /
  address-discovery workbench. Built in Lua; no mirror interface in the header
  yet. Today a C++ author uses `kcdxMemoryInterface::ScanPattern` for raw
  single-result scanning. See [scan.md](scan.md).

## Genuinely not built on either surface

- **Gameplay domains** (`kcdx.player.*`, `kcdx.world.*`, `kcdx.dialogue.*`,
  `kcdx.quest.*`, `kcdx.inventory.*`, `kcdx.assets.*`) — roadmap (Phase 9+), not
  built on either surface.

This is the C++ mirror of [the Lua planned list](../lua/planned.md).
