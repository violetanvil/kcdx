# Phase 8.5 step 4 — `kcdx.assets.*` Lua surface + `kcdxAssetInterface`

> **SUPERSEDED (2026-06-04) — historical record.** This 5-step stub was re-planned into the standalone [`../../asset-system/`](../../asset-system/README.md) tree (see this dir's [`README.md`](README.md)); the asset-system step docs are authoritative. Kept as the pre-spinout authoring record — NOT live work. (This step's intent — the `kcdx.assets.*` Lua surface + `kcdxAssetInterface` — was BUILT as asset-system Phase 2.)

**Status: NOT STARTED (superseded before build).** Ledger row: [`README.md`](README.md) → step 4.

## What

Ship the author-facing asset-overlay surface: the `kcdx.assets.*` Lua verbs and
the C++ `kcdxAssetInterface` mirror (full Lua↔C++ parity per the authoring-surface
rule). This is the programmatic alternative to the declarative
`[entrypoints].assets` directory — for authors who want to register overlays
conditionally.

## Scope

- `kcdx.assets.replace(virtual_path, loose_file)` — register an overlay at
  runtime.
- `kcdx.assets.replace_static(...)` — the static/declarative-equivalent form.
- Conflict reporting: when two plugins overlay the same virtual path, the
  loser gets the "lost to plugin X" report per the existing conflict-report
  shape.
- `kcdxAssetInterface` — the C++ peer (`K.assets->Replace(...)`), append-only
  per the interface-ABI rule.
- Docs entry in `docs/lua/` + `docs/cpp/` in the same step (docs-discipline).

## Disassembler test

`virtual_path` is a game-asset path the author already knows (the file they want
to replace) — no hex, no offset. Clean.

## Dependencies

Steps 1–3 (the overlay machinery the surface registers into).

## Test bar

Exercised at step 5; this surface is what step 5's plugin calls (or it uses the
declarative `[entrypoints].assets` form — step 5 covers both registration paths).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 8.5" → "Phase 8.5d".
