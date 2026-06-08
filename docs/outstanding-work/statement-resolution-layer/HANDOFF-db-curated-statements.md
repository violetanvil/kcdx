# Hand-off — ship the curated-function statement subset into the production reference DB

**To:** the DB / extractor lane (owner of `data/seeds/`, `data/refdata-extractor/`,
`data/reference.sqlite` — the same lane that owns the seed CSVs + `import_to_sqlite.py`).
**From:** the engine / consumer lane.
**Status:** requirements, not started. This is **step 1** of the
`statement-resolution-layer` prerequisite; the engine-side refdb API (step 2) is
built in parallel against the column contract pinned below.

---

## 1. Why (the one-paragraph grounding)

The Phase 9.3 author surface (`kcdx.locator.*` / `kcdx.op.*` / `kcdx.statement.*`)
resolves an author's locator — e.g. `kcdx.locator.first_call_to("IsInCombat")` — to
a **statement within a function** at runtime, and reads that statement's `byte_range_len`
(same-size-rewrite-vs-trampoline pick) and its captured variables (insert-callback
captures-by-name). The Phase 9.3 design states the shipped DB carries
"names + signatures + **statement metadata**" populated eagerly at engine startup
(`docs/outstanding-work/restructure/00-original-plan.md` §"Phase 9.3", the
`kcdx.functions.WHGame.*` paragraph + the `kcdx.op.*` paragraph). The production
`data/reference.sqlite` does **not** currently carry statement data — the
`statements` / `referenced_vars` tables are DEV-only today (`USER_TABLES` excludes
them). That exclusion was scoped to the **5.24M-row bulk** discovery corpus (correctly
DEV-only, Phase 9.4's `kcdx.find` job). It over-reached to the **curated subset** the
Phase 9.3 surface needs. This step ships the curated subset only.

**This is not a new design decision** — it reconciles the Phase 9.1 USER projection
back to the Phase 9.3 design it serves. The bulk stays DEV-only; the curated
133-function slice ships.

## 2. What ships (the deliverable)

`data/reference.sqlite` (the USER projection) gains two tables, **curated-subset
rows only**, **minimal runtime columns only**:

- `statements` — only rows whose `address_version_id` belongs to a curated entity
  (`address_versions.kcdx_id IS NOT NULL`, the same curated set the USER projection
  already row-filters `address_versions` to).
- `referenced_vars` — same curated row-filter.

`call_edges` stays **DEV-only** (Phase 9.4 `kcdx.find` consumer; not needed by the
Phase 9.3 runtime surface).

**Verified scope (measured against the current DEV DB, game version
`release_1_5_1164953_841`):**

- Curated entities: **157** `address_names` rows → **157** curated `address_versions`.
- Of those, **133** have statement coverage (the other ~24 are non-function kinds —
  vtable slots / data slots — that legitimately carry no statements).
- Curated `statements` rows: **2,385**.
- Curated `referenced_vars` rows: **5,595**.
- Size delta to `reference.sqlite`: **~0.44 MB** with the column set below (the pinned
  set drops `content_hash` + `kcdx_id`; it KEEPS `pseudo_text`, which two runtime
  locator forms need — see §3).

## 3. The PINNED column contract (load-bearing — the engine reads exactly these)

Ship **only** these columns in the USER projection. The dropped columns
(`content_hash`, `kcdx_id` on these two tables) are not read by the runtime resolution
path. `pseudo_text` IS kept (it backs two runtime locator forms — see the table note).
This is the contract the engine-side refdb API (step 2) is written against; changing it
is a contract renegotiation.

### `statements` — USER columns

| Column | Type | Why the engine needs it |
|---|---|---|
| `id` | INTEGER PK | row identity |
| `address_version_id` | INTEGER FK | the join key (→ `address_versions.id`); the handle a resolved function's statements are fetched by |
| `idx` | INTEGER | statement ordering within the function — `first_*`/`last_*` locator resolution |
| `kind` | INTEGER (dict) | locator `kind=` match + op kind-mismatch teaching error (1=store 2=call 3=return 4=branch 5=assign 6=other 7=none) |
| `callee` | TEXT | `first_call_to(fn)` / `call_to(fn)` / `matching{callee=}` resolution |
| `string_ref` | TEXT | `references_string` / `first_read_of_cvar` resolution |
| `byte_range_start` | INTEGER | the statement's byte offset (apply-site) |
| `byte_range_len` | INTEGER | the same-size-rewrite-vs-trampoline fit decision (`kcdx.op.*`) |
| `pseudo_text` | TEXT | the SOLE backing of two §9.3 runtime locator forms: `return_value(v)` (the return operand) + `matching{condition_contains=}` (the branch condition text). Verified: 0 of 142 curated branch statements carry the condition outside `pseudo_text`; 0 of 171 return statements carry the value outside it. Dropping it strands both forms. |

**Dropped from USER:** `content_hash` (per-statement survival hash — not a Phase 9.3
runtime need), `kcdx_id` (nullable convenience column; the engine joins via
`address_version_id`).

> **On `pseudo_text`:** it is heavy human-readable decompile text, and for the **5.24M
> bulk** it is correctly discovery-only (Phase 9.4 `kcdx.find`). But the **curated
> subset's** `pseudo_text` backs two *runtime* `kcdx.locator.*` common-path forms, so it
> ships for the curated subset. This is the same bulk-vs-runtime distinction this whole
> step embodies — "discovery-only" applies to the bulk, not to the curated runtime data.

### `referenced_vars` — USER columns

| Column | Type | Why the engine needs it |
|---|---|---|
| `id` | INTEGER PK | row identity |
| `address_version_id` | INTEGER FK | the join key (→ `address_versions.id`) |
| `statement_idx` | INTEGER | links a captured var to its statement (`statements.idx`) — the captures-by-name thunk |
| `var_name` | TEXT | the capture's author-facing name (`captures.<name>`) |
| `storage_kind` | INTEGER (dict) | register / memory / const / stack / … — where the capture lives |
| `storage_detail` | TEXT | the concrete register/offset the thunk copies from/to |
| `size_bytes` | INTEGER | the capture's width |
| `data_type` | INTEGER (dict) | the capture's type (marshalling) |

**Dropped from USER:** `kcdx_id` (join via `address_version_id`).

> Both tables' dict columns (`statements.kind`, `referenced_vars.storage_kind`,
> `referenced_vars.data_type`) reference the `_dict_*` lookup tables — those dict
> tables are **already present** in `reference.sqlite` today (the USER projection
> already materializes them). No new dict work.

## 4. Where the change lives (the implementation, your lane)

The machinery is already present — this is an inclusion + a row-filter extension, not
new infrastructure:

1. **`data/refdata-extractor/python/seeds_shared/schema.py`**
   - Add `"statements"` and `"referenced_vars"` to **`USER_TABLES`** (currently
     `["modules", "game_versions", "address_names", "address_versions", "meta"]`).
   - Add **`USER_COLUMNS["statements"]`** and **`USER_COLUMNS["referenced_vars"]`** with
     exactly the column lists in §3 (the projection's column-filter reads `USER_COLUMNS`).
   - Leave `call_edges` out of `USER_TABLES` (stays DEV-only).

2. **`data/refdata-extractor/python/import_to_sqlite.py`** — `write_db()` /
   `filter_rows()`
   - `filter_rows` already narrows `address_versions` to curated rows when
     `user_projection=True`. Extend it so `statements` and `referenced_vars` are
     **also** narrowed to rows whose `address_version_id` is in the curated
     `address_version` set. (The curated av-id set = the ids of the curated-`kcdx_id`
     `address_versions` rows the projection already keeps. Build that id set once in
     `write_db` and filter both statement tables against it.)
   - The index-creation branches for `statements` / `referenced_vars` **already exist**
     in `write_db` (the `if "statements" in tables:` / `if "referenced_vars" in tables:`
     blocks) — they fire automatically once the tables are in `USER_TABLES`. The
     engine's lookup paths are `ix_st_av` on `(address_version_id, idx)` + `ix_rv_av`
     on `address_version_id`.
   - **Reconcile the `kcdx_id` indexes against the dropped column.** Those branches also
     create `ix_st_kcdx` on `statements(kcdx_id)` and `ix_rv_kcdx` on
     `referenced_vars(kcdx_id)` — but the pinned contract DROPS `kcdx_id` from the USER
     projection of both tables. Creating an index on a non-existent column fails. Guard
     those two index creations to the DEV projection only (skip them when
     `user_projection=True`), OR keep `kcdx_id` if the lane prefers (the contract drops
     it as redundant, not as forbidden — but the engine joins via `address_version_id`,
     so the `kcdx_id` index buys nothing at the USER tier). Either resolves the
     mismatch; the agent's call which.

3. **Rebuild** `data/reference.sqlite` via the extractor's normal rebuild path and
   commit the regenerated DB (per the lane's usual DB-regeneration + AP18/seed
   discipline — note this adds **no new seed rows**; it re-projects existing data, so
   it is not an AP18 entity addition).

## 5. The acceptance contract (how "done" is proven — your side)

A DB-shape assertion, runnable headless (extractor + a sqlite check, no engine, no
game launch). The agent owns this test; emit the canonical acceptance signal
(`.claude/rules/acceptance-signal.md` — `ACCEPT-RESULT` / `ACCEPT-SUITE`) into the
DB-pipeline's test sink. Assert, against the rebuilt `data/reference.sqlite`:

1. `statements` and `referenced_vars` tables EXIST in `reference.sqlite`.
2. Each has **exactly** the columns in §3 — `statements` INCLUDES `pseudo_text` (it
   backs the `return_value(v)` + `condition_contains=` locator forms); `content_hash`
   and `kcdx_id` are ABSENT from both tables (the pinned contract; a stray column, OR a
   MISSING `pseudo_text` on `statements`, is a contract-drift FAIL).
3. Row counts match the curated subset: `statements` ≈ **2,385**, `referenced_vars`
   ≈ **5,595** (allow the count to move only if the underlying curated set or the
   dump changes — pin the expectation to "= the curated-`address_version_id` subset of
   the DEV tables", computed from the same dump, not a frozen literal).
4. **133** distinct `address_version_id`s have ≥1 statement (the curated functions with
   coverage).
5. The 5.24M bulk is ABSENT: `SELECT COUNT(*) FROM statements` is the curated count,
   **not** the millions — a single assertion that the bulk did not leak into USER.
6. `call_edges` is ABSENT from `reference.sqlite` (stays DEV-only).

A self-checking equivalence assertion is the strongest form: the USER `statements`
row set == `{DEV statements WHERE address_version_id ∈ curated-av-ids}`, projected to
the §3 columns. If that set-equality holds, every count above holds by construction.

## 6. The contract boundary with the engine lane (what I build in parallel)

I (engine lane) build **step 2** — the refdb statement-resolution API — against the
§3 column contract, in parallel with this step. I do **not** need step 1 finished to
write the resolution code (the schema already exists in the DEV DB; I develop against
a local DEV-DB copy / a curated fixture). What I block on is the **final runtime
acceptance**: my `cap-NN-stmt-resolve` test plugin reads the shipped
`data/reference.sqlite` in-game, so its suite-green acceptance needs this step
**deployed** (the curated rows present in the shipped DB).

**The boundary is this column contract.** As long as §3 ships verbatim — exact tables,
exact columns, curated-row-filter, dict tables present — the two sides meet with no
further coordination. A deviation (a missing column, the bulk leaking in, a renamed
column) breaks my resolution read and is a contract FAIL on your acceptance test
(§5.2 / §5.5), caught before it reaches my side.

## 7. Out of scope for this hand-off

- The refdb API, the Lua/C++ binders, the `kcdx.locator.*` / `kcdx.op.*` /
  `kcdx.statement.*` surface, the `cap-NN-stmt-resolve` runtime test — **engine lane
  (step 2), mine.**
- `call_edges` and the 5.24M bulk `statements` — **stay DEV-only** (Phase 9.4).
- Any new curated entity / seed row — **none.** This re-projects existing dumped data;
  no AP18 entity addition.
- The Phase 9.1 README "DEV-only" doc reconcile + the §9.3 step-doc
  `statements.captures`→`referenced_vars` correction — **step 3** of this prerequisite
  (whoever lands last, or split DB-doc ↔ engine-doc).

## 8. Source references

- Design authority: `docs/outstanding-work/restructure/00-original-plan.md` §"Phase 9.3"
  (the `kcdx.functions.WHGame.*` eager-population paragraph; the `kcdx.locator.*`,
  `kcdx.op.*`, `kcdx.statement.*` catalogs; the captures paragraph).
- The drift this corrects: `docs/outstanding-work/restructure/phase-09.1-reference-db/README.md`
  (the "statements / referenced_vars / call_edges tables are DEV-only … not consumed
  by production" line — true for the bulk, over-applied to the curated subset).
- The shipped-schema authority: `docs/outstanding-work/parallel-ghidra-research.md`
  §11.8 / §11.9 (the three-track model + the USER/DEV column split).
- The projection code: `data/refdata-extractor/python/seeds_shared/schema.py`
  (`USER_TABLES` / `USER_COLUMNS` / `DEV_TABLES`) +
  `data/refdata-extractor/python/import_to_sqlite.py` (`write_db` / `filter_rows`).
- Verified data facts in §2/§3: measured against `data/reference-dev.sqlite` and
  `data/reference.sqlite` at game version `release_1_5_1164953_841`, 2026-06-07.
