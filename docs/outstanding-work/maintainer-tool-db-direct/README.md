# maintainer-tool-db-direct

**Intent.** Build the maintainer tool: DB-direct authoring of the Address Library with
CSV auto-export — **the complete six-job web app** (v1). A small set of trusted
maintainers manages the entire reference DB from any browser (including a phone):
browse/search/filter, view any entity's full record + all game-version rows, compare
versions side-by-side, and author all six jobs (create entity, re-verify, supersede,
deprecate, create version, plus edit any existing version's full columns). Every mutation
validates, writes the DB, auto-exports the three seed CSVs (byte-identity round-trip),
shows a plain-language field delta, and commits + pushes server-side as one atomic
transaction — no CSV hand-edit, git invisible.

Shared spec: [`plan-spec.md`](plan-spec.md). Settled design:
[`data/maintainer-tool/design.md`](../../../data/maintainer-tool/design.md) (TRD,
web-pivoted, `32df16d`) +
[`data/maintainer-tool/ui/design.md`](../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../data/maintainer-tool/ui/screens/) (the React/Mantine UI layer,
`8c170c8`).

## Status ledger (phase-grain)

| Step | Status | Commit |
|---|---|---|
| Phase 1 — data-core (exporter + round-trip + db_editor UPDATE/INSERT/lifecycle + field-delta) | DONE | 044fe03 |
| Phase 2 — backend (FastAPI over the data-core: skeleton+adapter, read API, field-delta, preview-Save, DIRECT-DB write + scoped restore-point + Confirm commit+push+seams — DB-direct model D19/D20/D21) | DONE | 75442e0 |
| Phase 3 — frontend spine (React+Mantine: shell+theme, navigator, detail-read, field editor, save-confirm+toast) | NOT STARTED | — |
| Phase 4 — frontend full-jobs (client JS resolver, create version/entity, lifecycle, compare) | NOT STARTED | — |
| Phase 5 — Docker packaging (image + volume layout) | NOT STARTED | — |

The per-step ledgers live in each `phase-NN-*/README.md`. A top row flips to `DONE` only
when every step in the phase is `DONE`. **Phase 1 is DONE** (8 steps landed, all oracles
green); two follow-ons remain filed but out of this re-plan: step 3c (full-column
correction, probe-first) + TD-0004 (the rebuild-oracle baseline re-capture).

## Phases

- **[Phase 1 — data-core](phase-01-data-core/README.md)** ✅ DONE — the headless authoring
  logic in `seeds_shared/`: `csv_exporter`, the round-trip oracle, `db_editor`
  (UPDATE/INSERT/lifecycle, wrapping the validated applier), and `field_delta`. The whole
  authoring path is proven with zero UI. **Delivery-agnostic** — the web backend calls it
  unchanged.
- **[Phase 2 — backend](phase-02-backend/README.md)** — the Python (FastAPI/Flask) API over
  the data-core: the app skeleton + the version-tag→data-core-params adapter (no DLL
  server-side), the read API, the field-delta API, the save API (the six job shapes), and
  the git commit + push on confirm + the auth-ready seams (D16/D17).
- **[Phase 3 — frontend spine](phase-03-frontend-spine/README.md)** — the React + Mantine
  read/edit/save backbone over the backend: the app skeleton + the Mantine theme (the token
  system) + the responsive app shell (two-pane ↔ drill-down), the navigator (s01), the
  entity detail read (s02), the field editor (s04), and the save-confirm + toast + atomic
  save→commit (s06). End state: re-verify/edit an existing version end-to-end works in a
  browser.
- **[Phase 4 — frontend full-jobs](phase-04-frontend-jobs/README.md)** — the capabilities
  built on the spine: the client-side JS `.rdata` resolver (D15), create new version (Job 6)
  + new entity (Job 1) (s05), lifecycle editing supersede/deprecate (Jobs 4/5) on s02, and
  version history + side-by-side compare (s03).
- **[Phase 5 — Docker packaging](phase-05-docker/README.md)** — the Docker image (backend
  serving the built frontend) + the mounted-checkout-@-configured-path + the env-injected
  push credential seam (D14/D18). Lands last — nothing to package until the app works. Auth
  / hosting / the portal are the operator's (D17), out of scope.
