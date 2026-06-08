# Step 2 — verified_date read-only + shown-only-when-verified (D17b)

## What

Make `verified_date` a read-only SYSTEM fact in the s04 field editor (TRD D17b): it is set
automatically to today when the maintainer verifies the row (sets `last_verified_at_version`),
is **never editable** (no hand-typed date), and is **shown ONLY when the row is verified**
(`last_verified_at_version` non-empty) — removed from the always-shown set, so an unverified row
shows no `verified_date` cell at all. The all-or-null trio coupling (`applyAuditTrioCoupling`)
keeps setting it to today on verify and clearing it with the trio; this step changes only that it
is system-set (read-only) and conditionally rendered. Today `verified_date` is a `render: "text"`
editable field in the `ALWAYS_SHOWN` set — this step supersedes that.

## Scope

One commit in the frontend repo (`data/maintainer-tool/frontend/`):
- `fieldModel.ts` — change `verified_date`'s render to a READ-ONLY kind (a new read-only render
  treatment, or reuse the existing read-only mechanism `valid_from_version` uses — it is already
  rendered read-only as the identity key); REMOVE `verified_date` from the always-shown set; add the
  conditional-render predicate (shown iff `last_verified_at_version` is non-empty on the prospective
  row). Keep `applyAuditTrioCoupling`'s set-to-today-on-verify / clear-with-trio for `verified_date`.
- The field-relevance / visible-field computation — honor the new conditional (an unverified row
  drops the `verified_date` cell entirely; a verified row shows it read-only).
- The s04 validation surface — drop the maintainer-facing "malformed verified_date" entry path (the
  FE no longer authors the date); the backend validator stays the authority on the date's shape
  (law 6) — do NOT reimplement validation in the FE.
- `FieldRow` (or the render switch) — render the read-only `verified_date` as a non-editable display
  (law 7 — visibly non-editable, not just disabled-by-color), consistent with the existing read-only
  `valid_from_version` treatment.

## Test bar

Vitest in the FE repo (runnable AT this step — the field editor + the trio coupling exist):
- `verified_date` renders ONLY when `last_verified_at_version` is non-empty (an unverified row has
  no `verified_date` cell — assert its absence; a verified row has it — assert presence);
- `verified_date` is READ-ONLY (no editable input element; the maintainer cannot type into it) —
  falsifiable: attempting to edit it does not change its value / there is no input;
- on verify (setting `last_verified_at_version`), `verified_date` is system-set to today
  (`todayIsoLocal()`), set together with the rest of the trio; clearing `last_verified_at_version`
  clears `verified_date` with the trio (the all-or-null invariant holds);
- the validation surface no longer offers a maintainer-facing malformed-date path (assert no FE
  date-entry validation is surfaced — the field is read-only).

## Dependencies

None on Step 1 — independent surface (Step 1 touches the confirm-author path; this touches the
field render + the trio coupling). Either order works; sequenced after Step 1 only by convention.
Rests on the existing `fieldModel.ts` field definitions + `applyAuditTrioCoupling` + the read-only
render mechanism `valid_from_version` already uses (all in the FE repo at HEAD).

## Reference

- [`../plan-spec.md`](../plan-spec.md) — the coverage map (the verified_date rows) + the grounding facts.
- **Design authority:** `data/maintainer-tool/design.md` §6 US-3 (the `verified_date` rule) + §10 D17b;
  `data/maintainer-tool/ui/screens/s04-field-editor.md` §"Contents" (the `verified_date` field row),
  §"Field relevance by kind" (the always-shown set — `verified_date` NOT in it; conditional render),
  §"Validation" (the dropped malformed-date path). Build to these, not to this doc's summary.

## UX

Carried from the s04 spec (`.claude/rules/ux-first-class.md` — not invented):
- **Unverified row (empty/default state for verified_date)** — NO `verified_date` cell is shown;
  the grid renders without it (the field is simply absent until the row is verified). This is the
  user's explicit "shouldn't show unless actually verified."
- **Verified row** — `verified_date` shows read-only, displaying the system-set date (today, set on
  verify), in the law-7 read-only treatment (visibly non-editable, like `valid_from_version`).
- **On verify (the transition)** — when the maintainer sets `last_verified_at_version`, the
  `verified_date` cell APPEARS (system-set to today) alongside the rest of the trio; the field grid
  reflows to include it. When `last_verified_at_version` is cleared, the cell DISAPPEARS with the
  trio. (The reserved-space / no-reflow law-1 treatment applies within a shown cell; the appear/
  disappear of the whole cell on verify is a user-action-driven state change, not a background reflow.)
- **No editable input, no error state for verified_date** — the maintainer cannot type it, so there
  is no malformed-date error path on this field (the validator stays the backend authority, law 6).
- **Consistency** — reuses the existing read-only render `valid_from_version` uses; no one-off
  read-only treatment.

## Disassembler-test / author-burden

N/A — no author-facing game-binary input is added (this is a field-render + coupling change; no
address/offset/signature is involved).
