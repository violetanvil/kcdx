# cap-36 C++ hook installs fail — apply handler not registered at Load time

## Symptom

All 7 CAP-36-cpp-hook-* rows FAIL on the chunk-5 verification launch
(2026-05-25, suite 79/94). Every row reports the same shape: `IsApplied=0`
+ observed value = the un-hooked original stub return. The C++-installed
hooks never fire. The sibling Lua hook (crosslang row) DOES fire
(`Lua before fired=1, seed observed=10`) — proving the Lua path works and
only the C path is broken.

## Facts

- The cap-36 plugin loaded fine (`handle=29`); its `kcdxHookInterface`
  thunks were callable. (engine log 01:07:56.794)
- The FIRST `K.hook->Before(...)` install returned handle 0. (cap-36 plugin
  log 01:07:57.593)
- The engine log carries the exact cause:
  `lua_registry::Append: no handler for Kind=1 (entry name='cap36_before')`
  → `[HOOK_INTERFACE] append_failed reason="no apply handler registered
  for this kind"`. (engine log 01:07:57.593)
- `Kind=1` is `Kind::Hook` (lua_registry.h:43 — `Bytes=0, Hook=1`).
- `RegisterApplyHandler(Kind::Hook, &ApplyHookEntry)` is called ONLY by
  `lua_bind_hook::bind()` (lua_bind_hook.cpp:951-953).
- `lua_bind_hook::bind()` runs from `RegisterKcdxTable` (lua_bind.cpp:186+),
  which fires at first-update-tick — when the Lua VM comes up.
- C++ `kcdxPlugin_Load` fires EARLIER, before the Lua VM is up. The engine
  log shows `RegisterFunction: queued … (kcdx global not ready yet)` at the
  same timestamp, confirming the Lua surface isn't installed at Load time.
- So when a C++ DLL's thunk calls `Append(Kind::Hook)` during
  `kcdxPlugin_Load`, the Hook apply-handler array slot is still null →
  `Append` rejects at lua_registry.cpp:319-329.
- A Lua plugin never hits this: its `plugin.lua` runs AFTER the VM is up +
  after `bind()` registered the handler.

## Reframe 2026-05-25: this is a lifecycle-ordering coupling, not a marshaling bug

The C dispatch path (BuildCDispatchThunk, AddC, the per-mode ABIs from
chunks 1-4) was NEVER REACHED. The entry failed to QUEUE — it never got
past `lua_registry::Append`. The bug is purely "the Kind::Hook apply
handler is registered too late for the C++ Load-time caller."

The handler registration is logically engine state (it makes `Kind::Hook`
appliable), but it currently lives inside the Lua binder's `bind()`, gated
on the Lua VM existing. The C++ surface introduced in chunks 3+4 routes
through `lua_registry` for its deferred-apply queue but consumes a handler
that only the Lua-side init path installs.

## Resolution

FIXED in `cdd5e7a`; verified live 2026-05-25 (suite 86/94, all 7
CAP-36-cpp-hook-* rows PASS incl. crosslang=122; cap-35 7/7 green; zero
`no handler for Kind` / `append_failed` lines). Fix direction = Option 1a: the
apply-handler REGISTRATION was split out of the Lua `bind()` functions into
per-binder `RegisterHandlers()` calls invoked at ENGINE INIT, before any
plugin's `kcdxPlugin_Load`. The registration MOVED (not duplicated) — leaving
it in `bind()` too would trip the warn-on-double-register at
lua_registry.cpp:388.

What changed:

- `src/lua_bind_hook.cpp` — new `kcdx::lua_bind_hook::RegisterHandlers()`
  calls `RegisterApplyHandler(Kind::Hook, &ApplyHookEntry)`; the call was
  removed from `bind()`, which now does Lua-surface wiring only
  (`EnsureHandleMetatable` + `lua_pushcfunction(Lua_Hook)` + `lua_setfield`).
  `ApplyHookEntry` stays a TU-local static; `RegisterHandlers()` is in the
  same TU.
- `src/lua_bind_bytes.cpp` — SAME treatment for `Kind::Bytes` /
  `ApplyBytesEntry` (the general fix — `Kind::Bytes` had the identical latent
  bug: a future `kcdxBytesInterface` installing a byte-patch at C++ Load time
  would have hit the same wall). The binder's shape was identical to
  lua_bind_hook's (one kind, one static handler), so no divergence.
- `src/lua_bind_hook.h` / `src/lua_bind_bytes.h` — declare `RegisterHandlers()`.
- `src/dllmain.cpp` — `#include`s both binder headers and calls
  `kcdx::lua_bind_hook::RegisterHandlers()` + `kcdx::lua_bind_bytes::RegisterHandlers()`
  immediately before `DiscoverAndLoad` (the "engine init complete, now load
  plugins" boundary).

The `lua_registry::Append` gate (lua_registry.cpp:319-329) was NOT weakened —
it is correct and is what surfaced the bug (AP9). The fix registers the
handler earlier; it does not loosen the gate.

## Trail

| Action | Result |
|---|---|
| Read cap-36 plugin log + engine log from the failing launch | Root cause found in Phase 1, no probe: `Append: no handler for Kind=1`. C apply handler registered too late (Lua-VM-init time) for the C++ Load-time caller. |
| Split `RegisterApplyHandler` out of `bind()` into per-binder `RegisterHandlers()`, called from dllmain.cpp before `DiscoverAndLoad`; applied to both Kind::Hook and Kind::Bytes | Handler now registered at engine init, before any C++ `kcdxPlugin_Load`. cap-36's 7 rows expected to go PASS (Append succeeds → AddC runs → hooks install + fire); Lua path (cap-15..22, cap-35) unaffected (`bind()` still wires the Lua surface; handler is merely registered earlier). |

## Closed questions with answers

**Where should `RegisterApplyHandler(Kind::Hook, &ApplyHookEntry)` move so it
runs before C++ `kcdxPlugin_Load`?** ANSWERED — it moves to a per-binder
`RegisterHandlers()` function (the handler is engine state, not Lua-surface
state) called at engine init in `dllmain.cpp`, immediately before
`DiscoverAndLoad`. The same treatment is applied to `Kind::Bytes`. The
registration MOVES out of `bind()` (it is not duplicated, to avoid the
warn-on-double-register at lua_registry.cpp:388).

## Active diagnostic instrumentation

(none — root cause found from existing logs; no probe code added.)
