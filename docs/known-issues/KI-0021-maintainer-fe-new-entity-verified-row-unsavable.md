# maintainer-tool FE: creating a NEW ENTITY with a verified first row is impossible to save (audit-trio trap)

**Status:** open

**Component:** the maintainer-tool frontend repo (`data/maintainer-tool/frontend/`, the separate
gitignored nested repo) — `src/create/CreateEntityForm.tsx` (the "New entity" / s05-create overlay).
NOT the kcdx engine; NOT the backend validator (the validator is correct).

## Symptom (user-hit)

Creating a new entity (`CSystem_pCryPak_construct_store` in the report) and marking its first row
verified — `last_verified_at_version = 1.5.1164953`, `verified_by = VioletAnvil`,
`evidence_kind = Maintainer Ghidra walk` — is rejected on every save attempt. The error repeats on
each audit-trio field:

> `address_versions_seed.csv:159: last_verified_at_version='1.5.1164953' is set but the audit trio
> (verified_by, verified_date, evidence_kind) is incomplete — got verified_by='VioletAnvil',
> verified_date='', evidence_kind='maintainer_ghidra'`

The maintainer CANNOT clear the error: `verified_date` renders **read-only / system-set** (the 🔒
lock glyph, "(unchanged)", no input element — D17b), so there is no field to type the date into, and
the form never fills it. The trio is permanently incomplete → every save fails.

## Diagnosis (code-read, not theorized)

The audit-trio validator (`policy.md` §"Verification audit trail") enforces all-set-or-all-null on
`{verified_by, verified_date, evidence_kind}` keyed off `last_verified_at_version`. That rule is
correct. The defect is a **create-path asymmetry** in the frontend:

- `src/create/CreateVersionForm.tsx` (new **version** of an existing entity) auto-fills
  `seed.verified_date = todayIso()` when the row is verified (US-3, the "verified_date defaults to
  TODAY" create-flow default) — so the trio completes automatically and the save succeeds.
- `src/create/CreateEntityForm.tsx` (new **entity** — the screenshot's "New entity" overlay) has the
  same D17b read-only `verified_date` render but **NO `todayIso()` auto-fill**. Its prospective row is
  `{ ...saved, ...edits, [VALID_FROM_SEED]: validFrom }` where `saved` is the blank baseline, so
  `verified_date` stays `''`. The maintainer sets the other two trio fields + the version, and the
  read-only `verified_date` can never be filled.

The new-version form got the today-default; the new-entity form was missed. A `verified_date` that is
read-only AND never auto-filled is the trap: the validator demands a value the UI gives no way to
supply.

## Fix (deferred by user decision — Phase 3.2 of the divergence-diff feature finishes first)

Port `CreateVersionForm`'s verified-row → `verified_date = todayIso()` auto-fill to
`CreateEntityForm`: when the new entity's first row is marked verified (`last_verified_at_version`
non-empty), default `verified_date` to today (read-only, system-set, overrideable to the same degree
the version form allows), so the trio is complete and the save succeeds. The fix is an FE-repo change
(gated by `npm run build` + `npx vitest run`), committed there with an `FE:<hash>` ledger reference,
NOT a kcdx engine change. It ships with a regression test (a `CreateEntityOverlay` test that a
verified new-entity first row saves — pinning the trio-completeness contract on the create-entity
path, the same way `CreateVersionOverlay.test.tsx` pins it for the version path). Likely routed through
`/debug` or a focused `/execute` cycle when the divergence-diff feature closes.

## Resolution

_(open — to be filled when the auto-fill lands)_
