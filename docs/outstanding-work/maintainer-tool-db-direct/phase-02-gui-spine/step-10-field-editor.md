# Step 10 — s04 field editor (view / edit a version row, dirty markers, inline validation)

**What.** Build the field editor (s04) inline in the right pane below the version
table: render the selected version row's full columns, make the editable ones editable
(everything EXCEPT the identity key `valid_from_version` + entity identity — law 7),
with **layout-stable dirty feedback** (a changed field gets the `dirty` accent marker +
a reserved "was: `<old>`" helper line that never reflows — law 1) and **inline
field-level validation** binding the shared validator (`validators.py`, R3 — law 6),
never reimplementing a rule. `evidence_kind` / `kind` / `module` are dropdowns (gated).
Editing an already-decided existing version raises the **edit-existing confirmation**
("editing existing version `<v>`, not creating a new one" — law 5 boundary) before
fields unlock. `[Review changes (N)]` enables when ≥1 dirty AND all dirty fields
validate; it opens the save-confirm (step 11). No save/commit yet (step 11).

**Scope.** The field editor: read-only + editable field rendering, the dropdowns, the
dirty markers + "was:" lines, the inline validation (calling the data-core validator),
the edit-existing confirmation, the `[Review changes]` enablement. Covers the
version-row UPDATE shape (audit trio + full column — Phase 1 step 3). No save chain
(step 11), no create (Phase 3).

**Test bar.** The validation rules are headless (`validators.py`, oracle-tested in
Phase 1). The GUI's inline-error rendering (the validator's verdict on the offending
field, `[Review changes]` gated while invalid), the dirty markers + "was:" lines (no
reflow), and the edit-existing confirmation are verified at the phase's user-facing
acceptance gate. No new data-core logic — the GUI binds the existing validator +
field-delta (step 6, for the "was:" old values).

**Test bar runnable now?** Yes — the validator it binds is oracle-proven (Phase 1);
the GUI rendering is an eyeball gate at the phase acceptance (a selected row from step
9 is its input).

**Dependencies.** Step 9 (a selected version row rendered). Phase 1 steps 3 (the
UPDATE shape it will save via step 11) + 6 (the field-delta for "was:" values).
Sequenced after step 9.

**Design authority.** [`data/maintainer-tool/ui/screens/s04-field-editor.md`](../../../../data/maintainer-tool/ui/screens/s04-field-editor.md)
(the editable set, the dirty/validation/edit-existing behavior). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-3/US-5 + §7 (validation-error state) + R3 (single validator). `data/seeds/policy.md`
§"Verification audit trail" + §"evidence_kind enum" + §"Address kinds".

**UX** (`.claude/rules/ux-first-class.md`, from s04):
- **View / editing** — the row's fields; editable ones unlock (after the edit-existing
  confirmation); a changed field shows the dirty marker + "was: `<old>`" (space always
  reserved — law 1).
- **Validation error** — inline, field-level, the validator's verdict; NO write;
  `[Review changes]` disabled while any dirty field is invalid; the error names the
  cause (user-caused copy).
- **Disabled** — read-only identity fields (law 7); a row viewed without the edit
  action renders read-only.
- **Edge content** — long signature / survival values wrap within their well; the
  field list scrolls; survival columns grouped so the trio + resolve facts aren't
  buried.
- **Flow + feedback:** edit a field → dirty marker + "was:" + inline validity →
  `[Review changes]` enables when valid → opens the confirm (step 11). No silent
  acceptance of an invalid value.
- **Accessibility + consistency:** labelled fields, keyboard-reachable, dropdowns
  keyboard-operable, error text readable (not color-only), dirty marker is glyph+color.

**Disassembler-test / author-burden.** N/A — editing an existing row's values; no NEW
game-function offset/ABI authored (that is the create flow, Phase 3 step 13/14).
