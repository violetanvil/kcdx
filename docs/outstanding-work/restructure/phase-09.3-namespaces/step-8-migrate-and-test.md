# Phase 9.3 step 8 — migrate existing hook test plugins + new tests

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 8.

## What

Migrate every existing hook test plugin to the new sub-verb shape and add the
phase's new regression plugins. The phase verification gate.

## Scope

- Migrate `cap-03-hook-lua-callback`, `cap-04-midhook`, `cap-20-hook-modes`,
  `cap-21-mid`, `cap-22-callsite`, etc. to the sub-verb shape; suite stays green.
- New `cap-XX-statement-replace` (zero per-call dispatch).
- New `cap-XX-plugin-fn-declare` (cross-plugin function access without
  disassembly).
- New `cap-XX-pdb-autoload` (PDB-sourced internal address + static op).
- C++ test plugins migrate alongside the Lua ones.
- Matrix rows in `../../../../test-plugins/README.md`.

## Note — the suite GROWS, it does not just migrate

Migrating the existing hook plugins to the new shape preserves their coverage; the
three NEW `cap-XX-*` plugins are the phase's own new permanent rows. A migration
that only re-pointed existing rows at the new surface (without the new
statement/declare/PDB rows) would leave the phase's own machinery untested.

## Dependencies

All prior steps.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "Verification
gate".
