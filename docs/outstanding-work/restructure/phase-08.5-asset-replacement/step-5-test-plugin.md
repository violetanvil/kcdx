# Phase 8.5 step 5 — `cap-XX-asset-replace` test plugin

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 5.

## What

The permanent regression plugin for asset overlay. Replaces a known-safe game
file (e.g. a UI string visible in a menu) and verifies the replacement is visible
in-game, plus the conflict path.

## Scope

- `cap-XX-asset-replace` plugin: ships an `assets/` overlay of a known-safe file
  (and/or registers via `kcdx.assets.replace` — cover both registration paths
  per the grow-the-suite rule).
- A second plugin (or sub-plugin) overlaying the same file to exercise the
  conflict report ("lost to plugin X").
- Matrix row in `../../../../test-plugins/README.md`.

## Test mode

`in-game` — the replacement is only confirmable by reaching the menu/UART that
shows the replaced string. Declare the exact gesture (which menu) + the
falsifiable observable (the replaced string visible) + the overlay-hit dev-log
line.

## Dependencies

Steps 1–4.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5e" +
the phase verification gate in [`README.md`](README.md).
