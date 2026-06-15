# maintainer-tool FE: setting a single audit-trio field (verified_by / verified_date / evidence_kind) directly orphans the trio → validator rejects

**Status:** closed
**Closed:** 2026-06-15
**Closed by:** FE 8557c11 + FE 158dd3f

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

## Reframe — acceptance reopened the fix (2026-06-15, fix attempt `8557c11`)

The first fix (option b — lock the trio inputs while `last_verified_at_version` is empty + gate the
D29 auto-suggest; FE `8557c11`) passed build + vitest 598/598 + Gate A + Gate B, but the user's live
acceptance found it INCOMPLETE and surfaced two adjacent defects on the same surface. All three are
the same audit-trio-coupling family and stay in THIS KI:

- **P1 — the clear-after-fill orphan (a NEW orphan path the lock did not cover).** Once a version is
  set, the lock correctly lifts and the trio inputs enable (conditional, not permanent). But then
  EMPTYING a now-enabled trio cell (e.g. clearing `verified_by`) on a version-set row produces
  `verified_by='', verified_date='2026-06-15', evidence_kind='maintainer_ghidra'` + a non-empty
  version → a half-set trio again. The first fix closed the version-empty direction (can't write a
  lone cell) but left the version-SET direction (can empty a cell back to a lone-empty trio) open.
  Root cause: still the asymmetric coupling — `applyEditPatch({ [seed]: '' })` on a trio field does
  not re-run `applyAuditTrioCoupling`, so emptying one cell does not clear the trio (or block).

- **P2 — raw validator strings surfaced in the UI (not teaching errors).** The editor renders the
  data-core validator's literal message verbatim (`address_versions_seed.csv:20:
  last_verified_at_version='1.5.1164953' is set but the audit trio (verified_by, verified_date,
  evidence_kind) is incomplete -- got verified_by='', ...`). An internal validator string is not
  author-facing copy — it must TEACH (cornerstones §"errors that teach" / AP14). User: "why are we
  surfacing these full errors in the ui like this? they need teaching errors."

- **P3 — the error attributed to the WRONG fields.** `attributeErrorsToFields` (FieldEditor.tsx:859)
  attributes an error to a field by `error.toLowerCase().includes(field.seed.toLowerCase())` — a naive
  substring match. The trio-incomplete message NAMES all three trio seeds, so it attaches to every
  dirty field whose name appears in the string (the POPULATED ones: `verified_date`, `evidence_kind`,
  `last_verified_at_version`), NOT the emptied-and-deficient `verified_by`. The screenshot shows the
  error under three fields, not under the one the maintainer emptied. User: "this error should appear
  under the field that is required and not filled, NOT the other fields."

The fix for all three is a design fork the user owns (Gate A): P1's coupling-on-clear behavior, P2's
teaching-error translation, P3's attribution to the deficient field. KI stays OPEN; `8557c11` is a
partial fix (the version-empty direction is genuinely closed and stays).

## Resolution

**Root cause:** The s04 field editor (`FieldEditor.tsx`) enforced the audit-trio all-or-null invariant
(verified_by / verified_date / evidence_kind + last_verified_at_version all-set or all-empty) with a
coupling that fired in ONLY ONE direction. `applyAuditTrioCoupling` (`fieldModel.ts`) runs only when
`last_verified_at_version` is the edited field (`setField`'s version branch): a SET fills the empty
trio cells, a CLEAR empties all three. Every OTHER way to change a trio cell bypassed it — a trio cell
written through `setField`'s fall-through `applyEditPatch({ [seed]: value })`, or the D29 evidence_kind
auto-suggest's direct `applyEditPatch`. So any trio-cell write that did not go through the version field
could leave the trio half-set against a mismatched version, and the data-core validator's all-or-null
rule then rejected the prospective row. Four inevitabilities followed from the one-directional coupling:
(1) on a version-EMPTY row, writing a lone trio cell — manually or via the D29 auto-suggest — orphaned
the trio; (2) on a version-SET row, EMPTYING an (correctly-enabled) trio cell to `""` wrote a lone-empty
trio cell + a non-empty version — the same orphan in the opposite direction, because emptying a trio
cell never re-ran the coupling; (3) when the orphan was reached, the editor rendered the validator's RAW
internal string (`address_versions_seed.csv:NN: ... incomplete -- got verified_by='', ...`) verbatim;
(4) it attributed that error by a naive substring match (`attributeErrorsToFields`:
`error.includes(field.seed)`) which, because the trio-incomplete message names all three trio seeds,
attached it to every dirty (populated) trio field while the actually-empty deficient field — often not
even in `dirtyFields` — got no error at all. The unifying cause: a single-direction coupling on the
version field left every trio-cell-initiated path (write a cell, empty a cell, auto-suggest a cell)
uncovered, and the error surface compounded it by showing the raw verdict on the wrong fields.

**Fix:** FE `8557c11` (version-empty direction) + FE `158dd3f` (version-set direction + error surface).
(1) **version-empty:** disable the editable trio inputs + early-return the D29 auto-suggest while
`last_verified_at_version` is empty. (2) **version-set (P1):** `setField` rejects an empty value (`""`)
for an editable trio seed while the version is set — re-typing a different non-empty value stays allowed;
to un-verify, clear the version (the existing version→trio CLEAR coupling empties all three) — made
legible by an inline note (new append-only `FieldRow.note` prop). (3) **teaching error (P2):** the
trio-incomplete verdict is detected by a stable substring (`"audit trio"` + `"incomplete"`, not the
volatile file:line prefix), translated to teaching copy, and the raw string is suppressed from the
footer. (4) **attribution (P3):** the teaching error is attached to the deficient (empty) trio field
computed client-side (the editable trio seed(s) empty while the version is set), never the populated
fields. The editable trio set is single-sourced from `AUDIT_TRIO_SEEDS`. Validity stays the API's
`res.valid` (law 6 — only the displayed string + its attribution are client-side).

**Verification:** vitest cause-tests in `FieldEditor.test.tsx` — the version-empty lock + D29
suppression; the version-set blank-reject + non-empty-retype-allowed + the legible note (P1); the
raw-string-suppressed + teaching-copy-rendered (P2); the error-attached-to-deficient-not-populated (P3).
Build green; vitest 604/604. Gate A architect-review settled both forks (1b block-the-blank, 2a
teaching-copy + deficient-attribution); Gate B root-cause-verifier `land-fix` (re-derived the mechanism
for all four inevitabilities; confirmed no third orphan path in s04 — `revertField`, the Custom-version
exit, and the D29 guard all checked). User-confirmed the repro 2026-06-15: blanking a trio cell is
blocked with the inline note, re-typing works, clearing the version un-verifies cleanly, no raw
validator string in the UI.

**Follow-ups surfaced (not part of this close — the user's decisions):**
- The general validator-message → teaching-copy translation layer ("option 2c", covering every
  validator message, not just the trio-incomplete verdict) — DEFERRED by the user at the Fork-2
  decision; recorded for a future cycle.
- The s05 `CreateVersionForm` has the SAME blank-trio (P1) shape on a different surface (the create
  path, the KI-0021 family) — uncovered by this s04-scoped fix; the validator still rejects the orphan
  loud there (no silent save). Surfaced by Gate B; the user's fix-now / next-cycle / scope call.
