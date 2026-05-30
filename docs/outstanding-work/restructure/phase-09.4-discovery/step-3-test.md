# Phase 9.4 step 3 — test plugin + console-path tests

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

The permanent regression coverage for discovery.

## Scope

- Test plugin: `kcdx.find({string = "test_marker"})` against a reference DB with a
  known function → expected name + statement list. Empty-criteria → `{}`. Synthetic
  600-row search → 500 + `_truncated` + `_total_matches`.
- Console-path checks: `kcdx_find` and `kcdx_dev_inspect` parse module + criteria
  correctly; the not-found path produces the documented teaching error.
- Matrix rows in `../../../../test-plugins/README.md`.

## Test mode

`console` for the console verbs (the exact command strings + the falsifiable dev-log
observable); the Lua-find assertions are suite-gated boot/console checks.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → Verification gate.
