# Phase 2 — GUI spine

**Intent.** Build the PySide6/Qt6 read/edit/save backbone over the proven Phase-1
data-core (design §5: presentation only — no validation, no SQL, no export logic in
the GUI; it calls down into `seeds_shared/`). Delivers the spine every job reuses: the
app shell + token layer, the entity navigator (s01), the entity detail read view
(s02), the field editor (s04), and the save-confirm + atomic commit (s06). End state:
a maintainer can browse/find an entity, view its current version, edit it (re-verify
or full-column correct), see the plain-language field delta, and commit — end-to-end.
The create/compare/lifecycle jobs (Phase 3) build on this spine.

The GUI lives in `data/maintainer-tool/` (a new package; design §5). It depends on the
data-core; the data-core depends on nothing in the GUI. Every GUI step obeys the 9
interaction laws (`data/maintainer-tool/ui/design.md`).

Shared spec: [`../plan-spec.md`](../plan-spec.md). UI design:
[`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../../data/maintainer-tool/ui/screens/).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 7 app skeleton + token/theme layer + load curated set (empty/loading states) | NOT STARTED | — |
| 8 s01 navigator — search + status/kind filters + entity list + status chips | NOT STARTED | — |
| 9 s02 entity detail — header (read-only identity) + version table + default-row select | NOT STARTED | — |
| 10 s04 field editor — view/edit a version row, dirty markers + "was:", inline validation | NOT STARTED | — |
| 11 s06 save-confirm (field delta + approval/override) + atomic commit transaction | NOT STARTED | — |

## Step docs

7. [step-7-app-skeleton-tokens.md](step-7-app-skeleton-tokens.md)
8. [step-8-navigator.md](step-8-navigator.md)
9. [step-9-entity-detail-read.md](step-9-entity-detail-read.md)
10. [step-10-field-editor.md](step-10-field-editor.md)
11. [step-11-save-confirm-commit.md](step-11-save-confirm-commit.md)

## Verification gate (phase end)

- The edit/save spine runs end-to-end: launch → load curated set → search/filter →
  pick an entity → see its current version row → edit (audit trio OR a full-column
  correction) with dirty markers + inline validation → Review changes → see the
  plain-language field delta → Confirm → committed (one atomic commit). No CSV
  hand-edit.
- The GUI holds no validation / SQL / export logic — it calls the Phase-1 data-core
  (design §5 thin-shell invariant; UI law 6).
- The 9 interaction laws hold on every spine screen: layout never jumps (law 1), the
  two panes persist (law 2), navigation is user-driven (law 3), read-only identity is
  non-color (law 7), the save is one atomic confirmed transaction (law 5), no raw
  values at a call site (law 9).
- UX states present + correct (each screen spec + design §7): populated, empty (no
  DB/seeds / no selection / no search match — distinct copy), loading, validation
  error (inline, no write), write failure (atomic rollback shown), field-delta
  confirm, save result (committed / blocked-retry — never a forced reap).
- **User-facing acceptance** (`.claude/rules/ux-first-class.md`, design §7): the
  maintainer experiences the spine — finds an entity, edits it, sees the field delta,
  confirms. Build-green is necessary, not sufficient for this UI phase. The field
  delta + the status-bar "Saved" line are the acceptance signals
  (`.claude/rules/acceptance-signal.md`); the perceptual parts (layout stability, the
  read-only fields visibly distinct, dirty markers) are an eyeball gate.
