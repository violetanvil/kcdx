# Phase 9.4 step 1 — `kcdx.find{...}` Lua surface + `kcdx_find` console

**Status: DONE** (landed; Lua binder + console + docs, step-review GREEN). Ledger row: [`README.md`](README.md) → step 1.

## What

`kcdx.find(criteria_table)` — discover a function from what the author already
knows. Plus the `kcdx_find` console command invoking the same path.

**`kcdx.find` / `kcdx_dev_inspect` are DEV TOOLS.** They search the DEV reference
DB (`reference-dev.sqlite` — the FULL game corpus: all statements + referenced_vars
+ `call_edges`), NOT the shipped `reference.sqlite` (the curated 133-function subset,
no `call_edges`). Discovery is an AUTHORING-TIME activity: the author finds a function
in dev mode, then writes `kcdx.statement.*` / `kcdx.locator.*` code against it. The
shipped product does not carry the dev DB; a `find` in a non-dev install fails
gracefully with a teaching message (below). This is the settled resolution of the
find-corpus question (user-decided 2026-06-10) — the surface searches the full game
because it reads the dev corpus, and all six criteria are serveable (`call_edges`
backs `callers_of` / `callee_in_subsystem`). See [`TD-0006`](../../../tech-debt/TD-0006-statement-layer-in-user-db.md)
(the DEV-only statement-layer split this decision rests on).

## Scope (`src/lua_bind_find.cpp`)

- **Consumes step 0's `refdb::FindFunctions(criteria)` search surface** — this step is
  the Lua/console binder over it, NOT the search engine (that is step 0). The dev-DB
  connection + the per-criterion query strategy live in step 0
  ([`step-0-devdb-search-layer.md`](step-0-devdb-search-layer.md) §"Settled
  search-layer design").
- `kcdx.find(criteria_table)` — table REQUIRED (the at-least-one-of-N case),
  validated at parse-time to contain at least one criterion. Criteria: `string`,
  `cvar`, `callers_of`, `callee`, `name_contains`, `callee_in_subsystem` (all
  serveable from the dev DB; `callee` / `callers_of` / `callee_in_subsystem` resolve
  via `statements.callee` TEXT match — `call_edges` is unused, step 0 §design).
- Resolves through step 0's **dev-DB** surface (the connection at
  `<game-bin>/kcdx-engine/data/reference-dev.sqlite` — a separate ~1.3GB artifact, NOT
  in the release zip — opened read-only by step 0, separately from the shipped
  `reference.sqlite` the production resolver uses).
- Returns a Lua table of records: `{function, module, rva, decompile_quality,
  statements = [{idx, kind, pseudo_text, captures, applicable_ops}]}`.
- **Dev-mode + dev-DB gate (graceful, fail-loud).** When dev mode is OFF
  (`kcdx::log::IsDevModeEnabled()` is false) OR the dev DB is absent at the path
  above, `kcdx.find` logs the teaching message (below) AND **returns `{}`** — the
  same empty-result contract as a no-match, so mod code's `if #results == 0` path
  runs harmlessly. NEVER a Lua error, NEVER a crash (a shipped mod that calls `find`
  in a player's non-dev install must not break). The console `kcdx_find` prints the
  same teaching message. The teaching message:
  ```
  [kcdx.find] dev tool unavailable. kcdx.find / kcdx_dev_inspect need dev mode
  AND the dev reference DB:
    1. set dev_mode = true in <game-bin>/kcdx-engine/engine.toml
    2. place reference-dev.sqlite (a separate download, NOT in the release zip)
       at <game-bin>/kcdx-engine/data/reference-dev.sqlite
  These are authoring tools — discover a function here, then write your
  kcdx.statement.* / kcdx.locator.* code against it.
  ```
- **No matches (dev DB present):** returns `{}` (idiomatic `if #results == 0`). No
  nil, no error. (Indistinguishable in shape from the gated-off empty above; the
  teaching log line is the discriminator a dev author sees.)
- **Result cap 500:** over-500 searches return the first 500 + `_truncated = true`
  + `_total_matches = N`. Loud truncation, not silent.
- Console `kcdx_find` — module as first positional (the module-required rule), then
  criteria flags: `kcdx_find WHGame.dll --string "test_marker"`. Same Lua path,
  same gate, same teaching message on the gated-off path.

## Test bar

Step 3. Self-check (dev mode on, dev DB present): a known-string find returns the
expected function; empty-criteria returns `{}`; a synthetic 600-row search truncates
loudly. Gated-off self-check: with the dev DB renamed/absent, `kcdx.find` returns `{}`
and the teaching message is in the log (falsifiable: FAILS if it errors, crashes, or
returns non-empty).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4" → Scope (the dev-tool
+ dev-DB-gate decision is recorded in this step's What section, user-decided
2026-06-10).
