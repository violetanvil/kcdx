# Phase 1 — Data-core: reconciliation skip + lifecycle detection (headless)

**Intent:** the headless data-core foundation — the close-intervals already-done skip + the
reconciliation classification (so the resolver never emits a no-op edit), and the
lifecycle-completeness detection query (the orphan / never-verified / broken-reference set at the
current version V). No backend, no FE — pure data-core, tested by the data-core pytest.

## Step-grain ledger

| Step | Status | Commit |
|---|---|---|
| [1.1 — Close-intervals already-done skip + reconciliation classification](step-1-close-skip-reconciliation.md) | DONE | 07fcf70 — close-intervals emits no edit-spec when the target is already closed to its last_verified (the D41 fact-2 symmetric skip); the no-op confirm gap closed. test_reverify_resolver 11/11 ACCEPT, mutation-confirmed falsifiable. |
| [1.2 — The lifecycle-completeness detection query](step-2-lifecycle-detection-query.md) | NOT STARTED | — |

## Phase verification gate

Both steps land their data-core pytest (`data/refdata-extractor/tests/`), emitting the canonical
`ACCEPT-RESULT` / `ACCEPT-SUITE` signal to the DB-pipeline sink. The phase is done when: the resolver
emits no edit-spec for an already-closed close-intervals row (1.1) and the detection query flags each
incomplete-lifecycle kind correctly while leaving a healthy entity unflagged (1.2) — both verified by
the full data-core suite green (no regression). Headless — no game launch, no FE.
