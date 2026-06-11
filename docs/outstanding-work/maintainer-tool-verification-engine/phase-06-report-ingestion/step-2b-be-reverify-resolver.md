# 6.2b [BE/CORE] The re-verify resolver + the `/save/reverify-batch` preview seam (D39)

## What

Build the **data-core re-verify resolve seam** D39 settled: the data-core computes the bulk re-verify
edit-specs FROM the v3 report (the FE sends report rows, never pre-computed edits). Two units land in
this step: **(1) a new data-core module `reverify_resolver.py`** that takes the report's actionable rows
(`kcdx_id`, resolved `version`, `verdict`, `method_rank`, `matched_address_version_id`) and produces the
per-row edit-specs in the `{kcdx_id, valid_from_version, edits}` shape `/confirm/batch` (6.2) already
consumes; and **(2) a new backend preview endpoint `/save/reverify-batch`** that calls the resolver and
returns the per-row **field-deltas** (`field: old → new`) for the FE to display — **writing nothing,
opening no transaction** (the Save-previews / Confirm-transacts model, D16/law 5; mirrors
`routes_save.py`'s preview-only contract). The existing `/confirm/batch` (6.2) transacts the returned
edits UNCHANGED. The write MECHANISM is unchanged (D19) — only the edit-spec SOURCE moves from the FE to
this resolver.

The resolver's two paths:
- **Verify-all** (`verified_working` / `passed_not_verified` rows): read the matched row BY
  `matched_address_version_id` → its `{kcdx_id, valid_from_version}` (the write-path key) + its current
  `valid_through` / `last_verified_at_version`; compute the **audit trio** (`last_verified_at_version` →
  the report version, `verified_date` → today, `verified_by` → the injected identity — D17a), the
  **proof-rank-keyed `evidence_kind`** (a rank-1 `verified_working` row → `live_production`; a ranks-2–5
  `passed_not_verified` row → `live_test_plugin` — D29-rev), and the **D34 gap-extension** (`valid_through`
  → the report version when the swept version sat beyond the matched row's interval; a row already
  covering it — `last_verified_at_version >= swept` or interval covers it — is SKIPPED, nothing to add).
- **Close-intervals** (`failed` rows — `matched_address_version_id` is NULL by the report's attribution
  invariant): resolve the target row DETERMINISTICALLY as the `address_version` row of `kcdx_id` whose
  interval **contains** the resolved `version` (a `failed` row's version was definitionally covered — a
  build version in a GAP is `not_applicable`, NOT `failed`, and `not_applicable` is shown-no-action, never
  in the failing block — so exactly one row contains it, intervals being non-overlapping per entity);
  compute `valid_through` → that row's `last_verified_at_version` (the D35 retract).
- **Verdict routing**: handle EXACTLY `{verified_working, passed_not_verified}` → verify-all and `failed`
  → close-intervals; the no-action set (`not_applicable` / `cannot_check` / `skipped` / `error`) produces
  NO edit-spec (shown-no-action, D28/D36).

## Scope

One commit in the kcdx tree:
- **`data/refdata-extractor/python/seeds_shared/reverify_resolver.py`** (NEW data-core module) — the
  resolve + compute logic above. It reads the DB (the matched row by id; the close-target by
  `kcdx_id`+interval-containment; the current `valid_through`/`last_verified_at_version` for the delta) and
  returns the edit-specs. No write (the resolver computes; `_apply_one_db`/the batch path writes — law 6).
- **`data/maintainer-tool/backend/app/routes_save.py`** (or a sibling route module) — the
  `/save/reverify-batch` POST endpoint: a request body carrying the author context + the report's
  actionable rows + the action (verify-all | close-intervals); routes through `reverify_resolver`; returns
  the per-row field-deltas (the `batch field-delta list` shape the FE renders). PREVIEW-ONLY: no write, no
  transaction (the same contract as the single-edit `/save/*` previews).
- The request/response Pydantic models for the new endpoint.

Does NOT change `/confirm/batch` (6.2 — transacts unchanged), the FE (6.3), or the write mechanism (D19).

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/`) for `reverify_resolver`: the resolve + compute
  over a fixture DB — verify-all produces the correct trio + the **proof-rank-keyed `evidence_kind`** (a
  `verified_working` row → `live_production`; a `passed_not_verified` row → `live_test_plugin` — assert
  BOTH) + the D34 gap-extension on a gap-pass row (and SKIPS an already-covered row); close-intervals
  resolves the deterministic interval-containing target by `kcdx_id`+`version` and produces the D35
  `valid_through` → `last_verified_at_version` retract; the verdict routing emits edit-specs for exactly
  the actionable set + NONE for the no-action set. Emits the canonical `ACCEPT-RESULT`/`ACCEPT-SUITE` to
  the DB-pipeline sink. **FALSIFIABLE:** a test that asserts a `passed_not_verified` row → `live_production`
  (the WRONG mapping the soundness gate caught) must FAIL; a close-target resolved to a row whose interval
  does NOT contain the version must FAIL.
- **backend test** (`data/maintainer-tool/backend/tests/`): `/save/reverify-batch` POSTs the report rows,
  returns the per-row field-deltas, and **writes NOTHING / opens no transaction** (assert the DB is
  byte-identical after the preview — a preview never touches the DB). Emits the canonical ACCEPT signal.
  FALSIFIABLE: a preview that mutates the DB fails the row.

Runnable AT this step (the report schema, the `address_versions` table, the `_apply_one_db` batch path,
and `/confirm/batch` all exist; this step adds the resolve+preview half). Per
`.claude/rules/test-discipline.md`, `.claude/rules/incremental-delivery.md`,
`.claude/rules/headless-testable.md` (the resolver is a headless data-core module, tested without the
FastAPI layer; the endpoint test exercises the thin route).

## Dependencies

- **6.2** — the `/confirm/batch` endpoint + `update_version_rows_batch` (the transact-half + the
  `{kcdx_id, valid_from_version, edits}` batch shape the resolver's edit-specs target). Built before this
  so the resolver produces the shape an existing endpoint consumes.
- The existing `routes_save.py` preview-only contract (the pattern `/save/reverify-batch` mirrors) + the
  data-core read seam (the resolver reads the matched/target row + current values).

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (report ingestion) + cross-step invariants 5 (all-or-nothing
batch rollback) + 6 (data-core sole writer).

## Design authority

`data/maintainer-tool/design.md` **D39** ("the bulk re-verify writes are RESOLVED + computed by the
data-core from the report, not the frontend — a new `reverify_resolver` + a `/save/reverify-batch` preview
seam") + **D28** ("the tool computes the writes") + **D29** (rev — the proof-rank-keyed `evidence_kind`) +
**D34** (the gap-extension via `matched_address_version_id`) + **D35** (the close-intervals retract) +
**D16** (Save-previews / Confirm-transacts — the preview-only contract) + **D19** (the write mechanism
unchanged) + **§7** "Batch mutation". Build to D39's settled seam (the resolver module + the preview
endpoint + the unchanged `/confirm/batch`), not this doc's summary.

## UX

Not a UI step — a data-core module + a backend preview endpoint. The s08 batch-confirm UX (the `batch
field-delta list` the preview's deltas feed, the toast, the in-place update) is step 6.3, which drives this
endpoint. The save-spine git commit/push stays invisible plumbing (the endpoint returns deltas, never a
hash; the transact half is `/confirm/batch`).

## Disassembler-test / author-burden

None — a save-spine resolve+preview seam; no author-facing input, no game-function target, no AP18
addition (both batch actions are all-UPDATE; a new/variant row is the separate per-row create flow).
