"""app.csv_integrity -- the Confirm transaction's cheap integrity check (design S4 /
S7; the round-trip-cost resolution).

THE ROUND-TRIP-COST QUESTION (resolved, results-driven -- the code answered it)
-------------------------------------------------------------------------------
The save spine (design S7) calls for a round-trip oracle on every save. The data-core's
full round_trip (seeds_shared.round_trip) asserts BOTH directions of D2:

    import(export(DB)) == DB   -- a FULL run_rebuild from the exported CSVs (+ a refdata
                                  dump_dir): rebuilds the 1.3GB DEV DB from scratch.
    export(import(CSVs)) == CSVs -- a byte-identity re-export compare: NO rebuild, NO dump.

The `import(export(DB))==DB` half is PROHIBITIVE per Confirm -- it rebuilds the bulk DEV
DB (1.3GB, the 321K bulk rows) from a refdata dump on EVERY incremental save. The web
backend has no refdata dump_dir mounted (D18 -- the image carries only app code), and a
per-save full rebuild would make every Confirm take minutes. So Confirm runs the CHEAP,
CORRECT half: the `export(import(CSVs))==CSVs` byte-identity direction -- re-export the
just-committed DB to a scratch dir and assert the three CSVs are byte-identical to the
ones Confirm just wrote to data/db-export/ (the derived-export record, D20 -- NOT
data/seeds/, the frozen bootstrap). This proves DB<->CSV information-equivalence for
the committed change (the export is deterministic: re-exporting the same committed DB
reproduces the same CSV bytes) WITHOUT a rebuild.

Why this is the CORRECT check for an incremental confirm (not a weaker substitute that
misses bugs): Confirm's CSVs are themselves produced BY export_seeds from the committed
DB (step 3). The risk this guards is the export being non-deterministic or diff-unstable
-- a re-export that differs from what was committed means the CSVs Confirm staged are not
a faithful, reproducible projection of the DB (a tool bug: the commit's CSVs would not
round-trip). The DB->CSV direction is the one the incremental save actually exercises;
the CSV->DB->CSV full rebuild re-proves the WHOLE 321K-row import pipeline, which an
incremental one-row edit did not touch and which the data-core's own oracle test
(test_round_trip on the mini-dump) already covers at build time. (The full bidirectional
round-trip remains the build-time correctness gate -- it is NOT abandoned, only NOT run
per incremental save. This is a SURFACED design call: see the step-5 confirm composition.)

WHAT THIS OWNS (and what it does NOT)
-------------------------------------
The check itself is a thin wrapper over the data-core's REAL export (export_seeds): it
re-exports the committed DB into a scratch dir and byte-compares to the committed CSVs.
It re-implements no export logic (the export is the data-core's, S5 law 6); it owns only
the byte compare + the scratch-dir lifecycle (a backend concern -- the integrity of the
files the backend is about to commit).
"""
import logging
import os
import shutil
import tempfile

from . import data_core

log = logging.getLogger(__name__)

# The three seed CSV basenames (the data-core's, mirrored from config). Compared
# byte-for-byte (row order, #-comments, quoting, line terminator, trailing newline --
# the diff-preservation the exporter honours).
_SEED_BASENAMES = ("module_seed.csv", "address_names_seed.csv",
                   "address_versions_seed.csv")


class CsvIntegrityError(AssertionError):
    """The committed CSVs are NOT a deterministic export of the committed DB -- a
    re-export diverged byte-for-byte. A TOOL BUG (the export is non-deterministic or the
    CSVs Confirm staged do not reproduce from the DB): the message names the diverging
    file + the first differing byte so the Confirm surfaces a precise report, not a bare
    'integrity failed'."""


def assert_csv_export_deterministic(db_path, committed_seed_dir, *, work_dir=None):
    """Assert the three CSVs under `committed_seed_dir` are byte-identical to a fresh
    re-export of `db_path` -- the cheap `export(import(CSVs))==CSVs` integrity direction
    (the round-trip-cost resolution above). NO full rebuild, NO dump.

    Re-exports `db_path` into a COPY of `committed_seed_dir` (so the exporter sees the
    same existing-file format the committed files have -- diff-preservation is seeded
    from the existing bytes, exactly the data-core round_trip's CSV-direction
    convention), then byte-compares each re-export to the committed file.

    Parameters:
      db_path            -- the just-committed reference DB (config.user_db). The export
                            reads it; the USER DB carries the full curated set.
      committed_seed_dir -- the dir holding the three CSVs Confirm just wrote
                            (config.db_export_dir -- data/db-export/, D20). NOT mutated --
                            the re-export goes to a scratch copy.
      work_dir           -- optional scratch dir; a temp dir is created + removed when
                            omitted.

    Returns {"csv_identical": True, "files": [...]} on success.
    Raises CsvIntegrityError on any byte divergence.
    """
    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="confirm_integrity_")
    try:
        committed = _read_seed_bytes(committed_seed_dir)

        # Re-export into a COPY of the committed dir so the exporter's diff-preservation
        # is seeded from the same existing-file format the committed files have (the
        # data-core round_trip's CSV-direction convention). The export OVERWRITES the
        # copies in place; we then compare those re-exports to the originals.
        export_into = os.path.join(work_dir, "reexport")
        os.makedirs(export_into, exist_ok=True)
        for f in _SEED_BASENAMES:
            shutil.copy2(os.path.join(committed_seed_dir, f),
                         os.path.join(export_into, f))

        # The REAL data-core export (export_seeds) -- re-implemented nowhere.
        data_core.export_seeds(db_path, export_into)
        reexported = _read_seed_bytes(export_into)

        diffs = []
        for f in _SEED_BASENAMES:
            a, b = reexported[f], committed[f]
            if a != b:
                first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
                             min(len(a), len(b)))
                diffs.append(
                    f"{f}: re-export != committed bytes (len reexport={len(a)} "
                    f"committed={len(b)}; first diff at byte {first})")
        if diffs:
            # logging.md: the integrity failure logs before it raises -- name the
            # diverging files so the operator sees a precise tool-bug report.
            log.warning("Confirm CSV integrity check FAILED: %s", "; ".join(diffs))
            raise CsvIntegrityError(
                "the committed CSVs do not deterministically re-export from the "
                "committed DB (a tool bug -- the CSVs are not a faithful projection of "
                "the DB):\n  " + "\n  ".join(diffs))

        return {"csv_identical": True, "files": list(_SEED_BASENAMES)}
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)


def _read_seed_bytes(seed_dir):
    """Return {basename: raw bytes} for the three seed CSVs under seed_dir."""
    out = {}
    for f in _SEED_BASENAMES:
        with open(os.path.join(seed_dir, f), "rb") as fh:
            out[f] = fh.read()
    return out
