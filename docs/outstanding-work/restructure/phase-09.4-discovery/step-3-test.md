# Phase 9.4 step 3 — test plugin + console-path tests

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 3.

## What

The permanent regression coverage for discovery.

## Scope

- Test plugin (dev mode on, dev DB present): `kcdx.find({string = "test_marker"})`
  against the dev DB with a known function → expected name + statement list.
  Empty-criteria → `{}`. Synthetic 600-row search → 500 + `_truncated` +
  `_total_matches`.
- **Dev-gate graceful-failure check** (the load-bearing safety property): with the
  dev DB renamed/absent, `kcdx.find({string = "x"})` returns `{}` AND the dev-tool-
  unavailable teaching message is in the log — falsifiable: FAILS if `find` raises a
  Lua error, crashes, or returns a non-empty table. (A shipped mod calling `find` in
  a player's non-dev install must not break — this row pins that contract.)
- Console-path checks: `kcdx_find` and `kcdx_dev_inspect` parse module + criteria
  correctly; the not-found path produces the documented teaching error; the gated-off
  path prints the dev-tool-unavailable teaching message.
- Matrix rows in `../../../../test-plugins/README.md`.

## Test mode

`console` for the console verbs (the exact command strings + the falsifiable dev-log
observable); the Lua-find assertions are suite-gated boot/console checks.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → Verification gate.
