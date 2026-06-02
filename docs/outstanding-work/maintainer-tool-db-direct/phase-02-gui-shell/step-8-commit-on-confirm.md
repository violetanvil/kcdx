# Step 8 — commit-on-Confirm, git-concurrency-safe (D6)

**What.** Implement D6: on Confirm (from step 7's diff view), the tool creates the
git commit landing the DB + the three exported CSVs. The commit obeys the existing
committer discipline (`.claude/rules/concurrency-git.md`), since the tool is
another writer of the shared `.git`/index: **exact-path staging** (only the DB +
the 3 seed CSVs — never `-A`/`.`/`-u`); **respect a live `index.lock`**
(block-and-retry, never reap a live lock); **self-authored commit message**. This
closes the Job-2 MVP — the maintainer's one Confirm lands the change end-to-end.

**Scope.** The Confirm → commit wiring + the commit-result state (success or
live-lock-retry). Exact-path staging of the DB + 3 CSVs; the index-lock
block-and-retry; message authorship. NO push, no amend, no branch creation
(`concurrency-git.md`). This is the last MVP step.

**Test bar.** The commit logic (exact-path staging, the live-lock detection +
retry, the message authorship) is headless — it ships a `tests/test_*.py` that
asserts: the commit stages exactly the DB + 3 CSVs (a foreign-staged file in the
index is NOT swept in), and a simulated live `index.lock` blocks-and-retries
rather than reaping. (The git operation is the load-bearing logic; it is reached
headless per `.claude/rules/headless-testable.md`, the GUI Confirm button a thin
trigger over it.) The end-to-end Confirm→committed path is confirmed at the
phase's user-facing acceptance gate.

**Dependencies.** Step 7 (the diff-confirm view + the written-on-disk DB + CSVs to
commit). Sequenced last — nothing to commit until the save chain has produced the
files.

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§8 (the commit constraint, verbatim: exact-path staging, live-lock respect,
self-authored message) + §10 D6 + §7 (the commit-result state).
`.claude/rules/concurrency-git.md` (the committer discipline the tool inherits —
exact-path staging, the `guard-git-lock.py` live-lock posture, no auto-branch, no
push).

**UX** (`.claude/rules/ux-first-class.md`, from design §7):
- **Commit result** — Confirm → committed (short-hash shown) OR commit-blocked (a
  live shared-index lock → retry guidance, never a forced reap). The maintainer
  sees the outcome of their Confirm.
- **Flow + feedback:** Confirm → commit → result line (hash, or "index busy,
  retrying"). No silent commit; no dead-end on a lock (it retries / tells the
  maintainer to retry).
- **Accessibility + consistency:** the result is readable text (hash / retry
  notice), keyboard-dismissable; consistent with the screen's feedback idiom.

**Disassembler-test / author-burden.** N/A — no author-facing game-function input.
