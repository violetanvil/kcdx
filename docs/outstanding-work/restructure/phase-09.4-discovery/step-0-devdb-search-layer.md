# Phase 9.4 step 0 — refdb dev-DB connection + cross-function search layer

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 0.

The engine FOUNDATION the discovery surfaces (steps 1/2) consume. Added 2026-06-10
after the `/feature` audit found the 3-step plan assumed a "resolves against the dev
DB" mechanism that does NOT exist: `refdb::Open()` opens only the shipped
`reference.sqlite` (one read-only connection), and refdb has NO cross-function search
surface (only resolve-by-name/id + resolve-a-locator-within-a-known-function).
`kcdx.find` needs BOTH a separate dev-DB connection AND a new search-query layer.
This step builds both; the consumers come after (`.claude/rules/incremental-delivery.md`
— producer before consumer).

## What

Two engine additions in `src/refdb.{h,cpp}`:

1. **A lazily-opened, dev-gated read-only connection to the DEV DB**
   (`reference-dev.sqlite`, the full 1.3 GB corpus), SEPARATE from the shipped-DB
   connection `g_db`.
2. **A cross-function search surface** (`FindFunctions(criteria)` +
   `EnumerateStatements(fn)`) the Lua/console binders call.

## Settled search-layer design (the design authority steps 1/2 build to)

User-settled 2026-06-10 (this step's design source; surface-shape + dev-tool/gate
decisions are in [`step-1-find-surface.md`](step-1-find-surface.md) §What).

### Connection model

- `g_devDb` — a second `sqlite3*`, opened **lazily on the first find/inspect call**,
  NOT at `refdb::Open()` (the 1.3 GB dev DB must not open in production).
- **Gate (both required):** dev mode ON (`kcdx::log::IsDevModeEnabled()`) AND the file
  present at `<game-bin>/kcdx-engine/data/reference-dev.sqlite`. Gate fails → the
  caller gets the dev-tool-unavailable signal (step 1's teaching message / `{}`).
- Opened `SQLITE_OPEN_READONLY` + the same `meta.schema_version` gate the shipped DB
  uses (fail-loud on mismatch). The shipped `g_db` connection is untouched.

### The data shape (ground truth, dev DB inspected 2026-06-10)

- `address_versions`: **321,144 rows** (one per function) — `auto_name` (`FUN_<rva>`)
  + `signature` on essentially all; `kcdx_id` non-NULL on only **157** (the curated
  set). So a result function's display name = the curated `address_names.name` when
  `kcdx_id` is set, else `auto_name`.
- `statements`: 5.24M rows — `kind` (dicted: store/call/return/branch/assign/other/
  none), `pseudo_text`, `byte_range_*`, `callee` (TEXT, 1.20M non-empty), `string_ref`
  (TEXT, 23k non-empty), keyed by `address_version_id`.
- `referenced_vars`: 10.88M rows (captures). `call_edges`: 1.29M rows — keyed by
  `caller_kcdx_id`/`callee_kcdx_id`, **NULL for 320,987 of 321,144 functions**
  (curated-only).

### `FindFunctions(criteria)` — per-criterion query

A function = one `address_versions` row. Each criterion yields a set of owning
`address_version_id`s; multi-criterion = **AND** (intersect the sets).

| Criterion | Query |
|---|---|
| `string` | `statements.string_ref = ?` → distinct `address_version_id` |
| `cvar` | `statements.string_ref = ?` (cvar names live in `string_ref`; same path as `string`, surfaced as the cvar-typed lens) |
| `name_contains` | `address_names.name LIKE %?%` (curated) UNION `address_versions.auto_name LIKE %?%` |
| `callee` | `statements.callee = ?` → owning functions (TEXT, **full 321k coverage**) |
| `callers_of` | `statements.callee = ?` → owning functions (the callers of `?`; full coverage via TEXT) |
| `callee_in_subsystem` | `statements.callee LIKE <prefix>%` → owning functions |

**`call_edges` is UNUSED** (user-confirmed 2026-06-10) — keyed by `kcdx_id`, NULL for
320,987 functions, so it returns only the 157 curated functions; useless for
discovery. The TEXT `statements.callee` is the full-coverage caller path. (Recorded
so a future reader does not "fix" the omission — it is deliberate, the data shape
forces it.)

### Result record + cap + ranking

- Record: `{function, module, rva, decompile_quality, statements = [{idx, kind,
  pseudo_text, captures, applicable_ops}]}` — `function` = curated name or `auto_name`;
  `decompile_quality` decoded from the dict; `statements` from `EnumerateStatements`.
- **Cap 500:** over-500 → first 500 + `_truncated = true` + `_total_matches = N`
  (loud, not silent).
- **Ranking (user-settled 2026-06-10):** `ORDER BY decompile_quality DESC, rva ASC`
  — best-decompiled first (the readable matches a dev author wants), deterministic
  tiebreak by address.

### `EnumerateStatements(fn)` — for `kcdx_dev_inspect` (step 2)

Resolve `fn` (curated name or `auto_name`) → its `address_version_id` → its
idx-ordered statements (kind / pseudo_text / captures / applicable-ops). The
not-found path returns a name-similarity suggestion (step 2 renders the teaching
error).

## Scope (`src/refdb.{h,cpp}`)

- `OpenDevDb()` (lazy, gated, read-only, schema-checked) + `g_devDb` lifecycle +
  `CloseDevDb()` (idempotent; clears on `Close()`).
- `struct FindCriteria` (the 6 optional fields + at-least-one validation is the
  binder's, step 1) + `struct FindRecord` + `FindFunctions(const FindCriteria&)`
  returning records + the `_truncated`/`_total_matches` signal.
- `EnumerateStatements(const std::string& fn)` → the statement list (reuses the
  resolve-by-name/auto_name lookup).
- Fail-loud everywhere (`.claude/rules/logging.md`): a gate-off / missing-DB / query
  error logs a structured reason token, never a silent empty.

## Test bar

A `cap-NN-find-discovery` engine self-test row (the same plugin step 3 grows): with
dev mode on + the dev DB present, `FindFunctions({string="<known>"})` returns the
expected owning function; an over-500 synthetic search truncates loudly; the gated-off
path (dev DB renamed) returns the unavailable signal, never a crash. Falsifiable:
FAILS if the dev DB connection opens in production, if a search returns a wrong/empty
set for a known input, or if the gate-off path errors instead of signaling.

## Dependencies

None new — the dev DB + extractor exist (`reference-dev.sqlite`). This step is the
producer; steps 1/2/3 consume it. Ordered FIRST (`.claude/rules/incremental-delivery.md`).

## Design authority

This doc §"Settled search-layer design" (user-settled 2026-06-10) +
[`step-1-find-surface.md`](step-1-find-surface.md) §What (the dev-tool/gate decision).
Build to the settled query strategy here, not a re-derived one.

## RE / author-burden note

No game-binary RE — the dev DB is a derived artifact already on disk; this is a query
layer over it. No new Address Library DB rows. No author hex (a dev tool the author
queries by what they know).

## Reference

[`../00-original-plan.md`](../00-original-plan.md) §"Phase 9.4"; the search design is
this doc.
