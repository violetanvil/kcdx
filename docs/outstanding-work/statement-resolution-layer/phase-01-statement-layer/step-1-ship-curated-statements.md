# Step 1 — ship the curated statement subset into `reference.sqlite` (DB lane)

**Status: NOT STARTED.** Ledger row: [`README.md`](README.md) → step 1.
**Lane: DB / extractor.** The complete requirements live in the hand-off:
[`../HANDOFF-db-curated-statements.md`](../HANDOFF-db-curated-statements.md) — this
step doc is the ledger handle; the hand-off is its spec.

## What

Add `statements` + `referenced_vars` to the USER projection of `data/reference.sqlite`,
**curated rows only** (rows whose `address_version_id` belongs to a curated entity),
with **exactly the pinned column set** (the column contract in
[`../plan-spec.md`](../plan-spec.md) §"The column contract" — this KEEPS
`statements.pseudo_text`, which backs the `return_value(v)` + `condition_contains=`
locator forms). The 5.24M-row bulk `statements` and all of `call_edges` stay DEV-only.
Rebuild and commit the regenerated `data/reference.sqlite` (~0.44 MB larger). The
machinery already exists — this is an inclusion + a row-filter extension (+ one
index-guard reconcile for the dropped `kcdx_id` column, hand-off §4), not new
infrastructure.

## Scope

- `data/refdata-extractor/python/seeds_shared/schema.py` — add `statements` /
  `referenced_vars` to `USER_TABLES`; add their `USER_COLUMNS` lists per the contract.
- `data/refdata-extractor/python/import_to_sqlite.py` — extend `write_db` /
  `filter_rows` so both statement tables are narrowed to the curated
  `address_version_id` set (the same set `address_versions` is already filtered to).
  The index branches for both tables already exist in `write_db`.
- Rebuild `data/reference.sqlite` via the extractor's normal rebuild path; commit it.

Full per-file detail + the exact column lists: the hand-off §3–§4.

## Test bar (runs AT this step)

A headless DB-shape assertion (extractor + sqlite check, no engine, no game launch),
emitting the canonical acceptance signal (`.claude/rules/acceptance-signal.md`) to the
DB-pipeline's test sink. Against the rebuilt `data/reference.sqlite`:

- `statements` + `referenced_vars` EXIST with EXACTLY the pinned columns — `statements`
  INCLUDES `pseudo_text` (it backs two locator forms); a stray `content_hash` /
  `kcdx_id`, or a MISSING `pseudo_text`, is a contract-drift FAIL.
- Curated row counts match (`statements` ≈ 2,385, `referenced_vars` ≈ 5,595, computed
  as the curated-`address_version_id` subset of the DEV tables — not a frozen literal).
- 133 distinct `address_version_id`s have ≥1 statement.
- The 5.24M bulk is ABSENT (the count is the curated count, not the millions).
- `call_edges` is ABSENT from `reference.sqlite`.

The strongest form: assert set-equality — USER `statements` ==
`{DEV statements WHERE address_version_id ∈ curated-av-ids}` projected to the pinned
columns. The hand-off §5 specifies this.

## Dependencies

None — this is the foundation step. The data, schema, extractor, and curated set all
exist today; this re-projects them.

## Design authority

[`../plan-spec.md`](../plan-spec.md) §"The column contract" + §"Settled design
decisions" (decisions 1 + 3) + the hand-off
[`../HANDOFF-db-curated-statements.md`](../HANDOFF-db-curated-statements.md) §2–§5.
The Phase 9.3 design source:
[`../../restructure/00-original-plan.md`](../../restructure/00-original-plan.md)
§"Phase 9.3" (the eager-populate-from-`reference.sqlite` + statement-metadata
paragraphs). The shipped-schema authority:
[`../../parallel-ghidra-research.md`](../../parallel-ghidra-research.md) §11.8 / §11.9.
Build to those, not this summary.

## Disassembler-test / author-burden note

Author-invisible — a DB-projection change. No author-facing surface, no hex.
**No new seed rows / no AP18** — this re-projects existing dumped data; it adds no
curated entity. Not an Address Library row addition.
