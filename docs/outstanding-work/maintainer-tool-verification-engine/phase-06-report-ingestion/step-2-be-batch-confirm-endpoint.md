# 6.2 [BE/CORE] The D32 batch-confirm transaction — `/confirm/batch` endpoint + the `confirmBatch` client method

## What

Build the **batch save-spine transaction** D32 settled but never implemented: a backend
`/confirm/batch` endpoint that applies **N validated edits as ONE atomic transaction** — all-UPDATE
edits composed into a single `DeferredCommit`, all-or-nothing rollback (D21: one row failing rolls
back the WHOLE batch, nothing partial lands), one git commit/push — plus the FE API-client method
(`confirmBatch`) that drives it. The single-mutation confirm endpoints
(`/confirm/update-version`, `/confirm/create-version`) and the `db_editor` `_apply_one_db` +
`DeferredCommit` primitive already exist; this step COMPOSES that primitive over N edits under one
deferred-commit transaction (it does NOT invent a new write path). This is the producer step 6.3's
verify-all / close-intervals FE wiring drives — without it, 6.3 would consume a non-existent
endpoint. **All-UPDATE only** (the bulk re-verify/close are UPDATEs to existing rows — the audit
trio, `evidence_kind`, `valid_through`); the new-row approval gate (law 8/AP18) does NOT apply to
the batch (a new/variant row is authored per-row via the existing single create flow, never in the
batch).

## Scope

- **data-core (`data/refdata-extractor/python/seeds_shared/db_editor.py`)** — a batch apply path
  that takes N edit-parameter sets, runs each through the EXISTING `_apply_one_db` validated write
  on the SAME held `DeferredCommit` connections, and returns the deferred-commit handle (so the
  caller commits once after all N validate+write). Reuses the existing per-edit validation gate
  (the shared validator runs per row) + the D21 robust-rollback restore-point — one row's failure
  rolls back every row in the batch. NO new write mechanism; the batch is N `_apply_one_db` calls
  on one transaction.
- **backend (`data/maintainer-tool/backend/app/routes_confirm.py` + the request model)** — a
  `/confirm/batch` POST endpoint: a request body carrying the author context + a LIST of per-row
  edit specs (each: `kcdx_id`, `valid_from_version`, the `edits` dict — the same shape
  `/confirm/update-version` takes per row), routed through `_run_confirm`'s transaction wrapper over
  the batch apply path, ONE git commit for the whole batch. Author resolution + the deferred-commit
  + the single commit/push reuse the existing `_run_confirm` machinery.
- **FE client (`data/maintainer-tool/frontend/src/api/client.ts`)** — a `confirmBatch(req)` method
  POSTing `/confirm/batch` with the list body + author, parsing the result, mirroring the existing
  `confirmUpdateVersion` client shape.

## Test bar

- **data-core pytest** (`data/refdata-extractor/tests/` or the db_editor test suite) — the batch
  apply path: N valid edits → one DeferredCommit → all N land on commit; a batch where row K fails
  validation → the WHOLE batch rolls back (rows 1..K-1 do NOT land — assert the DB is unchanged from
  the pre-batch state, the D21 all-or-nothing invariant); the per-row validator gate runs (an
  invalid edit is caught, not written). FALSIFIABLE: a batch that lands rows 1..K-1 when row K fails
  is the partial-write defect (fails the row).
- **backend test** (`data/maintainer-tool/backend/tests/`) — `/confirm/batch` POSTs the list body,
  runs ONE transaction, returns the batch result; a one-row failure returns the rollback result with
  nothing committed; the author resolves from the request context (D17a). Emits the canonical
  `ACCEPT-RESULT`/`ACCEPT-SUITE` to the DB-pipeline test sink (`.claude/rules/acceptance-signal.md`).
- **FE client test** (`data/maintainer-tool/frontend/src/api/client.test.ts`, Vitest) —
  `confirmBatch()` POSTs `/confirm/batch` with the list body + `author_name` (D17a, NO
  author_email), parses the changes list. Mirrors the existing `confirmUpdateVersion` test.

Runnable AT this step (the `_apply_one_db` + `DeferredCommit` primitive + `_run_confirm` + the
single-mutation endpoints all exist to compose over). Per `.claude/rules/test-discipline.md`,
`.claude/rules/incremental-delivery.md`.

## Dependencies

- The existing `db_editor` `_apply_one_db` + `DeferredCommit` primitive (the per-edit validated
  write + the deferred-commit transaction this step batches over).
- The existing `_run_confirm` transaction wrapper + author resolution + git commit/push
  (`routes_confirm.py`) — the batch endpoint reuses it.
- The existing single-mutation confirm endpoints (`/confirm/update-version`) — the per-row edit-spec
  shape the batch list carries mirrors their body.

## Reference

[`../plan-spec.md`](../plan-spec.md) — Group E (the batch-transaction the FE drives) + cross-step
invariants 5 (all-or-nothing batch rollback) + 6 (data-core sole writer).

## Design authority

`data/maintainer-tool/design.md` **D32** (the save spine's BATCH mutation — N UPDATEs as ONE atomic
transaction, all-or-nothing rollback D21, one git commit; both batch actions are all-UPDATE so the
law-8/AP18 new-row gate does NOT apply) + **§7** "Batch mutation (bulk re-verify — US-11/D32/D35)".
The per-row edit shape is the existing `/confirm/update-version` contract. Build to D32 + §7, not
this doc's summary.

## UX

Not a UI step — the backend endpoint + the data-core path + the FE client method. The s08
batch-confirm UX (the `batch field-delta list`, the per-row delta, the toast) is step 6.3, which
drives this endpoint. (The save-spine git commit/push stays invisible plumbing — the endpoint
returns a result, never a hash.)

## Disassembler-test / author-burden

None — a save-spine transaction + its client method; no author-facing input, no game-function
target, no AP18 addition (the batch is all-UPDATE; a new row is the separate per-row create flow).
