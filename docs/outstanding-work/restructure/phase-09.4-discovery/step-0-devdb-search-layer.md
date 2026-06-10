# Phase 9.4 step 0 — refdb dev-DB connection + cross-function search layer

**Status: DONE** (landed; engine foundation built + step-review GREEN). Ledger row: [`README.md`](README.md) → step 0.

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

### SQL does the work — `find` never materializes the corpus into Lua (the load-bearing principle, user-settled 2026-06-10)

**The dev DB is a SQL database; queries run IN SQL and return ONLY what is asked
for.** `kcdx.find` is a SEARCH — it runs a lean SQL query returning function HEADERS
(name / module / rva / quality + a SQL-computed `statement_count`), and STOPS. It does
NOT pull statement bodies, captures, or applicable-ops across the SQL→Lua boundary.
The statement bodies stay in the DB until the author asks for ONE function via
`kcdx_dev_inspect`, which runs its OWN scoped per-function SQL query.

This is the fix for the boot-hang root cause (KI on 2026-06-10): the original design
had `find` eagerly materialize EVERY statement of EVERY one of the (up to 500) matched
records into nested Lua tables. For a broad query (e.g. callers of a common function:
30,393 matches → 500 capped records) that is ~131,691 statement sub-tables + ~266,737
capture sub-tables ≈ 400,000 nested Lua tables built on CryEngine's Lua 5.1 on the boot
worker thread — which exhausts memory/GC and HANGS (watchdog-killed, no AV). The pure
SQL runs in ~1.5s; the stall is wholly the eager Lua materialization. The two tools
divide cleanly: **`find` = discover WHICH function** (lean headers, SQL-paged);
**`kcdx_dev_inspect` = inspect ONE function's body** (its statements, its own query).

### Result record + cap + ranking

- **`find` record (LEAN — no statement bodies):** `{function, module, rva,
  decompile_quality, statement_count}` — `function` = curated name or `auto_name`;
  `decompile_quality` decoded from the dict; `statement_count` = `SELECT COUNT(*) FROM
  statements WHERE address_version_id = ?` (computed in SQL, one cheap indexed count
  per capped record — NOT the statement rows). So 500 capped records = 500 small
  tables, never the ~400K-table blowup. The author reads the lean list, picks a
  function, and runs `kcdx_dev_inspect <module> <fn>` for ITS statements.
- The statement detail (`{idx, kind, pseudo_text, captures, applicable_ops}`) is
  `EnumerateStatements`'s / `kcdx_dev_inspect`'s result for ONE function — NOT
  `find`'s. The `applicable_ops` mapping below applies to a statement THERE.
- **`applicable_ops` per statement (user-settled 2026-06-10) = the real `kcdx.op.*`
  op NAMES whose declared required-statement-kind matches that statement's kind**,
  sourced from the shipped Phase 9.3 op catalog's per-op kind declarations
  (`src/lua_bind_op.cpp` — each op declares the statement kind(s) it applies to). The
  kind→ops mapping, from the op catalog (NOT a placeholder kind-echo):

  | statement `kind` | `applicable_ops` |
  |---|---|
  | `branch` (conditional jump) | `always_take_branch`, `never_take_branch`, `invert_branch_condition` |
  | `call` | `skip_call_void`, `skip_call_return_value`, `replace_call_target` |
  | `return` | `replace_with_return`, `replace_return_value` |
  | `assign` | `replace_assignment_value` |
  | (any statement) | `replace_with_noop` is appended to every kind above |
  | `store` / `other` / `none` | `replace_with_noop` only |

  **`applicable_ops` lists only catalog ops REACHABLE in the corpus** — the ops the
  author can ACTUALLY apply to a statement the discovery tool surfaces. The op
  `replace_compare_constant` is **deliberately omitted**: the op catalog declares it
  requires statement kind `compare` (`src/lua_bind_op.cpp` `RequiredKind::Compare`),
  but the dev DB corpus emits ZERO `compare` statements (the `statements.kind` dict is
  `store/call/return/branch/assign/other/none` — no `compare`). So no statement the
  tool can ever surface matches it; listing it under any kind would make `kcdx.find`
  name a move that `kcdx.statement.replace_with`'s own kind-gate then REJECTS — the
  two surfaces contradicting each other (`.claude/rules/anti-patterns.md` AP14: the
  tool must not name a move the author cannot make). The op stays in the catalog (a
  future corpus carrying `compare` statements re-includes it); `applicable_ops` is the
  catalog ops reachable in the corpus, not the whole catalog.

  **Why:** the discovery tool TELLS the author exactly which `kcdx.op.*` ops fit a
  located site — the disassembler-test payoff (declare intent, the engine names the
  moves; `.claude/rules/cornerstones.md`). The mapping is derived from the op catalog's
  kind declarations (`src/lua_bind_op.cpp`'s kind-check) INTERSECTED with the kinds the
  corpus actually emits — never guessed. The list is op NAMES the author then uses
  verbatim in `kcdx.statement.replace_with(...)`. (`replace_with_noop` applies to any
  statement, so it is always present; the kind-specific ops are added per the table.)
  The authority for the per-op required-kind is the op catalog itself — step 0
  reads/mirrors that table for the kinds the corpus carries, it does not re-invent
  which op fits which kind. If a future op is added to the catalog (or the corpus gains
  a new kind), this mapping is updated to match.
- **Cap 500:** over-500 → first 500 + `_truncated = true` + `_total_matches = N`
  (loud, not silent).
- **Ranking (user-settled 2026-06-10):** `ORDER BY decompile_quality ASC, rva ASC`
  — best-decompiled first (the readable matches a dev author wants), deterministic
  tiebreak by address. **The quality codes are `1` = clean (readable) and `2` =
  unanalyzable**, so ASC (not DESC) puts clean above unanalyzable — `1` < `2`. (The
  original spec said `DESC`, which inverted the intent: DESC would rank the 5
  unanalyzable functions ABOVE the clean ones. Corrected to ASC 2026-06-10 — step-review
  caught the spec-vs-intent contradiction; `.claude/rules/spec-conformance.md`.)

### `EnumerateStatements(fn)` — for `kcdx_dev_inspect` (step 2)

Resolve `fn` (curated name or `auto_name`) → its `address_version_id` → its
idx-ordered statements (kind / pseudo_text / captures / applicable-ops). The
not-found path returns a **name-similarity suggestion** (step 2 renders the teaching
error).

**Name-similarity = Levenshtein edit-distance ranked (user-settled 2026-06-10).** On a
not-found `fn`, rank candidate names by edit-distance to `fn` (nearest first), return
the top suggestion(s). Candidate set = the curated `address_names.name` set (157) +
`auto_name` matches; realistically the suggestion helps on the curated set (a typo of a
curated name) since `auto_name` is `FUN_<rva>`. **Why:** edit-distance ranks a 1-char
typo first — the documented example `IsInCombatt` → `Did you mean: IsInCombat?`
(distance 1) resolves correctly, which substring matching would miss when the typo is
not a substring of the real name. A small Levenshtein helper, run ONLY on the
not-found path (cold — not a hot path, `.claude/rules/memory.md` does not bind here).

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
