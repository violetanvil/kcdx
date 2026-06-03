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
| 1b data-core tag seam — optional `version=` on apply_seeds + 5 db_editor writes (A2; oracle-preserving) | NOT STARTED | — |
| 2 read API — curated set + entity detail + version rows + derived status | NOT STARTED | — |
| 3 field-delta API (D8) — saved-vs-prospective | NOT STARTED | — |
| 4 save API — the six job shapes (validate→write→export→round-trip), no commit yet | NOT STARTED | — |
| 5 git commit + push on confirm (D16) + auth-ready seams (D17, + dev default) | NOT STARTED | — |

## Step docs

1. [step-1-backend-skeleton-adapter.md](step-1-backend-skeleton-adapter.md)
1b. [step-1b-data-core-tag-seam.md](step-1b-data-core-tag-seam.md)
2. [step-2-read-api.md](step-2-read-api.md)
3. [step-3-field-delta-api.md](step-3-field-delta-api.md)
4. [step-4-save-api.md](step-4-save-api.md)
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
