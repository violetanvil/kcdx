# Step 10 — s06 save-confirm (field delta + approval) + toast + atomic save→commit

**What.** Build the save-confirm overlay (s06) + the toast layer — the shared save spine
surface every mutating job reuses. On `[Review changes]`, an overlay (Mantine `Modal`
centered on wide, full-screen sheet on phone — law 2) shows the **plain-language field
delta** (`field: old → new` — from the Phase-2 field-delta API, step 3); on Confirm it calls
the Phase-2 **save + commit API** (steps 4+5: validate → write → export → round-trip → commit
+ push, server-side — law 5/D16). The result surfaces as a **top-anchored toast**
(Mantine `Notification`): "Saved `<entity> <version>`" (success) or "Save blocked — files
locked, Retry" (a live lock — never reaps; git invisible, law 5). A write failure rolls back
(the DB + CSVs return to pre-action). The AP18 approval acknowledgment (law 8) + the "I accept
— save anyway" override (law 4) render conditionally (their triggers — a new row, an
unverified state — arrive in Phase 4; in Phase 3 the spine covers the existing-row UPDATE
save).

**Scope.** The save-confirm overlay (the field-delta list + Confirm/Cancel + the conditional
approval/override affordances), the call to the Phase-2 save+commit API, the **toast layer**
(the save-result `Notification`, top-anchored), and the write-failure state. No create/
lifecycle triggers (Phase 4). The frontend orchestrates by calling the API — no validation/
SQL/export/commit logic of its own (law 6).

**Test bar.** A component test (Vitest + Testing Library): Review changes opens the overlay
with the field delta (from the field-delta API); Confirm calls the save+commit API and shows
the success toast; a save-blocked response shows the blocked toast + Retry; a write-failure
response shows the rollback state. The API is the Phase-2 backend (mocked in unit; real at
acceptance — a real save→commit→push against the backend's test remote). Runnable now (the
Phase-2 save + commit + field-delta APIs exist).

**Dependencies.** Step 9 (a validated dirty edit to save). Phase 2 steps 3 (field-delta), 4
(save), 5 (commit+push). Sequenced after step 9 — the save chain cannot run until an edit +
the backend exist.

**Design authority.** [`data/maintainer-tool/ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the field-delta overlay, the approval/override conditionals, the toast concern, the result
states). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-4 + §7 (write-failure + field-delta-confirm + save-result states) + §8 (the commit) +
§10 D5/D8/D16.

**UX** (`.claude/rules/ux-first-class.md`, from s06):
- **Field-delta confirm** — the changed fields as `old → new`; Confirm/Cancel. The acceptance
  moment; the human reads fields, not CSV cells (D8).
- **Committing** — a brief "Saving…" state; the overlay closes on success → the success toast.
- **Write failure** — *"Save failed — nothing was written. `<reason>`."*; Confirm becomes
  Retry; pre-action state (law 5). System-caused copy.
- **Save blocked** — the blocked toast *"Save blocked — the files are locked by another
  process. Retry."* + Retry; never reaps (§8). The maintainer never learns it's "git".
- **Responsive** — wide: a centered `Modal`, the toast top-anchored. Phone: a full-screen
  sheet (the field delta full-viewport; Confirm/Cancel pinned reachable at the bottom), the
  toast top-anchored (out of thumb-reach of the bottom-pinned primary action).
- **Accessibility + consistency:** the field-delta list keyboard-reachable + scrollable;
  Confirm/Cancel reachable; values in `mono` where tabular.

**Disassembler-test / author-burden.** N/A — orchestrates the save of an already-authored
edit; no author-facing game-function input.
