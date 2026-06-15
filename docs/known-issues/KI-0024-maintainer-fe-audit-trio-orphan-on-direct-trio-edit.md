# maintainer-tool FE: setting a single audit-trio field (verified_by / verified_date / evidence_kind) directly orphans the trio → validator rejects

**Status:** open

## Symptom (user-reported)

Setting `verified_by` (or another audit-trio field) directly in the s04 field editor — on a row whose `last_verified_at_version` is empty — produces a validator rejection:

```
Verified by
was: (empty)
address_versions_seed.csv:20: last_verified_at_version is empty but at least one of the
audit trio is set -- got verified_by='VioletAnvil', verified_date='', evidence_kind=''
```

User: *"these errors appear all the time on random fields. there is something … not clean logic and it causes these to crop up everywhere."*

## Reproduction

1. Open an entity in s04 whose `last_verified_at_version` is empty (an unverified / never-verified row — e.g. the s09 Never-verified set, ids 19–25).
2. Set `verified_by` (or `verified_date`, or `evidence_kind`) directly — without first setting `last_verified_at_version`.
3. The inline validator rejects: the audit trio is now half-set (`verified_by` set, the other two + the version empty) → the all-or-null trio rule fails.

## Root cause (static-traced; awaiting `/debug` confirmation + design fork)

The audit-trio coupling in `data/maintainer-tool/frontend/src/editor/FieldEditor.tsx` `setField` (≈ line 583-595) is **asymmetric — it guards only one direction**:

- Editing **`last_verified_at_version`** → routes through `applyAuditTrioCoupling(prospective, value, maintainerName)` (line 590-592): setting the version auto-fills the empty trio cells (today / maintainer identity / `maintainer_ghidra`); clearing it clears all three. This is "FIX 1", and its comment claims *"the UI bug (orphaning the trio by setting the version alone) cannot happen."*
- Editing a **trio field directly** (`verified_by` / `verified_date` / `evidence_kind`) → falls through to the plain `applyEditPatch({ [seed]: value })` (line 594). **No coupling.** So a direct trio edit on a row with an empty `last_verified_at_version` produces exactly the orphaned half-set trio the version-path coupling was written to prevent — in the OTHER direction the coupling never covers.

The validator (`address_versions_seed.csv:20` / the data-core all-or-null trio rule) is correct to reject it; the defect is that the **editor lets the maintainer reach the orphan state at all**. The "fail-loud at the validator" half shipped without the "keep the trio consistent in the editor" half for the trio→version direction. This is the EDIT-editor sibling of [[KI-0021]] (the CreateEntityForm `verified_date` auto-fill gap) — same audit-trio-coupling family, different surface + direction.

## Facts

- The version→trio coupling (`applyAuditTrioCoupling`) exists and works; the trio→(version+trio) coupling does not — direct trio edits bypass it (FieldEditor.tsx:594).
- The validator rejection is correct (all-or-null trio + non-empty `last_verified_at_version` is a real data invariant — `policy.md` §"audit trio").
- "Random fields, all the time" = the user hitting any of the three trio fields, on any unverified row, plus the interaction with the `verified_date` D17b visibility + the D29 `evidence_kind` suggestion that can also touch a trio cell.
- The user is exercising this while doing the Step 2 (lifecycle-completeness Phase 3) acceptance — the s09 Never-verified rows (empty `last_verified_at_version`) are exactly the rows where a direct trio edit orphans.

## Open questions (the `/debug` will resolve these)

- The full set of entry paths that reach the orphan state: direct `setField` on each trio field; the D29 `evidence_kind` auto-suggest (line 496-507) writing `evidence_kind` on an unverified row; `revertField` interactions; any create/prefill path.
- The fix is a **design fork the user owns** (route through Gate A): when a maintainer edits a trio field on a row with an empty `last_verified_at_version`, should the editor (a) auto-fill `last_verified_at_version` (to what — today's game version? the row's `valid_from`?) + the rest of the trio, making a direct trio edit "promote the row to verified"; OR (b) make the trio fields **non-editable until `last_verified_at_version` is set** (set the version first, then edit the trio); OR (c) couple symmetrically so setting any one trio field fills the rest of the trio but NOT the version, and surface the still-required version as the one field to set? Each is a different UX; the user decides.
- Whether the same asymmetry exists in CreateVersionForm / the s09 `[Verify ▸]` prefill path.
