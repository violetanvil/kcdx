# Step 14 — s05 create new entity (Job 1)

**What.** Build the create-new-entity flow (s05, Job 1 / US-7): from the navigator's
`+ New entity`, open a modal that **assigns the next free `kcdx_id`** (read-only,
tool-assigned — `policy.md` §"ID assignment"; the maintainer never types it), takes a
`name`, and authors the first `address_versions` row (the field-editor pattern;
required: `valid_from_version`, `module`, `kind`; the audit trio may be set or left
all-null for a brand-new unverified row). On save it calls the Phase-1 new-entity INSERT
(step 4) through the spine's confirm + atomic commit (step 11), AP18-gated (law 8, D11 —
a new entity grows the library).

**Scope.** The new-entity modal (the assigned-id display, the `name` field, the
first-version field editor), wired to the new-entity INSERT (step 4) + the spine confirm
(step 11) with the AP18 approval. After save, the new entity appears selected in s02.

**Test bar.** The new-entity INSERT (id-assignment, the two-row atomic write,
required-column, AP18 flag) is oracle-tested in Phase 1 (step 4). The GUI create modal
(the assigned-id display, the first-row form, the approval gate) is verified at the
phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the new-entity INSERT oracle is proven (step 4); the
modal is an eyeball gate (its deps exist).

**Dependencies.** Phase 1 step 4 (the new-entity INSERT + id-assignment + AP18 flag).
Phase 2 steps 10 (the field-editor pattern for the first row) + 11 (the spine confirm/
commit + approval). Step 8 (the navigator's `+ New entity` placement). Sequenced after
step 13 (both reuse the create/confirm machinery; entity builds on the version-create
form pattern).

**Design authority.** [`data/maintainer-tool/ui/screens/s05-create.md`](../../../../data/maintainer-tool/ui/screens/s05-create.md)
§"New entity" (assigned id, name, first version row) +
[`s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the approval gate). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-7 + §10 D11. `data/seeds/policy.md` §"ID assignment" (append-only, next-free) +
§"DB additions require explicit approval" (AP18) + §"Required columns".

**UX** (`.claude/rules/ux-first-class.md`, from s05):
- **Populated** — the modal: assigned `kcdx_id` (read-only), `name` field, the
  first-version field editor.
- **Validation error** — inline (shared validator): a missing required column
  (`valid_from_version`/`module`/`kind`), a partial audit trio, an unresolvable
  `module`.
- **Approval** — the AP18 banner in the confirm; Confirm disabled until acknowledged
  (law 8).
- **Unlinked / resolver-failure** — `valid_from_version` not auto-resolved: advisory
  warning + override (law 4); the maintainer types it.
- **Edge content** — a long first-version signature wraps; the modal scrolls internally.
- **Flow + feedback:** + New entity → modal (assigned id) → name + first row → Review
  changes → field delta + AP18 approval → Confirm → Saved, new entity selected in s02.
  Cancel → nothing lands.
- **Accessibility + consistency:** the assigned-id read-only treatment (law 7);
  labelled fields, keyboard-reachable; the approval acknowledge keyboard-operable.

**Disassembler-test / author-burden.** The tool ASSIGNS the `kcdx_id` (never hand-typed)
and resolves `valid_from_version` from a linked DLL (step 12). The `rva`/`signature` for
a genuinely new game-function target IS the maintainer's to supply — the expert-only
authoring case (`cornerstones.md`), AP18-gated; this is the deliberate
mint-a-new-curated-target act, not a common per-hook task the engine should pre-name.

**AP18.** A new entity is a DB addition — the approval gate in the confirm (law 8, D11)
is the maintainer sign-off `policy.md` requires before it lands.
