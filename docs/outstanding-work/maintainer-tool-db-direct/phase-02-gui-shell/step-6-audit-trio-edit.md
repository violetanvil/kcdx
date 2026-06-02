# Step 6 — audit-trio edit form + inline validation (US-3)

**What.** Implement US-3: the maintainer edits the four audit-trio fields on the
selected row — `last_verified_at_version`, `verified_by`, `verified_date`,
`evidence_kind`. `evidence_kind` is picked from the `policy.md` enum (not free
text). Inline, field-level validation rejects a malformed `verified_date` shape,
an out-of-enum `evidence_kind`, or a partial trio (the trio is all-set-or-all-null
per `policy.md`) BEFORE any write is attempted — by calling the shared validator
(R3), not reimplementing the rules in the GUI.

**Scope.** The edit form for the four trio fields + the `evidence_kind` enum
picker + the inline-validation feedback (calling `seeds_shared/validators.py`). No
write/save yet (step 7) — this step makes the fields editable + validated; Save is
the next step's gate. The read-only triple (step 5) stays non-editable.

**Test bar.** The validation rules are headless (`validators.py`, already
exercised by the data-core oracles). The GUI's inline-error rendering (showing the
validator's verdict on the offending field, blocking before write) is verified at
the phase's user-facing acceptance gate. No new data-core logic — the GUI binds
the existing validator.

**Dependencies.** Step 5 (a picked entity + its current row rendered, with the
trio fields shown). This step makes those fields editable + validated. Sequenced
after step 5.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3 + §7 (the validation-error state) + the R3 single-validator invariant
(plan-spec cross-step invariants). `policy.md` §"Verification audit trail" (the
trio fields, formats, all-set-or-all-null) + §"evidence_kind enum".

**UX** (`.claude/rules/ux-first-class.md`, from design §7):
- **Editable** — the four trio fields editable; `evidence_kind` is an enum picker
  (the `policy.md` ranked enum), not a free-text box; `kcdx_id`/`name`/
  `valid_from_version` remain read-only (step 5).
- **Validation error** — inline, field-level, on the offending field; NO write
  attempted; the error names what's wrong (bad date shape, out-of-enum evidence
  kind, partial trio). The Save action (step 7) is the gate; this step surfaces the
  validator's verdict at edit time so the maintainer fixes before saving.
- **Flow + feedback:** edit a field → inline validity feedback → the form
  signals when the prospective edit is valid (ready to Save). No silent
  acceptance of an invalid value.
- **Accessibility + consistency:** labelled fields, keyboard-reachable, the enum
  picker keyboard-operable; error text is readable (not color-only).

**Disassembler-test / author-burden.** N/A — re-verify edits the audit trio only;
no game-function address/ABI authored (RVA/signature unchanged on re-verify).
