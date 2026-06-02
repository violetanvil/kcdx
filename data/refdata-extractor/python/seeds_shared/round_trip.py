"""seeds_shared.round_trip -- the bidirectional byte-identity round-trip oracle.

The correctness contract that binds the exporter (csv_exporter) and the importer
(import_to_sqlite) together, asserting BOTH directions of the round-trip
(data/maintainer-tool/design.md S4 / S10 D2):

    import(export(DB)) == DB       (curated DB rows byte-identical after a CSV bounce)
    export(import(CSVs)) == CSVs   (the seed CSVs byte-identical after a DB bounce)

A divergence is a TOOL BUG -- a column the export invents or the import drops, a
diff the export reformats, a derived value leaking onto the authored surface. The
GUI save chain (design S5 / US-4) calls this after every write to confirm the DB
and the CSVs are still information-equivalent before the change commits; the
round-trip oracle test (tests/test_round_trip.py) drives it on the mini-dump
fixture as the same-change correctness gate.

WHY THIS LIVES IN seeds_shared/ (design S5): the round-trip is a data-core concern
-- a headless, Qt-free callable the GUI calls and the test drives. Its two halves
are the REAL export (csv_exporter.export_seeds) and the REAL import
(import_to_sqlite.run_rebuild) -- this module re-uses both, re-implementing
neither.

THE TWO HALVES (both reuse production code -- no re-implementation):
  - export half: seeds_shared.csv_exporter.export_seeds(db_path, seed_dir).
  - import half: import_to_sqlite.run_rebuild(dump_dir, out_dir), with its seed-
    path module constants pointed at the seed dir under test (the existing oracle
    convention in test_apply_reverify.py / test_csv_exporter.py). run_rebuild is
    imported lazily inside the function so this seeds_shared submodule carries no
    import-time dependency on import_to_sqlite (which imports seeds_shared).

WHAT "== DB" COMPARES (the curated authored surface, NOT the bulk dev tables):
  the per-table content hash of the USER curated tables only --
  schema.USER_TABLES (modules, game_versions, address_names, address_versions,
  meta, survival). The DEV-only bulk discovery tables (call_edges / statements /
  referenced_vars / the _dict_* discovery lookups in the dev superset) are NOT
  part of the round-trip authored surface (design S4: a derived/cache column
  belongs to the bulk-dump dev-only tables, which the export does not touch), so
  comparing them would compare importer-discovery noise, not the authored round
  trip. The comparison is at the CONTENT-HASH level (every cell canonicalized,
  ordered rows) -- a byte/value-identity check, not a loose field compare.

WHAT "== CSVs" COMPARES: the raw bytes of each of the three seed CSVs before vs.
after the DB bounce -- the strongest diff-preservation assertion (row order,
#-comments, QUOTE_MINIMAL quoting, line terminator, trailing newline, in one
comparison).
"""
import hashlib
import os
import shutil
import sqlite3
import tempfile

from .schema import USER_TABLES
from .csv_exporter import (
    export_seeds,
    MODULE_SEED_NAME,
    ADDRESS_NAMES_SEED_NAME,
    ADDRESS_VERSIONS_SEED_NAME,
)

SEED_FILES = (MODULE_SEED_NAME, ADDRESS_NAMES_SEED_NAME, ADDRESS_VERSIONS_SEED_NAME)


# ---------------------------------------------------------------------------
# Curated-table content hashing -- the DB-identity comparator. Mirrors the
# per-table canonicalized content hash test_rebuild_oracle.py uses (BLOB -> hex,
# None -> a NULL sentinel, type-tagged scalars so 1 (int) and '1' (text) differ),
# scoped to the curated authored tables (USER_TABLES) so the comparison is the
# round-trip surface, never the bulk dev-only discovery tables.
# ---------------------------------------------------------------------------
def _canon(v):
    """Stable string form of one cell, independent of the sqlite driver's Python
    type choices. BLOB -> 'b:<hex>'; None -> a NULL sentinel; scalars type-tagged
    so an int and the same digits as text never collide."""
    if v is None:
        return "\x00NULL\x00"
    if isinstance(v, (bytes, bytearray, memoryview)):
        return "b:" + bytes(v).hex()
    if isinstance(v, int):
        return "i:" + str(v)
    if isinstance(v, float):
        return "f:" + repr(v)
    return "t:" + str(v)


def hash_curated_tables(db_path):
    """Return {table: {"count": N, "hash": sha256hex}} for the curated authored
    tables (schema.USER_TABLES) in `db_path`. Rows are read sorted by their
    canonicalized tuple (so an autoincrement-id renumber across two independent
    rebuilds is not a false mismatch -- the round-trip preserves the curated row
    CONTENT, not the internal rowid); columns in declared order. The bulk dev-only
    discovery tables are deliberately NOT hashed (they are not the authored
    round-trip surface)."""
    con = sqlite3.connect(db_path)
    try:
        out = {}
        present = {r[0] for r in con.execute(
            "SELECT name FROM sqlite_master WHERE type='table'")}
        for t in USER_TABLES:
            if t not in present:
                out[t] = {"count": 0, "hash": "<absent>"}
                continue
            cols = [c[1] for c in con.execute(f'PRAGMA table_info("{t}")')]
            rendered = []
            for row in con.execute(
                    f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} FROM "{t}"'):
                rendered.append("\x1e".join(_canon(c) for c in row))
            h = hashlib.sha256()
            for line in sorted(rendered):
                h.update((line + "\x1d").encode("utf-8", "surrogatepass"))
            out[t] = {"count": len(rendered), "hash": h.hexdigest()}
        return out
    finally:
        con.close()


def _read_seed_bytes(seed_dir):
    """Return {filename: raw bytes} for the three seed CSVs under seed_dir."""
    out = {}
    for f in SEED_FILES:
        with open(os.path.join(seed_dir, f), "rb") as fh:
            out[f] = fh.read()
    return out


def _rebuild_from_seeds(seed_dir, dump_dir, out_dir):
    """Run the REAL importer rebuild from the seeds under seed_dir + dump_dir into
    out_dir, returning the built USER DB path (reference.sqlite). Reuses
    import_to_sqlite.run_rebuild via the seed-path-constant pointing convention
    (the same mechanism the oracles use); imported lazily so this submodule does
    not depend on import_to_sqlite at import time."""
    import import_to_sqlite as imp

    os.makedirs(out_dir, exist_ok=True)
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, MODULE_SEED_NAME)
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, ADDRESS_NAMES_SEED_NAME)
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir, ADDRESS_VERSIONS_SEED_NAME)
    try:
        imp.run_rebuild(dump_dir, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved
    return os.path.join(out_dir, "reference.sqlite")


class RoundTripError(AssertionError):
    """Raised by round_trip() when either direction diverges. The message names
    the failing direction + the specific divergence (the table/file + the
    mismatch), so a caller (the GUI save chain) surfaces a precise tool-bug
    report, not a bare 'round-trip failed'."""


def round_trip(db_path, seed_dir, dump_dir, *, work_dir=None):
    """Assert the bidirectional byte-identity round-trip for one DB + its seed
    export, reusing the REAL export + REAL import. Raises RoundTripError on any
    divergence; returns a dict report ({"db_identical", "csv_identical",
    "db_tables", "csv_files"}) on success.

    Directions asserted (design S4 / D2):

      import(export(DB)) == DB     -- export `db_path` to fresh CSVs, re-import
                                      them (a full rebuild from those CSVs +
                                      `dump_dir`), and assert the rebuilt USER DB's
                                      curated tables (schema.USER_TABLES) are
                                      content-hash-identical to `db_path`'s.
      export(import(CSVs)) == CSVs -- read the committed seed CSVs under `seed_dir`,
                                      re-export them from `db_path`, and assert the
                                      three files are byte-identical.

    Parameters:
      db_path  -- the reference DB the export reads (the just-written DB on the
                  GUI save path; the rebuilt baseline USER DB in the test).
      seed_dir -- the directory holding the three committed seed CSVs (the export
                  target + the CSV-direction comparison source). NOT mutated.
      dump_dir -- the refdata dump the importer rebuild needs (the bulk discovery
                  superset; the curated rows come from the CSVs).
      work_dir -- an optional scratch dir for the export/rebuild artifacts; a
                  temp dir is created + removed when omitted.

    The callable is HEADLESS + Qt-free (design S5) so the GUI save chain and the
    test both drive it. It owns NO export/import logic -- both halves are the
    production functions (export_seeds, run_rebuild).
    """
    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="round_trip_")
    try:
        # Contract clause: export(import(CSVs)) == CSVs (checked first; cheap byte compare).
        # Re-export db_path into a fresh dir and compare byte-for-byte to the
        # committed seeds under seed_dir. (seed_dir is the diff-preservation
        # source the exporter honours -- so the re-export is seeded from the
        # committed files' format. We export into a COPY of seed_dir so the
        # exporter sees the same existing-file format the GUI save path sees,
        # then compare the result to the originals.)
        committed = _read_seed_bytes(seed_dir)
        export_into = os.path.join(work_dir, "export_csvs")
        os.makedirs(export_into, exist_ok=True)
        for f in SEED_FILES:
            shutil.copy2(os.path.join(seed_dir, f), os.path.join(export_into, f))
        export_seeds(db_path, export_into)
        reexported = _read_seed_bytes(export_into)

        csv_diffs = []
        for f in SEED_FILES:
            if reexported[f] != committed[f]:
                a, b = reexported[f], committed[f]
                first = next((i for i in range(min(len(a), len(b))) if a[i] != b[i]),
                             min(len(a), len(b)))
                csv_diffs.append(
                    f"{f}: re-exported bytes != committed seed bytes "
                    f"(len exported={len(a)} committed={len(b)}; first diff at "
                    f"byte {first})")
        if csv_diffs:
            raise RoundTripError(
                "export(import(CSVs)) == CSVs FAILED:\n  " + "\n  ".join(csv_diffs))

        # Contract clause: import(export(DB)) == DB (curated USER_TABLES content-hash).
        # Export db_path to fresh CSVs, rebuild a DB from THOSE CSVs (+ dump_dir),
        # and compare the rebuilt USER DB's curated tables to db_path's.
        db_export = os.path.join(work_dir, "db_export_csvs")
        os.makedirs(db_export, exist_ok=True)
        export_seeds(db_path, db_export)
        rebuilt_out = os.path.join(work_dir, "reimport_db")
        rebuilt_user_db = _rebuild_from_seeds(db_export, dump_dir, rebuilt_out)

        orig_hashes = hash_curated_tables(db_path)
        new_hashes = hash_curated_tables(rebuilt_user_db)

        db_diffs = []
        tables = set(orig_hashes) | set(new_hashes)
        for t in sorted(tables):
            o = orig_hashes.get(t)
            n = new_hashes.get(t)
            if o is None or n is None:
                db_diffs.append(f"{t}: present in only one DB")
                continue
            if o["count"] != n["count"]:
                db_diffs.append(
                    f"{t}: row count {n['count']} != original {o['count']}")
            elif o["hash"] != n["hash"]:
                db_diffs.append(
                    f"{t}: curated content hash differs "
                    f"(original {o['hash'][:12]}.., rebuilt {n['hash'][:12]}..)")
        if db_diffs:
            raise RoundTripError(
                "import(export(DB)) == DB FAILED (curated tables):\n  "
                + "\n  ".join(db_diffs))

        return {
            "db_identical": True,
            "csv_identical": True,
            "db_tables": sorted(orig_hashes),
            "csv_files": list(SEED_FILES),
        }
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)
