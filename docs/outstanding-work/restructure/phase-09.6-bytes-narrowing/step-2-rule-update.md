# Phase 9.6 step 2 — `lua-api-surface.md` rule 4 / 4a rewrite

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

Rewrite the design rule that governs the author surface to match the final
sub-verb model. Touches `.claude/rules/lua-api-surface.md` (private governance).

## Scope

- **Rule 4 rewritten:** "Required args are positional; optional args live in a
  trailing table." Required→positional = the author cannot forget (Lua errors at
  the call site, not later). Optional→table = self-documenting, order-free,
  additions don't break existing call sites.
- **New rule 4a:** "Discrete behavioral variants are sub-verbs, not table keys."
  `kcdx.<verb>.<variant>(...)` makes the variant impossible to forget, lets each
  carry its accurate signature, surfaces in autocomplete. Examples:
  `kcdx.hook.before/after/around/replace`,
  `kcdx.statement.replace_with/insert_before/insert_after`,
  `kcdx.log.info/warn/error/debug`. Mode-as-key reserved for cases where multiple
  modes legitimately compose on one call (none currently exist).

## Note

The rule file already documents the sub-verb model in part (struck during the
2026-05-28 doc updates) — confirm the current state of rule 4 / 4a against the
file before landing, don't assume the pre-rewrite text.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.6" →
"`.claude/rules/lua-api-surface.md` update".
