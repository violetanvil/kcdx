# Phase 9 step 2 — `kcdx.player.position`

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

`kcdx.player.position` — `:get()` returns a Vec3 (x, y, z) read from the player
struct. `:set()` is teleport-like and may not be safe; ship `:get()` for sure,
`:set()` only if RE confirms a safe write path.

## Scope

- RE the player-position field (Vec3 offset in the player struct); Address Library
  entry by name.
- Bind `:get()`. Probe the `:set()` write path — if a safe teleport write isn't
  confirmed, ship `:get()` only and record the `:set()` gap as its own follow-up
  (no deferred-correctness shortcut: ship what's safe, track the rest).
- `docs/lua/` entry in the same step.

## Test bar

`cap-XX-player-position`: asserts `:get()` returns a plausible Vec3 for a known
save location. `:set()` tested only if shipped. Test mode `in-game`.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9" → "Ships real".
