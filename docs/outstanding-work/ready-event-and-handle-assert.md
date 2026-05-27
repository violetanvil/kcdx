# Post-apply "ready" event + handle:applied() assertion

## Status

Deferred. `kcdx.hook` (and `kcdx.bytes`) return a handle whose
`:applied()` flips from `nil` (Pending) to `true`/`false` during the
end-of-zone apply pass (`lua_registry::ApplyZone`). But `ApplyZone` runs
AFTER a plugin's `plugin.lua` has finished executing, and there is no
lifecycle callback a plugin can register to run code once its zone's
apply pass completes. So a plugin.lua cannot, in straight-line code,
observe its own handles' applied/failed outcome — including the
loud-fail REASON on a rejected hook.

This blocks AUTOMATED in-test assertions of failure paths, e.g.
"address_id with a bad name fails with a clear reason"
(CAP-20-addrname's miss case) or "the load-order-losing replace's handle
went Failed" (CAP-20-conflict's rejection side). Those behaviors ARE
implemented and logged today; only the in-Lua assertion of them is
deferred.

## Trigger to revisit

When any of these is needed:
- A test must assert a hook/bytes handle reached `Failed` with a reason
  (the deferred miss-asserts above).
- A plugin author asks "how do I run setup code once my hooks are live?"
  (the SKSE `kInputLoaded`-style "everything's installed" moment, but for
  the deferred-apply Lua surface).
- `handle:wait_applied()` (currently a Phase-2i stub in
  `lua_registry.cpp`) needs to actually work.

## Design

The restructure plan already specifies the surface
(`docs/outstanding-work/restructure-plan.md` §"kcdx.on ... ready"): ONE
event name, `"ready"`, routed per-plugin by zone. After `ApplyZone(zone)`
installs that zone's entries, the engine fires `"ready"` to every plugin
in that zone whose `plugin.lua` registered `kcdx.on("ready", fn)`. At
that point every handle the plugin created has a final `:applied()` /
`:reason()`.

- `kcdx.on(event, fn)` is the (not-yet-built) lifecycle/event surface —
  a core authoring verb per `.claude/rules/lua-api-surface.md`.
- The coroutine-based `handle:wait_applied()` (plan §"Plugin
  coroutines") is the alternative ergonomic path; the callback `kcdx.on`
  path is the simpler first step.

## Files that need to change

- `src/lua_bind_on.cpp` (new) — `kcdx.on(event, fn)` binder; store
  per-plugin "ready" callbacks keyed by owning plugin.
- `src/lua_registry.cpp` — after `ApplyZone(zone)` transitions entries,
  fire the "ready" callbacks for plugins in that zone (routed by the
  same load_order zone resolution ApplyZone already uses).
- `src/hooks.cpp` — call the ready-dispatch right after the first-tick
  `ApplyZone(AfterGame)`.
- `test-plugins/cap-20-hook-modes/plugin.lua` — add the deferred
  miss-asserts: CAP-20-addrname-miss (bad `address_id` name →
  `handle:applied()==false` + reason) and the CAP-20-conflict
  rejection-side assertion (the load-order-losing replace's handle
  went Failed). Both currently rely on the engine-log reason only.

## Related

- `docs/outstanding-work/restructure-plan.md` — `kcdx.on` / "ready"
  event spec.
- `src/lua_registry.cpp` — `H_wait_applied` stub; the `"ready"` routing
  hooks in here.
