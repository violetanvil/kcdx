# Phase 9.4 step 1 — `kcdx.find{...}` Lua surface + `kcdx_find` console

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.

## What

`kcdx.find(criteria_table)` — discover a function from what the author already
knows. Plus the `kcdx_find` console command invoking the same path.

## Scope (`src/lua_bind_find.cpp`)

- `kcdx.find(criteria_table)` — table REQUIRED (the at-least-one-of-N case),
  validated at parse-time to contain at least one criterion. Criteria: `string`,
  `cvar`, `callers_of`, `callee`, `name_contains`, `callee_in_subsystem`.
- Returns a Lua table of records: `{function, module, rva, decompile_quality,
  statements = [{idx, kind, pseudo_text, captures, applicable_ops}]}`.
- **No matches:** returns `{}` (idiomatic `if #results == 0`). No nil, no error.
- **Result cap 500:** over-500 searches return the first 500 + `_truncated = true`
  + `_total_matches = N`. Loud truncation, not silent.
- Console `kcdx_find` — module as first positional (the module-required rule), then
  criteria flags: `kcdx_find WHGame.dll --string "test_marker"`. Same Lua path.

## Test bar

Step 3. Self-check: a known-string find returns the expected function; empty-criteria
returns `{}`; a synthetic 600-row search truncates loudly.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → Scope.
