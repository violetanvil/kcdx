# Step 12 — s05 create new version (Job 6)

**What.** Build the create-new-version flow (s05, Job 6 / US-6): from s02/s03's `[+ New
version]`, open the field-editor pattern as an overlay (Mantine `Modal` on wide, full-screen
sheet on phone — law 2) prefilled from a chosen source row (**all columns incl. the audit
trio**) with a "NEW VERSION (from `<source>`)" banner. `valid_from_version` is the field the
maintainer sets — prefilled from the version&verify surface's pick or the client-DLL-resolved
version (step 11), editable regardless (law 4). On save it calls the Phase-2 save API
(`create_version`) through the spine's confirm + atomic commit (Phase 3 step 10). Two guards:
the **AP18 approval** in the confirm (law 8, D11) and the **nothing-changed guard** (D12 — a
new version identical to its source except `valid_from_version` is BLOCKED with steering copy
routing to re-verify; the backend computes the verdict, the frontend renders the steering).

**Scope.** The new-version create overlay (prefill, the `valid_from_version` field, the
banner), the nothing-changed steering rendering (the backend signal — Phase 2), and the AP18
approval rendering in the confirm. Reuses the field editor (Phase 3 step 9), the spine
confirm/commit (step 10), the client-resolved version (step 11), the Phase-2 save API.

**Test bar.** A component test (Vitest + Testing Library): the form prefills all columns from
the source; `valid_from_version` is settable (prefilled from the resolved/picked version);
the nothing-changed steering banner blocks save when nothing differs; the AP18 approval gates
Confirm. The save lands via the Phase-2 `create_version` API (mocked in unit; real at
acceptance). Runnable now (the Phase-2 INSERT save + the nothing-changed signal exist).

**Dependencies.** Phase 2 step 4 (the `create_version` save API). Phase 3 steps 9 (the
field-editor pattern) + 10 (the spine confirm/commit + the approval/override affordances).
Step 11 (the resolved-version prefill). Sequenced after step 11.

**Design authority.** [`data/maintainer-tool/ui/screens/s05-create.md`](../../../../data/maintainer-tool/ui/screens/s05-create.md)
§"New version" (prefill-all, the `valid_from` field, the nothing-changed guard) +
[`s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the approval gate). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-6 + §10 D11/D12. `policy.md` §"DB additions require explicit approval" + the
tuple-uniqueness rule.

**UX** (`.claude/rules/ux-first-class.md`, from s05):
- **Populated** — the prefilled form + the source banner; `valid_from_version` editable.
- **Validation error** — inline (the validator via the API): a malformed `valid_from_version`,
  a duplicate `(kcdx_id, valid_from_version)` tuple (HARD ERROR), a partial trio.
- **Nothing-changed** — the steering banner blocks save (D12) → routes to re-verify.
- **Unverified / resolver-failure** — `valid_from_version` can't auto-resolve: advisory
  warning + override (law 4); the maintainer picks/types it.
- **Approval** — the AP18 banner in the confirm; Confirm disabled until acknowledged (law 8).
- **Responsive** — wide: an inline prefilled form in the detail. Phone: a full-screen sheet
  (the body scrolls; `[Review changes]` pinned reachable at the bottom). On save the new row
  appears selected in s02.
- **Accessibility + consistency:** labelled fields, keyboard-reachable + touch, the approval
  acknowledge keyboard/touch-operable.

**Disassembler-test / author-burden.** `valid_from_version` is RESOLVED from the linked DLL
(step 11) — the engine carries the version. A genuinely new resolve fact (`rva`/`signature`)
is the expert-only authoring case (`cornerstones.md`), AP18-gated — the deliberate
create-a-target act, not a common per-hook task.
