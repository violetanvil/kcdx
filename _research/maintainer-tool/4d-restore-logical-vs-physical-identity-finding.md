# Finding — the 4d scoped restore-point restores LOGICAL rows + sequence, not FILE bytes

**Context.** Reworking the step-5 Confirm from the full-file DB snapshot (`shutil.copy2`
both DB files) onto the 4d data-core scoped restore-point (`data_core.restore(handle)`).
The reworked test's post-commit-failure cases (git-push-to-dead-remote; live index.lock)
FAILED on `_state_hash(root) == state_before`, where `_state_hash` SHA-ed the raw DB
**file** bytes.

**Probe (theory-independent, one variable: physical-bytes vs logical-rows).** Rebuilt the
mini-dump DBs, ran a deferred `update_version_row`, `commit(handle)` (simulating the
irreversible commit), then `restore(handle)`; compared the DB's raw **file** SHA and a
canonical **logical-rows** SHA (every table's full ordered rows + `sqlite_sequence`)
before-edit vs after-restore.

**Result — VERDICT B (ground truth):**
- `rows SHA  before == after-restore?  True`  — every table's rows AND `sqlite_sequence`
  are byte-identical after the restore (the D21 guarantee holds).
- `file SHA  before == after-restore?  False` — the DB **file** bytes differ.

**Root cause / mechanism.** `restore(handle)` (import_to_sqlite `_restore_one_db`) undoes a
committed edit by a row-level **delete-then-reinsert + `sqlite_sequence` reset** on a freshly
re-opened connection. That converges the LOGICAL state (rows + PK counter) exactly, but
SQLite rewrites the file's physical page layout / free-list — so the raw file bytes differ
even on a perfectly correct restore. The dropped full-file snapshot restored exact file
bytes; the scoped restore-point restores logical state. **D21 requires logical row +
sequence identity, NOT physical file identity** — so file-byte identity was the wrong
oracle, not a restore bug.

**Fix (in the TEST, not the code).** `_state_hash` now hashes the two DBs by **logical
rows** (`_db_rows_hash` — every table's ordered rows incl. `sqlite_sequence`) and the three
`data/db-export/` CSVs by **raw bytes** (the backend reverts those from a kept pre-edit byte
copy, so they ARE byte-identical). The DB-rows assertion still proves the PK auto-increment
reset (`sqlite_sequence` is a table, included in the row hash). All 41 backend tests green.

**Reusable wiring.** The probe (`probe_4d_restore_bytes_vs_rows.py`, removed post-finding)
rebuilt the mini-dump, deferred-edited, committed, restored, and SHA-ed file-vs-rows. To
reconstruct: `db_editor.update_version_row(..., defer_commit=True)` → `commit(handle)` →
`restore(handle)`; canonical rows-SHA = `SELECT * FROM <each table> ORDER BY <all cols>` +
`sqlite_sequence`.

**Implication for any future consumer of the 4d restore-point.** Assert LOGICAL row +
`sqlite_sequence` identity after a restore, never raw DB-file-byte identity.
