# Phase 9.5 step 2 — engine-shipped catalog

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

The engine-shipped behavior catalog at `data/behavior-catalog/behaviors.toml`
(human-authored; a build step imports it into the reference DB with
`source = "engine"`). Reserved `kcdx.behavior.*` namespace.

## Scope

- Each entry: name, description, default value, implementation (a Lua function or
  a reference to a `kcdx.hook` / `kcdx.statement` recipe).
- **Ship 5–10 entries minimum**, all referencing functions whose data the
  parallel Ghidra research has produced. Canonical example:
  `kcdx.behavior.outfit_swap_in_combat` (the long-running case study of this
  design).
- The build step imports the TOML into the DB `behaviors` table (note: that table
  was not built under Phase 9.1 — this step lands it as its consumer arrives, per
  the no-anticipatory-structure rule).

## Dependencies

Step 1 (the verbs that read the catalog). Each catalog entry's underlying
hook/statement relies on Phase 9.3's namespaces being live — sequence 9.5 after
9.3, or scope catalog entries to recipes expressible with the shipped surface.

## Test bar

Step 3: `kcdx.behavior.set("<catalog entry>", true)` applies the underlying byte
rewrite.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.5" → engine-shipped
catalog.
