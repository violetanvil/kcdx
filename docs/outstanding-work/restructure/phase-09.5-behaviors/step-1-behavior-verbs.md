# Phase 9.5 step 1 — `kcdx.behavior.*` verbs

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

The `kcdx.behavior.*` verbs in `src/lua_bind_behavior.cpp` — the unified surface
over both engine-shipped and plugin-declared behaviors.

## Scope

- `kcdx.behavior.declare(name, spec)` — a plugin declares a behavior it
  implements. Stamped `<plugin.author>.<plugin.name>.<name>` per the namespace
  model; callable from any plugin via the full name or bare-name precedence (self
  > engine > other). `spec` carries `description`, `default`, `implementation`
  (a Lua function or a reference to a hook/statement recipe).
- `kcdx.behavior.set(name, value)` — resolves name (self > engine > other), calls
  the resolved behavior's implementation with `value`. `value` accepts any Lua
  type; the implementation validates per its own logic — no engine type-gating.
- `kcdx.behavior.get(name)` — current set value, or the spec's `default` if never
  set.
- `kcdx.behavior.list([filter])` — all available behaviors, optionally
  prefix-filtered (`list("kcdx.")` engine-only; `list("redmoon.")` redmoon's; no
  filter = everything). Unifies the SQLite-shipped catalog + the in-memory
  runtime registry of plugin declarations.

## Capability-gating note

Behavior-only consumer plugins (only call `kcdx.behavior.set`) are exempt from
`authored_against_game_version`. A plugin that DECLARES a behavior whose
implementation calls `kcdx.hook.*` / `kcdx.statement.*` still needs the field
(it uses hash-checked primitives).

## Test bar

Step 3.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5" → Scope.
