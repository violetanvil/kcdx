# maintainer-tool FE: creating a NEW ENTITY with a verified first row is impossible to save (audit-trio trap)

**Status:** Closed 2026-06-15 (closed_by_commit: FE:9ff0fb5)

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

**Closed 2026-06-15.** The fix landed as its own FE commit during the audit-trio coupling pass
(`FE:9ff0fb5`), not the divergence-diff cycle the §Fix above guessed — so this doc sat stale-open until
the completion-sequence reached it. Closed via `/debug` (Phase B skipped — cause statically verified;
Gate B `root-cause-verifier` returned `land-fix` after an independent revert-and-watch-it-go-red
falsifiability run). Root cause + Fix + Verification below are the gated mechanism record.

### Root cause (the mechanism)

The audit trio `{verified_by, verified_date, evidence_kind}` on an `address_versions` row is governed by
an **all-or-null validator rule keyed off `last_verified_at_version`** (data-core; `policy.md`
§"Verification audit trail") — if `last_verified_at_version` is set, all three trio cells must be
non-empty, else the row is rejected. In the new-**entity** create form
(`src/create/CreateEntityForm.tsx`, the s05 "New entity" overlay), the maintainer marks the first row
verified by setting `last_verified_at_version`, and `verified_date` renders **read-only / system-set**
(D17b — the 🔒 lock glyph, no input element). The form's prospective first row was built as
`{ ...blankBaseline, ...edits, valid_from_version }`, and its `setField` wrote **only the one edited
cell** — so setting `last_verified_at_version` wrote that cell alone and left `verified_date` EMPTY.
With `verified_date` read-only (no input to type into) AND never auto-filled by the form, the trio was
**structurally stuck incomplete**: the validator demanded a value the UI gave the maintainer no way to
supply, so every create-entity save of a verified first row was rejected — inevitably, on every attempt.
The sibling new-**version** form (`CreateVersionForm.tsx`) did not have this trap because it prefills the
whole trio from a source row + defaults `verified_date` to today (US-3); the create-**entity** form was
missed when that auto-fill discipline was applied. A create-path asymmetry, not a validator bug (the
validator is correct).

### Fix

`FE:9ff0fb5` ("fix(KI-0021): new entity with a verified first row is saveable (audit-trio coupling)").
It wires the SAME canonical, unit-tested `applyAuditTrioCoupling(prospective, value, identity)`
(`fieldModel.ts`) — already used by the s04 edit editor and the version form — into
`CreateEntityForm.setField` AND `revertField` for `last_verified_at_version`:

- **SET** (version non-empty) auto-fills only the EMPTY trio cells: `verified_date = todayIsoLocal()`,
  `evidence_kind = maintainer_ghidra`, `verified_by = identity`. `identity = ""` here (a new entity
  threads no configured-signer prop), so `verified_by` auto-fills empty and the maintainer types it; an
  incomplete trio then fails LOUD at the validator, never a silent orphan.
- **CLEAR** (version emptied) clears all three, holding the all-or-null invariant in the clearing
  direction (no orphaned lone `verified_date` — the AP13 concern).

Routing BOTH set and revert through the one canonical function makes the trio **complete-by-construction**
when the version is set, removing the structural trap at its mechanism (not masking the symptom). The
fix addresses the cause directly: the value that was wrong (`verified_date` empty) is now filled by the
same coupling the other forms use; the read-only render is no longer a dead end because the form supplies
the value. (Reusing the canonical coupling over a date-only sliver — "Option C" — was Gate-A
architect-review + AskUserQuestion approved when `9ff0fb5` landed, recorded in its commit body.)

The folded s05 `CreateVersionForm` blank-trio concern (a Gate-B finding during KI-0024, same audit-trio
family) was verified non-existent as a live trap: `CreateVersionForm` starts from a complete prefilled
trio (source row + `verified_date`-today default), so it cannot orphan; nothing to fix there.

### Verification

- **Regression test** (the same-change permanent net): `src/create/CreateEntityOverlay.test.tsx`
  "KI-0021: marking the first row verified auto-fills verified_date (today) + evidence_kind, so a
  verified new entity is saveable" — **FALSIFIABLE** (revert the fix → setting the version fills nothing
  → `verified_date` stays empty → the read-only cell is blank AND the save body's `first_version_columns`
  carries no `verified_date` → the assertions fail). It drives the full flow (set version → assert
  `verified_date` read-only shows today + `evidence_kind = maintainer_ghidra` → type `verified_by` →
  Review → AP18 ack → Confirm) and asserts the create-entity save body carries the complete trio. The
  CLEAR direction (AP13 orphan) is covered by `fieldModel.test.ts`'s CLEARED-clears-all-three +
  both-directions tests (set/revert route through the identical function).
- **Verified live this session:** FE `npm run build` exit 0; `vitest src/create/CreateEntityOverlay.test.tsx`
  = **7/7 passing**. Gate B `root-cause-verifier` independently confirmed the mechanism by reverting the
  fix and watching the test go red at the predicted assertion (`expected '🔒—' to contain '2026-06-15'`),
  then returned `land-fix`. The s04 sibling trio-edit path was user-experienced + confirmed during the
  Step 2 (lifecycle-completeness Phase 3) acceptance the same day.
