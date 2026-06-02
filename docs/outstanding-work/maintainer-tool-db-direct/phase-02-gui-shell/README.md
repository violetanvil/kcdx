# Phase 2 — GUI shell

**Intent.** Build the PySide6/Qt6 thin shell over the proven Phase-1 data-core
(design §5: presentation only — no validation, no SQL, no export logic in the
GUI; it calls down into `seeds_shared/`). Delivers the Job-2 MVP screen
end-to-end: load the curated set, browse + pick an entity (current-row-first +
full-history, read-only triple), edit the audit trio with inline validation, the
save chain (validate→write→export→round-trip→show CSV diff), and commit-on-Confirm
under the git-concurrency discipline. Every UX state from design §7.

The GUI lives in `data/maintainer-tool/` (a new package; design §5). It depends on
the data-core; the data-core depends on nothing in the GUI.

Shared spec: [`../plan-spec.md`](../plan-spec.md). Screen spec:
[`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 (user stories) + §7 (UX & states).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 4 app skeleton + load curated set (US-1) + empty/loading states | NOT STARTED | — |
| 5 entity list + pick + current-row-first/full-history + read-only triple (US-2, R8) | NOT STARTED | — |
| 6 audit-trio edit form + inline validation via shared validator (US-3) | NOT STARTED | — |
| 7 save chain: validate→write→export→round-trip→show CSV diff (US-4, D5) + write-failure state | NOT STARTED | — |
| 8 commit-on-Confirm, git-concurrency-safe (D6) + commit-result state | NOT STARTED | — |

## Step docs

4. [step-4-app-skeleton-load.md](step-4-app-skeleton-load.md)
5. [step-5-entity-list-pick.md](step-5-entity-list-pick.md)
6. [step-6-audit-trio-edit.md](step-6-audit-trio-edit.md)
7. [step-7-save-chain-diff.md](step-7-save-chain-diff.md)
8. [step-8-commit-on-confirm.md](step-8-commit-on-confirm.md)

## Verification gate (phase end)

- The Job-2 MVP runs end-to-end: launch → load curated set → pick an entity →
  see its current-version row (+ full history) → edit the audit trio → Save →
  (validate → write DB → export CSVs → round-trip → CSV diff shown) → Confirm →
  committed. No CSV hand-edit at any point.
- Every UX state present + correct (design §7): populated, empty (no
  DB/seeds resolved), loading, validation-error (inline, no write), write-failure
  (atomic rollback shown), diff-confirm, commit-result (incl. live-lock retry),
  edge (multi-row version history).
- The GUI holds no validation / SQL / export logic — it calls the Phase-1
  data-core (design §5 thin-shell invariant).
- **User-facing acceptance** (`.claude/rules/ux-first-class.md`, design §7): the
  maintainer experiences the screen — picks an entity, edits the trio, sees the
  CSV diff, confirms. Build-green is necessary, not sufficient for this UI phase.
  The save chain's CSV-diff view is the acceptance signal
  (`.claude/rules/acceptance-signal.md`); the perceptual parts (layout, the
  read-only fields visibly distinct) are an eyeball gate.
