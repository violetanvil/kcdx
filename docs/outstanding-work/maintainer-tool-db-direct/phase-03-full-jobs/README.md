# Phase 3 — full jobs

**Intent.** Build the capabilities that complete the six-job catalog on top of the
Phase-2 spine: the DLL-link verification context (s07), create new version (Job 6) +
create new entity (Job 1) (s05), lifecycle editing supersede/deprecate (Jobs 4/5) on
s02, and version history + side-by-side compare (s03). Each reuses the spine's
save-confirm + atomic commit (step 11) and the Phase-1 write shapes (INSERT step 4,
lifecycle step 5). End state: the maintainer can do everything in the catalog.

Every step obeys the 9 interaction laws (`data/maintainer-tool/ui/design.md`). The
DLL-link (step 12) lands first because it is the verification context the create flows
(D9) and the "current row" marker (D10/R12) reference — but it is advisory, so the
spine + jobs work without it.

Shared spec: [`../plan-spec.md`](../plan-spec.md). UI design:
[`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../../data/maintainer-tool/ui/screens/).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 12 s07 DLL link — verification context (advisory/overridable) + resolver-matched "current" marker | NOT STARTED | — |
| 13 s05 create new version (Job 6) — prefill, nothing-changed guard, AP18 approval | NOT STARTED | — |
| 14 s05 create new entity (Job 1) — id-assignment, first version row, AP18 approval | NOT STARTED | — |
| 15 s02 lifecycle editing (Jobs 4/5) — supersede + deprecate forms, pair-integrity | NOT STARTED | — |
| 16 s03 version history + side-by-side compare — N-column, marked diffs, edit-from-compare | NOT STARTED | — |

## Step docs

12. [step-12-dll-link.md](step-12-dll-link.md)
13. [step-13-create-version.md](step-13-create-version.md)
14. [step-14-create-entity.md](step-14-create-entity.md)
15. [step-15-lifecycle-edit.md](step-15-lifecycle-edit.md)
16. [step-16-history-compare.md](step-16-history-compare.md)

## Verification gate (phase end)

- The full catalog runs end-to-end from the GUI: link a DLL (or work unlinked with the
  advisory warning + override); create a new version (prefilled, nothing-changed guard,
  AP18 approval) and a new entity (assigned id, first row, AP18 approval); supersede +
  deprecate an entity (pair-integrity); compare versions side-by-side with marked diffs
  and edit a version from the compare view. Each mutation goes through the spine's
  field-delta confirm + atomic commit.
- The advisory/override discipline holds (law 4): no action is blocked by an unlinked
  DLL or a resolver failure; every such case is overridable by an explicit "I accept —
  save anyway". A new entity/version is approval-gated (law 8, AP18, D11); a new
  version identical to its source is blocked with steering copy (D12).
- The compare reads `address_versions` rows (game-version data, NOT git history);
  differing fields are marked by glyph + band (not color-alone, law 7); the column
  count drives dynamic horizontal scroll without reflowing non-compare elements
  (law 1).
- **User-facing acceptance** (`.claude/rules/ux-first-class.md`): the maintainer
  experiences each job — links a DLL, creates a version + entity, supersedes/deprecates,
  compares and edits from compare. The field delta + status-bar result are the
  acceptance signals; the perceptual parts (the compare diff marking, the prefilled
  create forms, the approval/override banners) are an eyeball gate. Build-green is
  necessary, not sufficient.
