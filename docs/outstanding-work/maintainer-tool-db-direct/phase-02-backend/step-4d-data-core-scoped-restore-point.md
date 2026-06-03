# Step 4d — data-core SCOPED restore-point (D21; post-commit rollback)

**What.** Add a **scoped restore-point** capability to the data-core so a POST-commit failure
(export / integrity / git, which run AFTER the irreversible DB commit) can undo the committed
write — honoring the user's "on ANY failure nothing lands, incl. PK auto-increment reset" (D21).
The deferred-commit `ROLLBACK` (4a) covers a PRE-commit failure (validation); it is gone once
`commit(handle)` runs (which COMMITs + closes both connections, one-way). So a separate
restore-point — captured BEFORE the commit, restored on a post-commit failure — is required, and
it is a **data-core capability** (D13/law 6: it owns the write semantics, holds the open
connections, and knows exactly which rows the edit touched). It captures only the TOUCHED rows +
the `sqlite_sequence` values + the `data/db-export/` CSVs — a few KB regardless of DB size, NOT a
1.3 GB file copy (the rejected full-file snapshot, D21).

**Scope.**
- **A restore-point capture inside `apply_direct_edit` (4c), BEFORE the writes land.** The
  direct write already knows its actions (`_seed_action_rows` + the new-tag action) and the
  tables it touches: `address_versions` (the av INSERT/UPDATE + the interval-close),
  `survival` (the 1:1 sibling INSERT), `game_versions` (the new-tag INSERT), `address_names`
  (the entity INSERT + the lifecycle UPDATEs) — across BOTH DBs (USER + DEV). Capture, before
  applying: (a) the **current rows** of the touched (table, key) tuples the edit will
  INSERT/UPDATE (so a restore can re-establish or delete them), and (b) each touched DB's
  `sqlite_sequence` value for the autoincrement tables (`address_versions`, `survival`,
  `game_versions` — `address_names` is NOT autoincrement, its id is the kcdx_id). Carry the
  capture on the `DeferredCommit` handle (or a sibling restore-point object the handle holds).
- **A `restore(handle_or_restore_point)` entry the data-core exposes** — called by step 5 on a
  POST-commit failure. It re-opens (or reuses) connections and: DELETEs the rows the edit
  INSERTed (by their captured-absent keys), UPDATEs back the rows the edit changed (to the
  captured prior values), and resets each autoincrement table's `sqlite_sequence` to the
  captured value. Idempotent-safe. After `restore`, the DB is byte-identical to before the edit
  (incl. `sqlite_sequence`). The `data/db-export/` CSVs are reverted too (the step-5 backend
  owns the CSV revert, OR the data-core's restore takes the pre-edit CSV snapshot — decide the
  cleanest split: the CSV record is the backend's `data/db-export/` artifact, so the BACKEND
  may own the CSV revert while the data-core owns the DB-row+sequence restore; confirm + document
  the split at build time, keeping write-semantics in the data-core and file-artifact handling
  in the backend).
- **The capture is cheap + scoped:** only the rows the edit's actions name (a bounded set — one
  to a few rows per job shape) + the `sqlite_sequence` rows. NOT a `SELECT *` of any table, NOT
  a file copy. The capture happens only on the maintainer-tool deferred path (`defer_commit=True`)
  — the bootstrap/immediate path is unaffected (additive).

**Out of scope.** No backend endpoint changes (step 5 calls `restore`). No git/CSV-file handling
beyond deciding the data-core-vs-backend split for the CSV revert. The full-file snapshot the
step-5 WIP built (`restore_point.py`) is REMOVED in step 5 (replaced by the call to this
data-core capability). `apply_seeds`/`run_rebuild`/the immediate path stay byte-unchanged.

**Test bar (same change; the data-core test tree + the mini-dump fixture exist):**
a new `data/refdata-extractor/tests/test_restore_point.py`:
- **Capture-then-restore is byte-identical (the load-bearing proof):** for each job shape
  (re-verify, full-column UPDATE, create-version-at-current-tag-as-the-invalid-dup, create-entity,
  supersede, deprecate, create-version-at-a-NEW-tag): apply the edit deferred, COMMIT it (so the
  change is real), then call `restore` → assert the DB is **byte-identical to before the edit**
  (whole-DB per-table content fingerprint over BOTH DBs) INCLUDING `sqlite_sequence` (a subsequent
  add reuses the same next id — the PK-reset proof). This proves the scoped restore is COMPLETE
  (misses no touched row).
- **The new-tag restore:** a create-version-at-a-new-tag, committed, then restored → the new
  `game_versions` row is gone, the prior interval re-opened, the new av row gone, `sqlite_sequence`
  reset — byte-identical.
- **The scope is bounded:** assert the capture reads only the touched rows (not a full-table
  scan) — e.g. the capture size is O(actions), not O(table) (a structural assertion or a probe
  that the capture's row count is small).
- **Both DBs restored:** a function-kind add (which writes USER + DEV differently) → restore
  leaves both byte-identical.
- The landed oracles stay green (additive — the capture is new, on the deferred path only;
  `apply_seeds`/the immediate path untouched). Capture the data-core suite on HEAD first (the 3
  pre-existing seed↔CSV-drift reds are NOT 4d's); confirm 4d turns no green oracle red.
Run: `python -m pytest data/refdata-extractor/tests/ -q`.

**Dependencies.** Step 4c (the direct-write path the restore-point captures within; the actions
it reads) + step 4a (the deferred-commit handle the restore-point rides on). This is the
PRODUCER; step 5 (the Confirm) consumes it (`.claude/rules/incremental-delivery.md`).

**Design authority.** [`data/maintainer-tool/design.md`](../../../../data/maintainer-tool/design.md)
§10 **D21** (the two-mechanism rollback — deferred ROLLBACK pre-commit + the scoped restore-point
post-commit; the data-core placement per D13/law 6; the touched-tables list; the
reject-the-full-file-snapshot rationale) + D19 (the direct write whose actions the capture reads)
+ D1 (DB authoritative). [`../plan-spec.md`](../plan-spec.md) §"Cross-step invariants" (the D21
robust-rollback invariant). `data/seeds/policy.md` (the schema — which tables are autoincrement).

**UX.** N/A — a data-core transaction-rollback capability; no user-facing surface.

**Disassembler-test / author-burden.** N/A — internal Python restore-point; no author-facing input.
