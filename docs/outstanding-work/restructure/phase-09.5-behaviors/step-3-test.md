# Phase 9.5 step 3 — test plugin (engine + cross-plugin behaviors)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

The permanent regression coverage for the behavior surface — both the
engine-catalog path and the cross-plugin declare/consume path.

## Scope

- `kcdx.behavior.set("<catalog entry>", true)` → underlying byte rewrite applies.
- Behavior-only plugin without `authored_against_game_version` still loads
  (exempt).
- `kcdx.behavior.list()` returns engine + plugin behaviors; `list("redmoon.")`
  filters.
- Cross-plugin: plugin A declares `a.test.foo`; plugin B calls
  `set("a.test.foo", "value")`; A's implementation fires with the value.
- Matrix rows in `../../../../test-plugins/README.md`.

## Test mode

`in-game` where a behavior's effect is only observable live; the list/declare/set
resolution checks are suite-gated. Declare each gesture + observable.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5" → Verification gate
+ the cross-plugin example.
