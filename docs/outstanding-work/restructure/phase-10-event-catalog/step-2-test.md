# Phase 10 step 2 — gameplay-event subscription test

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

The permanent regression plugin for gameplay-event subscriptions.

## Scope

- `cap-XX-gameplay-events`: subscribes via `kcdx.on("<event>", fn)` to one or more
  of the step-1 events; asserts the callback fires when the in-game trigger
  occurs.
- Per the hook-fired-test rule: a test asserting a per-frame/per-event GAME hook
  fired must self-report PASS from the callback's FIRST fire (one-shot guarded),
  not poll a fire count at ready/input_loaded.
- Matrix rows in `../../../../test-plugins/README.md`.

## Test mode

`in-game` — the trigger (take damage, pick up an item, …) is a user gesture.
Declare the exact gesture + the falsifiable observable (the callback's first-fire
log line).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 10".
