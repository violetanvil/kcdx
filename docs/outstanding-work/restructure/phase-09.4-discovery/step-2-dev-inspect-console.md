# Phase 9.4 step 2 — `kcdx_dev_inspect` console command

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

`kcdx_dev_inspect <module> <function>` — full statement enumeration for one
function as a formatted console table: per-statement kind, pseudo-text, captures,
applicable ops. Resolves via the reference DB.

## Scope

- Register the console command; enumerate the function's statements from the
  reference DB; format as a readable table.
- **Not-found UX:** teaching error with name-similarity suggestion + recommended
  next step:
  ```
  [ERROR] no function 'IsInCombatt' in WHGame.dll for KCD2 1.5.1164953.
  Did you mean: IsInCombat? Try:
      kcdx_dev_inspect WHGame.dll IsInCombat
  Or search by content:
      kcdx_find WHGame.dll --name_contains combat
  ```

## Dependencies

Independent of step 1 (different command), shares the reference-DB resolution.

## Test bar

Step 3. Self-check: a known function prints the statement table; the not-found
path produces the documented teaching error.

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → `kcdx_dev_inspect`.
