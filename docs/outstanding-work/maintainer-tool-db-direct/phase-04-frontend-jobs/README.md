# Phase 4 — frontend full-jobs

**Intent.** Build the capabilities that complete the six-job catalog on top of the Phase-3
spine: the client-side JS `.rdata` resolver (D15), create new version (Job 6) + create new
entity (Job 1) (s05), lifecycle editing supersede/deprecate (Jobs 4/5) on s02, and version
history + side-by-side compare (s03). Each reuses the spine's save-confirm + atomic
save→commit (step 10) and the Phase-2 save endpoints (the data-core INSERT + lifecycle
shapes). End state: the maintainer can do everything in the catalog, from a browser including
a phone.

Every step obeys the 9 interaction laws (`ui/design.md`). The client resolver (step 11)
lands first because the create flows (step 12) prefill the new version's tag from it — but it
is advisory (D15), so the spine + jobs work with just the version dropdown.

Shared spec: [`../plan-spec.md`](../plan-spec.md). UI design:
[`data/maintainer-tool/ui/design.md`](../../../../data/maintainer-tool/ui/design.md) +
[`ui/screens/`](../../../../data/maintainer-tool/ui/screens/).

## Step ledger

| Step | Status | Commit |
|---|---|---|
| 11 client-side JS `.rdata` resolver (D15) + the "check against a local DLL" control (s02) + cross-impl test | NOT STARTED | — |
| 12 s05 create new version (Job 6) — prefill, nothing-changed guard, AP18 approval | NOT STARTED | — |
| 13 s05 create new entity (Job 1) — id-assignment, first version row, AP18 approval | NOT STARTED | — |
| 14 s02 lifecycle editing (Jobs 4/5) — supersede + deprecate forms, pair-integrity | NOT STARTED | — |
| 15 s03 version history + side-by-side compare — N-column, marked diffs, edit-from-compare | NOT STARTED | — |

## Step docs

11. [step-11-client-dll-resolver.md](step-11-client-dll-resolver.md)
12. [step-12-create-version.md](step-12-create-version.md)
13. [step-13-create-entity.md](step-13-create-entity.md)
14. [step-14-lifecycle-edit.md](step-14-lifecycle-edit.md)
15. [step-15-history-compare.md](step-15-history-compare.md)

## Verification gate (phase end)

- The full catalog runs end-to-end in a browser: check a version against a local DLL
  (client-side, no upload — or work with just the version dropdown); create a new version
  (prefilled, nothing-changed guard, AP18 approval) and a new entity (assigned id, first row,
  AP18 approval); supersede + deprecate an entity (pair-integrity); compare versions
  side-by-side with marked diffs and edit a version from the compare view. Each mutation goes
  through the spine's field-delta confirm + atomic save→commit (Phase 3 step 10 / Phase 2).
- The advisory/override discipline holds (law 4, D15): no action is blocked by an unverified
  version; every such case is overridable ("I accept — save anyway"). A new entity/version is
  approval-gated (law 8, AP18, D11); a new version identical to its source is blocked with
  steering copy (D12).
- The client `.rdata` resolver agrees with the Python `version_resolver.py` (the
  cross-implementation test) and never uploads the DLL (D15).
- The compare reads `address_versions` rows (game-version data, NOT git history); differing
  fields are marked by glyph + band (not color-alone, law 7); the column count drives dynamic
  horizontal scroll (law 1) — the primary fit mechanism on a phone.
- **User-facing acceptance** (`.claude/rules/ux-first-class.md`): the maintainer experiences
  each job in a real browser (desktop AND a phone viewport) — checks a DLL, creates a version
  + entity, supersedes/deprecates, compares + edits from compare. The acceptance is the user
  running the built/served app (NOT a dev hot-reload launcher, `.claude/rules/acceptance-signal.md`);
  the field delta + the save toast are the signals; the perceptual parts (the compare diff
  marking, the prefilled create sheets, the approval/override banners, the responsive reflow)
  are an eyeball gate.
