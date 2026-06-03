# Step 13 — s05 create new entity (Job 1)

**What.** Build the create-new-entity flow (s05, Job 1 / US-7): from the navigator's `+ New
entity`, open an overlay (Mantine `Modal` on wide, full-screen sheet on phone — law 2) that
**displays the next free `kcdx_id`** (assigned by the backend / data-core — `policy.md`
§"ID assignment"; the maintainer never types it), takes a `name`, and authors the first
`address_versions` row (the field-editor pattern; required: `valid_from_version`, `module`,
`kind`; the audit trio may be set or left all-null for a brand-new unverified row). On save it
calls the Phase-2 save API (`create_entity`) through the spine's confirm + atomic commit
(Phase 3 step 10), AP18-gated (law 8, D11). After save, the new entity appears selected in s02.

**Scope.** The new-entity overlay (the assigned-id display, the `name` field, the first-version
field editor), wired to the `create_entity` save API (Phase 2 step 4) + the spine confirm
(step 10) with the AP18 approval.

**Test bar.** A component test (Vitest + Testing Library): the assigned `kcdx_id` displays
(read-only, from the API); the `name` + first-row form validate (the validator via the API);
the AP18 approval gates Confirm; the save lands via the Phase-2 `create_entity` API (mocked in
unit; real at acceptance — both names + first-version rows land atomically). Runnable now (the
Phase-2 new-entity INSERT + id-assignment exist).

**Dependencies.** Phase 2 step 4 (the `create_entity` save API + the next-free id). Phase 3
steps 9 (the field-editor pattern) + 10 (the spine confirm/commit). Phase 3 step 7 (the
navigator's `+ New entity` placement). Sequenced after step 12 (both reuse the create/confirm
machinery).

**Design authority.** [`data/maintainer-tool/ui/screens/s05-create.md`](../../../../data/maintainer-tool/ui/screens/s05-create.md)
§"New entity" (assigned id, name, first version row) +
[`s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the approval gate). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-7 + §10 D11. `policy.md` §"ID assignment" (append-only, next-free) + §"DB additions
require explicit approval" (AP18) + §"Required columns".

**UX** (`.claude/rules/ux-first-class.md`, from s05):
- **Populated** — the overlay: assigned `kcdx_id` (read-only), `name`, the first-version
  field editor.
- **Validation error** — inline (the validator via the API): a missing required column, a
  partial audit trio, an unresolvable `module`.
- **Approval** — the AP18 banner in the confirm; Confirm disabled until acknowledged (law 8).
- **Unverified / resolver-failure** — `valid_from_version` not auto-resolved: advisory
  warning + override (law 4); the maintainer picks/types it.
- **Responsive** — wide: a centered `Modal`. Phone: a full-screen sheet (body scrolls;
  Review changes pinned). On save the new entity is selected in s02.
- **Accessibility + consistency:** the assigned-id read-only treatment (law 7); labelled
  fields, keyboard-reachable + touch.

**Disassembler-test / author-burden.** The tool ASSIGNS the `kcdx_id` (never hand-typed) and
the version resolves from a linked DLL (step 11). The `rva`/`signature` for a genuinely new
target IS the maintainer's to supply — the expert-only authoring case (`cornerstones.md`),
AP18-gated; the deliberate mint-a-new-curated-target act, not a common per-hook task.
