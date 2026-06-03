"""app.csv_revert -- the backend half of the Confirm robust rollback: revert the
data/db-export/ CSVs (design D21 -- the CSV-revert SPLIT).

WHY THIS EXISTS (the CSV-revert split -- read this)
---------------------------------------------------
The robust post-commit rollback (D21) is TWO halves at the irreversible DB commit:
  - the DB ROWS + sqlite_sequence are restored by the DATA-CORE's 4d scoped restore-point
    (data_core.restore(handle)) -- it owns the write semantics (which rows each job
    touches), so the undo of those rows is its capability (D13/law 6). It restores DB rows
    ONLY -- it explicitly does NOT touch the CSVs.
  - the data/db-export/ CSVs are a backend FILE artifact (D20 -- the derived diff record
    the backend exports + stages + commits). Reverting THEM is the backend's own concern,
    exactly like csv_integrity (the backend owns the safety of the files it commits).

This module is that backend half: it keeps a pre-edit copy of the three db-export CSVs
(a few KB) before the commit and reverts them byte-for-byte on a post-commit failure --
so after data_core.restore(handle) + this revert, the DB and the CSV record are both
byte-identical to before the Confirm.

WHY A KEPT PRE-EDIT COPY (not a re-export from the restored DB)
---------------------------------------------------------------
A re-export from the restored DB would reproduce the same bytes (the export is
deterministic -- the integrity check relies on exactly that), but it depends on the export
itself succeeding during the rollback -- and a rollback path that can itself fail is no
guarantee. A kept pre-edit byte copy is unconditional: reverting is a plain file copy that
cannot mis-derive the prior bytes. A few KB regardless of DB size -- the cost the "nothing
lands" guarantee earns. (The data-core's restore-point is likewise capture-then-restore,
not recompute, for the same robustness reason.)
"""
import logging
import os
import shutil
import tempfile

log = logging.getLogger(__name__)


class CsvRevert:
    """A pre-edit byte copy of the three data/db-export/ CSVs + their revert -- the
    backend half of the D21 robust rollback (the data-core restores the DB rows; this
    reverts the derived CSV record).

    Lifecycle (one per Confirm): capture() before the DB commit, then either
    revert() on a post-commit failure or discard() on success. A file ABSENT at capture
    (a db-export CSV that does not exist yet on a first-ever save) is recorded absent, so
    revert DELETES it -- its pre-Confirm state was 'absent', and 'nothing lands' means it
    is gone again."""

    def __init__(self, csv_paths):
        """`csv_paths` -- the absolute paths of the three data/db-export/ CSVs
        (config.db_export_files). Each is captured + reverted independently by basename."""
        self._paths = list(csv_paths)
        self._snap_dir = None
        # basename -> True iff the file EXISTED at capture time. Absent -> revert deletes.
        self._present = {}

    def capture(self):
        """Copy the current bytes of every db-export CSV BEFORE the DB commit -- the
        pre-edit state the revert restores. Must run while the live CSVs still hold their
        pre-Confirm committed bytes (before the post-commit export overwrites them)."""
        self._snap_dir = tempfile.mkdtemp(prefix="confirm_csv_revert_")
        for p in self._paths:
            base = os.path.basename(p)
            if os.path.isfile(p):
                shutil.copy2(p, os.path.join(self._snap_dir, base))
                self._present[base] = True
            else:
                # Absent at capture (a not-yet-created db-export CSV on a first save). Its
                # pre-Confirm state is 'does not exist'; revert must DELETE it.
                self._present[base] = False
        return self

    def revert(self):
        """Revert each db-export CSV to its captured pre-edit bytes -- the backend half of
        the robust rollback. Re-copies each captured-present file over the post-commit
        export's bytes and DELETES each captured-absent file. Idempotent-safe: a missing
        snapshot dir (never captured) is a no-op. Logged (logging.md -- a rollback is a
        lifecycle event)."""
        if self._snap_dir is None:
            return
        for p in self._paths:
            base = os.path.basename(p)
            snap = os.path.join(self._snap_dir, base)
            if self._present.get(base):
                shutil.copy2(snap, p)
            elif os.path.isfile(p):
                # Was absent -- the Confirm's export created it; remove it so nothing lands.
                os.remove(p)
        log.info("Confirm db-export CSVs reverted to their pre-Confirm bytes (the backend "
                 "half of the robust rollback, D21 -- the data-core restored the DB rows)")

    def discard(self):
        """Drop the pre-edit copy -- a clean Confirm landed, no revert needed. Idempotent
        (a never-captured / already-discarded copy is a no-op)."""
        if self._snap_dir is not None:
            shutil.rmtree(self._snap_dir, ignore_errors=True)
            self._snap_dir = None
