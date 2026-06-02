# Step 15 — s02 lifecycle editing (supersede / deprecate, Jobs 4/5)

**What.** Make the entity-header lifecycle flags (s02) editable: the supersede form
(`superseded_by` via a target-entity dropdown + `superseded_at_version`) and the
deprecate form (`is_deprecated` toggle + `deprecated_at_version` +
`deprecation_replacement` dropdown), plus the entity `notes`. On save it calls the
Phase-1 lifecycle UPDATE (step 5) through the spine's confirm + atomic commit (step 11).
The shared validator enforces pair-integrity (both-or-neither), no-self-supersede,
acyclicity, and replacement-requires-deprecated (law 6) and renders its verdict inline.
These are UPDATEs to an existing entity — NOT AP18-gated (law 8 gates additions only).

**Scope.** The s02 lifecycle edit forms (the supersede/deprecate field rows + the
target/replacement dropdowns + `notes`), wired to the lifecycle UPDATE (step 5) + the
spine confirm (step 11). No new approval (it's an UPDATE). This completes s02 (read view
was step 9; this adds its editable lifecycle part).

**Test bar.** The pair-integrity + acyclicity + replacement rules are oracle-tested in
Phase 1 (step 5). The GUI lifecycle forms (the paired fields rendered together, the
dropdowns over the entity set, the inline validation verdict) are verified at the
phase's user-facing acceptance gate.

**Test bar runnable now?** Yes — the lifecycle UPDATE oracle is proven (step 5); the
forms are an eyeball gate (s02 from step 9 + the spine from step 11 exist).

**Dependencies.** Phase 1 step 5 (the lifecycle UPDATE + pair-integrity/acyclicity).
Phase 2 steps 9 (the s02 header these forms make editable) + 11 (the spine confirm/
commit). Sequenced after step 9's read view + the spine.

**Design authority.** [`data/maintainer-tool/ui/screens/s02-entity-detail.md`](../../../../data/maintainer-tool/ui/screens/s02-entity-detail.md)
§"Lifecycle" (the supersede/deprecate field rows + pair-integrity). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-8. `data/seeds/policy.md` §"Supersession" (pair integrity, no self-supersede,
acyclicity) + §"Deprecation" (pair integrity, replacement-requires-deprecated).

**UX** (`.claude/rules/ux-first-class.md`, from s02):
- **Editing** — the paired lifecycle fields rendered together (supersede: both fields;
  deprecate: the toggle + version + optional replacement); dirty markers + "was:" lines
  (law 1, as in the field editor).
- **Validation error** — inline (shared validator): a partial pair, a self-supersede, a
  cycle, a `deprecation_replacement` without `is_deprecated` — each named; no write.
- **Disabled** — an illegal transition (e.g. on an already-superseded entity) is gated
  by the validator with an inline error, not silently hidden.
- **Edge content** — a long `notes` wraps within its well without pushing siblings
  (law 1).
- **Flow + feedback:** edit a lifecycle field → dirty + inline validity → Review
  changes → field delta → Confirm → Saved. The entity's status chip (s01) updates in
  place after save (law 3).
- **Accessibility + consistency:** labelled paired fields, the dropdowns keyboard-
  operable, error text not color-only.

**Disassembler-test / author-burden.** N/A — supersede/deprecate edits entity-level
flags + a successor/replacement NAME (resolved from the existing entity set via a
dropdown); no game-function address/ABI authored.
