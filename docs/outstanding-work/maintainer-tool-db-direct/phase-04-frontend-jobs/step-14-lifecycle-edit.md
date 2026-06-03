# Step 14 — s02 lifecycle editing (supersede / deprecate, Jobs 4/5)

**What.** Make the entity-header lifecycle flags (s02) editable: the supersede form
(`superseded_by` via a Mantine `Select` over the entity set + `superseded_at_version`) and the
deprecate form (`is_deprecated` `Checkbox` + `deprecated_at_version` + `deprecation_replacement`
`Select`), plus the entity `notes`. On save it calls the Phase-2 save API (`supersede_entity`
/ `deprecate_entity`) through the spine's confirm + atomic commit (Phase 3 step 10). The
validator (via the API) enforces pair-integrity (both-or-neither), no-self-supersede,
acyclicity, and replacement-requires-deprecated (law 6) and renders its verdict inline. These
are UPDATEs to an existing entity — NOT AP18-gated (law 8 gates additions only).

**Scope.** The s02 lifecycle edit forms (the supersede/deprecate field rows + the
target/replacement `Select`s + `notes`), wired to the lifecycle save API (Phase 2 step 4) +
the spine confirm (step 10). No new approval (it's an UPDATE). This completes s02 (the read
view was Phase 3 step 8; this adds its editable lifecycle part).

**Test bar.** A component test (Vitest + Testing Library): the paired lifecycle fields render
together; an invalid edit (partial pair, self-supersede, cycle, replacement-without-deprecated)
shows the validator's inline error (via the API) + no save; a valid supersede/deprecate lands
via the Phase-2 lifecycle save API (mocked in unit; real at acceptance). Runnable now (the
Phase-2 lifecycle save exists).

**Dependencies.** Phase 2 step 4 (the lifecycle save API). Phase 3 steps 8 (the s02 header
these forms make editable) + 10 (the spine confirm/commit). Sequenced after step 8's read
view + the spine.

**Design authority.** [`data/maintainer-tool/ui/screens/s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
§"Lifecycle" (the supersede/deprecate field rows + pair-integrity). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-8. `policy.md` §"Supersession" + §"Deprecation".

**UX** (`.claude/rules/ux-first-class.md`, from s02):
- **Editing** — the paired lifecycle fields together (supersede: both fields; deprecate: the
  toggle + version + optional replacement); dirty markers + "was:" lines (law 1).
- **Validation error** — inline (the validator via the API): a partial pair, a self-supersede,
  a cycle, a `deprecation_replacement` without `is_deprecated` — each named; no write.
- **Disabled** — an illegal transition is gated by the validator with an inline error, not
  silently hidden.
- **Responsive** — wide: the lifecycle forms in the s02 header. Phone: within the detail
  drill-down (the forms stack; the confirm is a full-screen sheet). The status chip (s01)
  updates in place after save (law 3).
- **Accessibility + consistency:** labelled paired fields, the `Select`s keyboard/touch-
  operable, error text not color-only.

**Disassembler-test / author-burden.** N/A — supersede/deprecate edits entity-level flags +
a successor/replacement NAME (a `Select` over the entity set); no game-function address/ABI
authored.
