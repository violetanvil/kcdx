# Step 9 — s04 field editor (view / edit a version row, dirty markers, inline validation)

**What.** Build the field editor (s04) inline in the detail region below the version table:
render the selected version row's full columns, make the editable ones editable (everything
EXCEPT the identity key `valid_from_version` + entity identity — law 7), with **layout-stable
dirty feedback** (a changed field gets the `dirty` accent marker + a reserved "was: `<old>`"
line that never reflows — law 1) and **inline field-level validation** binding the validator
**via the Phase-2 API** (law 6 — the frontend reimplements no rule). `evidence_kind` / `kind`
/ `module` are Mantine `Select`s (gated). Editing an already-decided existing version raises
the **edit-existing confirmation** ("editing existing version `<v>`, not creating a new one"
— law 5 boundary) before fields unlock. `[Review changes (N)]` enables when ≥1 dirty AND all
dirty fields validate; it opens the save-confirm (step 10). No save/commit yet (step 10).

**Scope.** The field editor: read-only + editable rendering, the `Select`s, the dirty markers
+ "was:" lines, the inline validation (calling the API — the read/field-delta endpoints for
"was:" old values + the preview-only save endpoint `POST /save/update-version`, which validates
and returns `valid: true/false` + the validator's verdict without writing), the edit-existing confirmation, the
`[Review changes]` enablement. Covers the version-row UPDATE shape (audit trio + full column).
No save chain (step 10), no create (Phase 4).

**Test bar.** A component test (Vitest + Testing Library): a changed field shows the dirty
marker + "was:" line (no reflow — the reserved space is always present); an invalid value
shows the validator's inline error (from the API) + disables `[Review changes]`; the
edit-existing confirmation precedes unlocking. The validation verdicts come from the Phase-2
API (mocked in unit; real at acceptance). Runnable now (the read + field-delta + save-
validate API exist, Phase 2).

**Dependencies.** Step 8 (a selected version row rendered). Phase 2 steps 2 (read) + 3
(field-delta, for "was:" values) + 4 (save, for the validation verdict). Sequenced after
step 8.

**Design authority.** [`data/maintainer-tool/ui/screens/s04-field-editor.md`](../../../../data/maintainer-tool/ui/screens/s04-field-editor.md)
(the editable set, the dirty/validation/edit-existing behavior). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3/US-5 + §7 (validation-error state) + R3 (single validator, via the API). `policy.md`
§"Verification audit trail" + §"evidence_kind enum" + §"Address kinds".

**UX** (`.claude/rules/ux-first-class.md`, from s04):
- **View / editing** — the row's fields; editable ones unlock (after the edit-existing
  confirmation); a changed field shows the dirty marker + "was: `<old>`" (space always
  reserved — law 1).
- **Validation error** — inline, field-level, the validator's verdict (via the API); NO
  write; `[Review changes]` disabled while any dirty field is invalid; the error names the
  cause (user-caused copy).
- **Disabled** — read-only identity fields (law 7); a row viewed without the edit action
  renders read-only.
- **Edge content** — long signature / survival values wrap; the field list scrolls; survival
  columns grouped under a "Survival" sub-heading.
- **Responsive** — wide: inline below the version table. Phone: inline within the detail
  drill-down (scrolls); when reached from a compare column (s03, Phase 4) it opens as a
  full-screen sheet. The dirty/was/error reserved space holds at every width (law 1).
- **Accessibility + consistency:** labelled fields, keyboard-reachable + touch, the `Select`s
  keyboard/touch-operable, error text readable (not color-only), dirty marker glyph+color.

**Disassembler-test / author-burden.** N/A — editing an existing row's values; no NEW
game-function offset/ABI authored (that is the create flow, Phase 4).
