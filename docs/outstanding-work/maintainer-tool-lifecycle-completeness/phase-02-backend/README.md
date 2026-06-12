# Phase 2 — Backend: expose detection + classification

**Intent:** surface the Phase-1 data-core work through the FastAPI backend — a read-only needs-action
endpoint (exposing the 1.2 detection query for the s09 view) and the reconciliation classification in
the `/save/reverify-batch` preview (so the FE knows which report rows are already-acted vs actionable).
Both read-only / preview-only; the write path is unchanged (law 6 / D19).

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [2.1 — The needs-action read endpoint](step-1-needs-action-endpoint.md) | DONE | d319f14 — read-only GET /needs-action exposes the 1.2 detection query (three kinds + total_count for the s01 badge); `audit_lifecycle` exported through the seeds_shared → data_core seam. test_read_endpoints 12 passed (ACCEPT 3/3), mutation-confirmed; full backend suite 78 passed. |
| [2.2 — The reconciliation classification in the /save/reverify-batch preview](step-2-reconciliation-preview.md) | DONE | (landed) — the preview now classifies each report row `actionable` vs `already_acted` (surfacing the resolver's already-done omission with the s08 marker — "interval already closed" / "already current"), so the FE reads it (never computes it). Preview-only; resolver untouched. test_reverify_batch_endpoint 6 passed (ACCEPT 3/3), mutation-confirmed; full backend suite 79 passed. |

## Phase verification gate

Both steps land their backend test (`data/maintainer-tool/backend/tests/`), emitting the canonical
`ACCEPT-RESULT` / `ACCEPT-SUITE` signal. The phase is done when: GET the needs-action endpoint returns
the three incomplete-lifecycle kinds read-only (2.1, the DB byte-identical after the read), and the
`/save/reverify-batch` preview classifies an already-acted report row as no-action (2.2) — both verified
by the backend test green + no data-core regression. No write, no transaction (read/preview only).
