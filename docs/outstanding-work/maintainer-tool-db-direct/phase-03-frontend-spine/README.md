# Phase 3 — frontend spine (React + Mantine)

**Intent.** Build the React + Mantine read/edit/save backbone over the Phase-2 backend API
(the frontend holds NO authoring logic — every rule is the data-core's, surfaced through the
API; law 6). Delivers the spine every job reuses: the app skeleton + the Mantine theme (the
token system) + the responsive app shell (two-pane ↔ drill-down), the navigator (s01), the
entity detail read (s02), the field editor (s04), and the save-confirm + toast + atomic
save→commit (s06). End state: a maintainer can browse/find an entity, view its current
version, edit it (re-verify or full-column correct), see the field delta, and save (commit +
push) — end-to-end in a browser, including a phone. The create/compare/lifecycle jobs
(Phase 4) build on this spine.

The frontend lives in `data/maintainer-tool/frontend/` (a new package; design §5). It calls
the backend API; the backend depends on nothing in the frontend. Every step obeys the 9
interaction laws (`ui/design.md`).

Shared spec: [`../plan-spec.md`](../plan-spec.md). UI design:
[`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../../data/maintainer-tool/ui/screens/).

## Settled before build (2026-06-03 — captured so they survive the session)

- **Frontend stack: Vite + React + TypeScript** + Vitest/@testing-library/react + npm — a
  client-only SPA over the FastAPI backend (no SSR). Recorded as a closed sub-decision in
  [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md) D14.
- **Linux-container-compat by construction from step 6** (the Docker image lands at Phase 5,
  D18): `.gitattributes` LF pin for the frontend tree, an `npm ci`-clean `package-lock.json`
  (linux optional-dep entries present), exact-case imports. Build discipline now; image at P5.
- **Sequencing — build the schema fold (`../../schema-flatten-survival-fold/`) FIRST.** This
  spine's field editor (step 9) + API client (step 6) should build against the FINAL flat
  schema (D22/§11 survival fold), not get refactored after it lands. Phase 3 resumes after the
  fold. A `/feature` run was started + parked mid-dispatch at step 6 (the stack + Linux-compat
  above are its settled audit decisions); re-running `/feature` here resumes from these.
  **The fold is now COMPLETE (`schema-flatten-survival-fold/` closed `274421a`) — this
  precondition is MET; Phase 3 is unblocked.** The 6 folded survival columns
  (`aob`/`anchor_string`/`rule`/`slot_count`/`expect_unique`/`derives_from`) are in
  `read_version_rows`' display contract (`read_api.py` `_VERSION_DISPLAY_COLUMNS`) and serialize
  through the backend read endpoint (`routes_read.py`) — s02 (step 8) surfaces them, s04
  (step 9) edits them, per US-5's "six survival columns". The step docs build against the final
  flat schema as authored; no edit was needed.

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 6 frontend skeleton + Mantine theme (tokens) + responsive app shell + API client | DONE | frontend-repo 845e694 |
| 7 s01 navigator — search + status/kind filters + entity list + status chips | DONE | frontend-repo eb9e497 |
| 8 s02 entity detail (read) — header + version table + version dropdown + default-row | DONE | frontend-repo 65cb234 |
| 9 s04 field editor — view/edit a version row, dirty markers + "was:", inline validation | NOT STARTED | — |
| 10 s06 save-confirm (field delta + approval) + toast + atomic save→commit | NOT STARTED | — |

## Step docs

6. [step-6-frontend-skeleton-theme-shell.md](step-6-frontend-skeleton-theme-shell.md)
7. [step-7-navigator.md](step-7-navigator.md)
8. [step-8-entity-detail-read.md](step-8-entity-detail-read.md)
9. [step-9-field-editor.md](step-9-field-editor.md)
10. [step-10-save-confirm-commit.md](step-10-save-confirm-commit.md)

## Verification gate (phase end)

- The edit/save spine runs end-to-end in a browser: open the app → load the curated set →
  search/filter → pick an entity (drill-down on phone) → see its current version row → edit
  (audit trio OR a full-column correction) with dirty markers + inline validation → Review
  changes → see the plain-language field delta → Confirm → committed + pushed (the "Saved"
  toast). No CSV hand-edit; git invisible.
- The frontend holds no validation/SQL/rule logic — it calls the Phase-2 API (law 6).
- The 9 interaction laws hold on every spine screen: layout never jumps (law 1), the
  responsive navigation shell persists (law 2 — two-pane on wide, drill-down on phone),
  navigation is user-driven (law 3), read-only identity is non-color (law 7), the save is
  one atomic confirmed transaction (law 5), no raw values at a call site (law 9 — all via the
  Mantine theme).
- The responsive shell works at both breakpoints (the `bp_two_pane` token): two-pane on
  wide, master-detail drill-down on phone (list → tap → full-screen detail + back).
- UX states present + correct (each screen spec): populated, empty (no DB/seeds via the API /
  no selection / no search match — distinct copy), loading, validation error (inline, no
  write), write failure (rollback), field-delta confirm, save result (committed / blocked-
  retry toast — never a forced reap).
- **User-facing acceptance** (`.claude/rules/ux-first-class.md`): the maintainer experiences
  the spine in a real browser (desktop AND a phone viewport) — finds an entity, edits it,
  sees the field delta, confirms. The acceptance is the user running the built app (the
  Docker/served artifact, or `vite preview`-class served build — NOT a dev hot-reload
  launcher, `.claude/rules/acceptance-signal.md`); the field delta + the "Saved" toast are
  the signals; the perceptual parts (layout stability, the read-only treatment, the dirty
  markers, the responsive reflow) are an eyeball gate.
