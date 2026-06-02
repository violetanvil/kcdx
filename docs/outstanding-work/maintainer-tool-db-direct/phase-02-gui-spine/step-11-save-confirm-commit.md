# Step 11 — s06 save-confirm (field delta + approval/override) + atomic commit

**What.** Build the save-confirm modal (s06) + the atomic commit transaction — the
shared save spine every mutating job reuses. On `[Review changes]`, the modal shows the
**plain-language field delta** (`field: old → new`, only changed fields — D8, from the
Phase-1 field-delta unit step 6); on Confirm it runs ONE atomic transaction (law 5):
shared-validator gate (R3) → atomic DB write (`db_editor`, step 3) → auto-export the 3
CSVs (`csv_exporter`, step 1, diff-preserved) → round-trip oracle asserts identity
(step 2) → **commit** (DB + 3 CSVs, exact-path staging, respect a live `index.lock` →
block-and-retry never reap, self-authored message — D6, `concurrency-git.md`). The
result reads "Saved `<entity> <version>`" in the status bar (s07) — git is invisible
(law 5). A write failure rolls back fully (the DB + CSVs return to pre-action). The
approval banner (law 8, AP18) + the "I accept — save anyway" override (law 4) render
here when applicable, but their TRIGGERS (a new row, an unresolved verify state) arrive
in Phase 3 — in Phase 2 the spine covers the existing-row UPDATE save (no approval, no
override needed yet; the modal renders those affordances conditionally).

**Scope.** The save-confirm modal (the field-delta list + Confirm/Cancel), the atomic
transaction wiring (GUI orchestrates the data-core chain — it adds no validation/SQL/
export logic of its own, thin-shell law 6), the commit (exact-path, lock-respecting),
the write-failure rollback state, and the status-bar save-result segment (the s07 part
this step needs; the DLL-link part of s07 is step 12). The approval/override
affordances are rendered conditionally (their triggers are Phase 3).

**Test bar.** The chain's units are oracle-tested in Phase 1 (write, export,
round-trip, field-delta). This step's GUI orchestration + the field-delta rendering +
the atomic-commit + the rollback-on-failure are verified at the phase's user-facing
acceptance gate — the maintainer saves an edited existing row and sees the field delta,
then "Saved"; a forced-invalid / forced-failure case shows the rollback with nothing
landed; a forced live-lock shows block-and-retry (never a reap).

**Test bar runnable now?** Yes — the spine saves an existing-row UPDATE end-to-end the
moment this step lands (its data-core units + step 10's editor exist); the
create/lifecycle triggers are Phase 3, but the SAVE mechanism is fully exercisable now
via the UPDATE path.

**Dependencies.** Step 10 (a validated dirty edit to save). Phase 1 steps 1
(`csv_exporter`), 2 (round-trip), 3 (`db_editor` UPDATE), 6 (field-delta). Sequenced
after step 10 — the save chain cannot run until its parts + an edit exist.

**Design authority.** [`data/maintainer-tool/ui/screens/s06-save-confirm.md`](../../../../data/maintainer-tool/ui/screens/s06-save-confirm.md)
(the field-delta modal, the approval/override conditionals, the result states) +
[`s07-status-dll-link.md`](../../../../data/maintainer-tool/ui/screens/s07-status-dll-link.md)
(the save-result status segment). [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-4 + §7 (write-failure + field-delta-confirm + save-result states) + §8 (the
commit discipline) + §10 D5/D6/D8. `.claude/rules/concurrency-git.md` (exact-path
staging, live-lock respect).

**UX** (`.claude/rules/ux-first-class.md`, from s06/s07):
- **Field-delta confirm** — the changed fields as `old → new`; Confirm/Cancel. The
  acceptance moment; the human reads fields, not CSV cells (D8).
- **Committing** — a brief "Saving…" state; the modal closes on success.
- **Write failure** — *"Save failed — nothing was written. `<reason>`."*; Confirm
  becomes Retry; the DB + CSVs are pre-action (law 5). System-caused copy.
- **Save blocked** — *"Save blocked — the files are locked by another process. Retry
  in a moment."* + Retry; never reaps the lock (§8). The maintainer never learns it's
  "git" (law 5 — git invisible).
- **Flow + feedback:** Review changes → field delta → Confirm → Saving… → "Saved
  `<entity> <version>`" in the status bar (or Cancel → back to the editor, nothing
  lands). No silent success, no dead-end error.
- **Accessibility + consistency:** the field-delta list keyboard-reachable + scrollable;
  Confirm/Cancel reachable; values in `mono` where tabular (versions/rva/signature).

**Disassembler-test / author-burden.** N/A — orchestrates the save of an
already-authored edit; no author-facing game-function input.
