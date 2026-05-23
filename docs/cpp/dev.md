# Dev-mode introspection (↔ kcdx.dev)
> Part of the [kcdx C++ API](index.md).

> **NYI** — mirror of [kcdx.dev](../lua/dev.md); lands in the C++ parity
> backfill. There is no dedicated dev-mode introspection accessor in
> [`include/kcdx/Interfaces.h`](../../include/kcdx/Interfaces.h) today.

The Lua `kcdx.dev` domain is dev-mode sugar: `kcdx.dev.is_enabled()` (is engine
dev mode on) and `kcdx.dev.on_ready(fn)` (run now if `kcdx.*` is populated, the
chicken-and-egg helper for pak Lua loading before the kcdx table exists).

## Planned / built-adjacent shape

The two halves map differently on the C++ side:

- **`on_ready`** — the chicken-and-egg problem it solves does **not** exist for
  a C++ plugin: a DLL's `kcdxPlugin_Load` is already called at a well-defined
  point with a live `api`, and `kcdxMessage_LuaReady`
  ([lifecycle.md](lifecycle.md), value 9) is the built signal for "the Lua
  surface is now callable". So the C++ analogue of `on_ready` is **already
  available** via `RegisterListener` ([on.md](on.md)) on `kcdxMessage_LuaReady`
  — single-surface in spelling, not owed as a new accessor.

- **`is_enabled()`** — there is no built `IsDevModeEnabled()` accessor on the
  C++ surface. Planned shape: a boolean accessor on the root interface (mirror
  of the Lua predicate). Until it lands, a C++ plugin observes dev mode
  indirectly — `ReportTestResult` ([test.md](test.md)) is a no-op outside dev
  mode, and debug/trace `Log` lines are gated the same way. Exact C++ signature
  is **NYI** — it lands in the parity backfill.

This is the C++ counterpart of [kcdx.dev](../lua/dev.md).
