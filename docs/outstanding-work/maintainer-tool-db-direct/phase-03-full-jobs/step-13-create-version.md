# Step 13 — s05 create new version (Job 6)

**What.** Build the create-new-version flow (s05, Job 6 / US-6): from s02/s03's
`[+ New version]`, open the field-editor pattern prefilled from a chosen source row —
**all columns including the audit trio** — with a "NEW VERSION (from `<source>`)"
banner. `valid_from_version` is the field the maintainer sets (prefilled from the linked
DLL's resolved version when linked — step 12; editable regardless — law 4). On save it
calls the Phase-1 INSERT shape (step 4) through the spine's confirm + atomic commit
(step 11). Two guards: the **AP18 approval** in the confirm (law 8, D11 — a new row
grows the library) and the **nothing-changed guard** (D12 — a new version identical to
its source except `valid_from_version` is BLOCKED with steering copy routing to
re-verify the existing row instead).

**Scope.** The new-version create form (prefill, the `valid_from_version` field, the
banner), the nothing-changed guard rendering (the data-core signal is step 4), and the
AP18 approval rendering in the confirm. Reuses the field editor (step 10), the INSERT
shape (step 4), the spine confirm/commit (step 11), the DLL-resolved version (step 12).

**Test bar.** The INSERT + tuple-uniqueness + AP18-flag + nothing-changed signal are
oracle-tested in Phase 1 (step 4). The GUI create form (prefill, the `valid_from`
field, the nothing-changed steering banner, the approval gate) is verified at the
phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the data-core INSERT/nothing-changed oracle is proven
(step 4); the create form is an eyeball gate (its data-core + spine deps exist).

**Dependencies.** Phase 1 step 4 (INSERT + nothing-changed signal + AP18 flag). Phase 2
steps 10 (the field-editor pattern) + 11 (the spine confirm/commit + the approval/
override affordances). Step 12 (the DLL-resolved `valid_from_version` prefill). Sequenced
after step 12.

**Design authority.** [`data/maintainer-tool/ui/screens/s05-create.md`](../../../../data/maintainer-tool/ui/screens/s05-create.md)
§"New version" (prefill-all, the `valid_from` field, the nothing-changed guard) +
[`s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the approval gate). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-6 + §10 D11/D12. `data/seeds/policy.md` §"DB additions require explicit approval"
+ the tuple-uniqueness rule.

**UX** (`.claude/rules/ux-first-class.md`, from s05):
- **Populated** — the prefilled form + the source banner; `valid_from_version` editable
  (prefilled from the linked DLL when linked).
- **Validation error** — inline (shared validator): a malformed `valid_from_version`,
  a duplicate `(kcdx_id, valid_from_version)` tuple (HARD ERROR), a partial trio.
- **Nothing-changed** — the steering banner blocks save: *"Nothing changed from version
  `<source>`. … instead re-verify the existing row for the game version you're
  targeting."* (D12) — routes to the correct action.
- **Unlinked / resolver-failure** — `valid_from_version` can't auto-resolve: advisory
  warning + the "I accept — save anyway" override (law 4); the maintainer types it.
- **Approval** — the AP18 banner in the confirm; Confirm disabled until acknowledged
  (law 8).
- **Edge content** — a long prefilled signature wraps; the form scrolls within the
  modal rather than growing past the window.
- **Flow + feedback:** + New version → prefilled form → set `valid_from` + edit →
  Review changes → field delta + AP18 approval → Confirm → Saved. Cancel → nothing
  lands.
- **Accessibility + consistency:** labelled fields, keyboard-reachable, the approval
  acknowledge keyboard-operable.

**Disassembler-test / author-burden.** The `valid_from_version` is RESOLVED from the
linked DLL (step 12) — the engine carries the version, the maintainer doesn't hand-type
it. A genuinely new resolve fact (`rva`/`signature`) on the new version is the
expert-only authoring case (`cornerstones.md`), AP18-gated — the deliberate
create-a-target act, not a common per-hook task.

**AP18.** A new version row is a DB addition — the approval gate in the confirm (law 8,
D11) is the maintainer sign-off `policy.md` requires before it lands.
