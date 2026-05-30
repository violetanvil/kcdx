# Phase 9.3 step 3 — `kcdx.hook.*` sub-verb split

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

Split `kcdx.hook` from mode-as-key (the Phase 2 shape) into mode-as-sub-verb:
callback-based interception, the industry-standard meaning of "hook" (per-call
cost in microseconds; use when per-call Lua logic is needed).

## Scope (`src/lua_bind_hook.cpp`, replaces existing)

- `kcdx.hook.before/after/around/replace(module, target, [locator], callback,
  [opts])`.
- `kcdx.hook.insert_before/insert_after(module, target, locator, callback,
  [opts])` — locator REQUIRED (no meaningful default for "insert before what?").
- `module` is the required positional first arg (no default).
- `target` accepts canonical name / stable kcdx ID / Ghidra auto-name (Phase 9.1
  resolution), OR a `kcdx.functions.*` reference value (step 5). Engine dispatches
  by arg-1 type.
- `insert_before/after` callback receives captures as a named table; returning a
  table writes the named captures back to registers/memory (same shape as
  `before` mode's return flow). The captures-by-name thunk is built once at
  install — native-speed register-to-table copies per call, no per-call lookup.

## Dependencies

Step 1 (locators). Consumes step 5's `kcdx.functions.*` when arg-1 is a reference.

## Test bar

Step 8 migrates the existing hook plugins to this shape; this step self-checks
each sub-verb fires.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "The two distinct
namespaces" + "`insert_before/insert_after` callback signature".
