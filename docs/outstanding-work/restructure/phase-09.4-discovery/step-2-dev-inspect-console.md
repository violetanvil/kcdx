# Phase 9.4 step 2 — `kcdx_dev_inspect` console command

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 2.

## What

`kcdx_dev_inspect <module> <function>` — full statement enumeration for one
function as a formatted console table: per-statement kind, pseudo-text, captures,
applicable ops. A DEV TOOL — resolves against the **dev DB**
(`reference-dev.sqlite`, the full corpus), same as `kcdx.find` (step 1), gated on
dev mode + the dev DB's presence.

## Scope

- Register the console command; enumerate the function's statements from the **dev
  DB** at `<game-bin>/kcdx-engine/data/reference-dev.sqlite` (read-only, separate
  from the shipped `reference.sqlite`); format as a readable table.
- **Dev-mode + dev-DB gate (graceful).** When dev mode is OFF or the dev DB is
  absent, `kcdx_dev_inspect` prints the SAME teaching message as `kcdx_find`
  (step 1's What section — set `dev_mode = true` + place `reference-dev.sqlite` at
  the path above), never an unhandled error. The gate is checked before the
  not-found path below.
- **Not-found UX (dev DB present, function unknown):** teaching error with
  name-similarity suggestion + recommended next step:
  ```
  [ERROR] no function 'IsInCombatt' in WHGame.dll for KCD2 1.5.1164953.
  Did you mean: IsInCombat? Try:
      kcdx_dev_inspect WHGame.dll IsInCombat
  Or search by content:
      kcdx_find WHGame.dll --name_contains combat
  ```

## Dependencies

**Consumes step 0's `refdb::EnumerateStatements(fn)` + dev-DB connection + the
dev-mode/dev-DB gate** ([`step-0-devdb-search-layer.md`](step-0-devdb-search-layer.md)).
Independent of step 1 (different command), shares step 0's surface.

## Test bar

Step 3. Self-check (dev mode on, dev DB present): a known function prints the
statement table; the not-found path produces the documented teaching error.
Gated-off self-check: with the dev DB absent, the command prints the dev-tool-
unavailable teaching message (the same one `kcdx_find` emits).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → `kcdx_dev_inspect`
(the dev-tool + dev-DB-gate decision is recorded in step 1's What section,
user-decided 2026-06-10).
