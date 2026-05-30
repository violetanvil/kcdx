# Phase 9.3 step 7 — C++ parity

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 7.

## What

Mirror the new Lua shapes on the C++ surface at full parity (the authoring-surface
rule: Lua↔C++ feature parity is an invariant on the shipped product).

## Scope

- `kcdxHookInterface` gains `Before/After/Around/Replace/InsertBefore/InsertAfter`
  sub-methods (mirroring the Lua sub-verb shape). The in-flight Phase 3
  mode-as-field shape migrates to sub-method shape here.
- New `kcdxStatementInterface` — static-bytes work, same shape as
  `kcdx.statement.*`.
- New `kcdxFunctionsInterface` — mirrors `kcdx.dll.declare` (`K.functions->Declare(...)`).
- All interface changes append-only (the interface-ABI rule — never insert
  mid-struct; pre-built plugins AV on load otherwise).

## Dependencies

The Lua shapes (steps 3, 4, 5) settled first.

## Test bar

C++ test plugins migrate alongside the Lua ones (step 8); both surfaces of each
capability under permanent regression.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.3" → "C++ side parity".
