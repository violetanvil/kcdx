# Phase 9.4 — `kcdx.find{...}` discovery + `kcdx_dev_inspect` console

**Status: NOT STARTED.** Detail: [`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4".

Without discovery, statement-level modding requires Ghidra. With it, authors find
what they need from what they already know about the game (a string they saw, a
CVAR they read about, a function they suspect). No new author-facing concepts beyond
the existing locator + op vocabularies.

**`kcdx.find` / `kcdx_dev_inspect` are DEV TOOLS** (user-decided 2026-06-10). They
read the DEV reference DB (`reference-dev.sqlite` — the FULL game corpus, all
statements + `call_edges`), NOT the shipped curated `reference.sqlite`. Discovery is
authoring-time: find a function in dev mode, then write `kcdx.statement.*` code
against it. The dev DB is a separate ~1.3GB artifact at
`<game-bin>/kcdx-engine/data/reference-dev.sqlite`, NOT in the release zip. Both
surfaces are gated on dev mode + the dev DB's presence; the gated-off path is
graceful (the Lua `kcdx.find` returns `{}` + logs a teaching message; the console
commands print it), never a crash or unhandled error. This resolves the find-corpus
question — searching the full game is possible because the surface reads the dev
corpus. The decision is recorded in [`step-1-find-surface.md`](step-1-find-surface.md)
§What; the DEV-only statement-layer split it rests on is
[`TD-0006`](../../../tech-debt/TD-0006-statement-layer-in-user-db.md).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| [1 — `kcdx.find{...}` Lua surface + `kcdx_find` console](step-1-find-surface.md) | NOT STARTED | — |
| [2 — `kcdx_dev_inspect` console command](step-2-dev-inspect-console.md) | NOT STARTED | — |
| [3 — test plugin + console-path tests](step-3-test.md) | NOT STARTED | — |

## Verification gate (whole phase)

With dev mode on + the dev DB present: `kcdx.find({string = "test_marker"})` against
the dev DB with a known function returns the expected name + statement list.
Empty-criteria → `{}`. A synthetic 600-row search returns 500 records +
`_truncated = true` + `_total_matches = 600`. Console `kcdx_find` /
`kcdx_dev_inspect` parse module + criteria; the not-found path produces the
documented teaching error with a name-similarity suggestion. **Dev-gate path:** with
the dev DB absent (or dev mode off), `kcdx.find` returns `{}` + logs the dev-tool-
unavailable teaching message and the console commands print it — graceful, never an
error or crash.
