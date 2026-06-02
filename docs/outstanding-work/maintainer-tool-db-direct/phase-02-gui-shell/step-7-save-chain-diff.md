# Step 7 — save chain: validate → write → export → round-trip → CSV diff (US-4, D5)

**What.** Implement the Job-2 save chain (US-4, design D5): on Save, the GUI runs
the full chain by calling the Phase-1 data-core — shared-validator gate (R3) →
atomic DB write (`db_editor`, step 3) → auto-export the three CSVs
(`csv_exporter`, step 1, diff-preserved) → round-trip oracle asserts identity
(step 2) → show the maintainer the git-style CSV diff (the exact changed cells)
with Confirm / Revert. The exported diff IS the user-facing acceptance signal
(`.claude/rules/acceptance-signal.md`). On Revert, nothing landed. Committing the
diff is the next step (step 8); this step ends at the diff-confirm view with the
files written-or-reverted on disk.

**Scope.** The Save action wiring (GUI → data-core chain), the round-trip
invocation, the CSV-diff rendering, the Confirm/Revert affordance, and the
write-failure (atomic rollback) state. NO git commit yet (step 8). The GUI
orchestrates the chain by calling data-core units — it adds no validation/SQL/
export logic of its own (thin-shell invariant).

**Test bar.** The chain's units are each oracle-tested in Phase 1 (`db_editor`
write, `csv_exporter` export, the round-trip). This step's GUI orchestration + the
diff rendering + the rollback-on-failure are verified at the phase's user-facing
acceptance gate — the maintainer saves and sees the correct CSV diff (the
acceptance signal); a forced-invalid or forced-failure case shows the rollback
with nothing landed.

**Dependencies.** Steps 1 (`csv_exporter`), 2 (round-trip), 3 (`db_editor`) — the
chain calls all three; they exist and are oracle-proven. Step 6 (a validated edit
to save). Sequenced after all of them — the chain cannot run until its parts
exist.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§6 US-4 (the full chain, verbatim) + §7 (the write-failure + diff-confirm states)
+ §10 D5 (the save/commit UX) + §4 (the round-trip the chain asserts).

**UX** (`.claude/rules/ux-first-class.md`, from design §7):
- **Diff-confirm** — after a successful write+export+round-trip: the git-style CSV
  diff showing ONLY the changed cells, with Confirm / Revert. This is the
  acceptance moment; the maintainer sees exactly the diff a reviewer will see.
- **Write failure** — the atomic DB transaction failed and rolled back; the seeds
  and DB are in their pre-action state; the maintainer sees the failure + that
  nothing landed. (A validation failure was already caught at step 6 before Save;
  this is a write-time failure.)
- **Flow + feedback:** Save → (validate → write → export → round-trip) → diff
  shown → Confirm/Revert. No silent success (the diff IS the confirmation), no
  dead-end error (a failure rolls back and says so).
- **Accessibility + consistency:** the diff view is readable + scrollable;
  Confirm/Revert are keyboard-reachable; the diff uses a consistent
  added/removed affordance that is not color-only.

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
