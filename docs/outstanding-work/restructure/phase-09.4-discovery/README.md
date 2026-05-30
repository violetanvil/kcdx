# Phase 9.4 — `kcdx.find{...}` discovery + `kcdx_dev_inspect` console

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4".

Without discovery, statement-level modding requires Ghidra. With it, authors find
what they need from what they already know about the game (a string they saw, a
CVAR they read about, a function they suspect). Both resolve via the reference DB;
no new author-facing concepts beyond the existing locator + op vocabularies.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.find{...}` Lua surface + `kcdx_find` console](step-1-find-surface.md) | NOT STARTED | — |
| [2 — `kcdx_dev_inspect` console command](step-2-dev-inspect-console.md) | NOT STARTED | — |
| [3 — test plugin + console-path tests](step-3-test.md) | NOT STARTED | — |

## Verification gate (whole phase)

`kcdx.find({string = "test_marker"})` against a reference DB with a known function
returns the expected name + statement list. Empty-criteria → `{}`. A synthetic
600-row search returns 500 records + `_truncated = true` + `_total_matches = 600`.
Console `kcdx_find` / `kcdx_dev_inspect` parse module + criteria; the not-found
path produces the documented teaching error with a name-similarity suggestion.
