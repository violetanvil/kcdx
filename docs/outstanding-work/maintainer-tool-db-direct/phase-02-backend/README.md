# Phase 2 — backend (FastAPI over the data-core)

**Intent.** Build the Python API that wraps the proven Phase-1 data-core (design §5: the
backend holds NO validation/SQL/export/rule logic — every invariant is the data-core's,
reached in-process; D13/R3/law 6). Delivers the API the frontend calls: the app skeleton +
the version-tag→data-core-params adapter, the read API, the field-delta API, the save API
(the six job shapes), and the git commit + push on confirm + the auth-ready seams. End
state: the full six-job authoring path is reachable over HTTP, committing + pushing
server-side — testable without any frontend.

The backend lives in `data/maintainer-tool/backend/` (a new package; design §5). It depends
on the data-core (`seeds_shared`); the data-core depends on nothing in the backend.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Design:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) §5/§6/§8 +
§10 D13/D14/D16/D17/D18.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 1 backend skeleton + data-core import seam + version-tag→params adapter + health/load endpoint | DONE | c0b270c |
| 1b data-core tag seam — optional `version=` on apply_seeds + 5 db_editor writes (A2; oracle-preserving) | DONE | 7ddb2a1 |
| 2a data-core read seam — read_curated_set/read_entity_detail/read_version_rows + the single derive_status (policy.md 4-rule); the rule logic lives in the data-core, not the backend (D13/law 6) | DONE | 73966eb |
| 2b backend read endpoints — call 2a + serialize to JSON (curated set / entity detail / version rows) | DONE | 5cfa556 |
| 3 field-delta API (D8) — saved-vs-prospective | DONE | 8224fc3 |
| 4a data-core deferred-commit seam — apply_seeds runs validate→write→round-trip then RETURNS the open connections uncommitted; commit(conns)/rollback(conns) exposed (THE write mechanism for the maintainer tool — DB changes commit only on confirm); additive + oracle-preserving | NOT STARTED | — |
| 4b backend save endpoints — the six job shapes, each opening a deferred-commit txn via 4a, returning the result+delta for the confirm gate; the txn is held for step 5's commit-on-confirm | NOT STARTED | — |
| 5 git commit + push on confirm (D16) — COMMIT the held 4a txn + the git commit as ONE confirm transaction + auth-ready seams (D17, + dev default) | NOT STARTED | — |

## Step docs

1. [step-1-backend-skeleton-adapter.md](step-1-backend-skeleton-adapter.md)
1b. [step-1b-data-core-tag-seam.md](step-1b-data-core-tag-seam.md)
2a. [step-2a-data-core-read-seam.md](step-2a-data-core-read-seam.md)
2b. [step-2b-backend-read-endpoints.md](step-2b-backend-read-endpoints.md)
3. [step-3-field-delta-api.md](step-3-field-delta-api.md)
4a. [step-4a-data-core-deferred-commit-seam.md](step-4a-data-core-deferred-commit-seam.md)
4b. [step-4b-backend-save-endpoints.md](step-4b-backend-save-endpoints.md)
5. [step-5-commit-push-seams.md](step-5-commit-push-seams.md)

## Verification gate (phase end)

- The backend serves the full six-job authoring path over HTTP: read the curated set + an
  entity's detail + version rows; compute a field delta; save any of the six job shapes
  (re-verify, full-column edit, create version, create entity, supersede, deprecate) through
  the data-core save spine; commit + push on confirm.
- The backend holds NO validation/SQL/export/rule logic — it calls the data-core; an invalid
  edit is rejected by the data-core's validator (not a backend re-implementation) and aborts
  with no write (law 6, D13).
- Each step ships a backend test (the repo's Python test convention — `pytest` over the
  API/route handlers against the mini-dump fixture, or a route-level test driving the
  data-core): the read API returns the curated shape; the field-delta API matches
  `field_delta`; the save API lands each job shape atomically + aborts an invalid one with
  no write; the commit+push stages by exact path + respects a live lock (the push is
  asserted against a throwaway local remote / mocked, not a real GitHub push).
- The auth-ready seams are present + documented (the env var names for the push credential;
  the request-context identity field the commit author uses) + a dev default lets the
  backend boot + run locally without the operator's auth (D17).
- This phase ships NO UI. Acceptance is the backend test suite — no browser, no Docker yet.
