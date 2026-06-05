# Phase 9 step 5 — test plugins

**Status: DEFERRED → [`TD-0005`](../../../tech-debt/TD-0005-high-level-lua-surface.md)** (item 5). Ledger row: [`README.md`](README.md) → step 5.

## What

The permanent regression plugins for the high-level surface — `cap-XX-player-*`
and `cap-XX-inventory-*` — plus the stub NYI sub-test. (Steps 1–3 may self-check;
this step guarantees every shipped capability has a permanent row and the stubs
are covered.)

## Scope

- `cap-XX-player-health`: `set(50)` → `get() == 50`.
- `cap-XX-player-position`: `:get()` returns a plausible Vec3 (+ `:set()` if
  shipped).
- `cap-XX-inventory-add`: add a known item, assert count rose.
- stub sub-test: a `kcdx.world.*` (or similar) stub logs NYI + returns nil.
- Matrix rows in `../../../../test-plugins/README.md`.

## Test mode

`in-game` for the three real items (loaded save with a player). Declare each
gesture + falsifiable observable.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9" → "Verification".
