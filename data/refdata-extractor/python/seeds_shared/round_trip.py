"""seeds_shared.round_trip -- the byte-identity round-trip oracle(s).

The correctness contract that binds the exporter (csv_exporter / bulk_exporter) and
the importer (import_to_sqlite) together. TWO oracles live here, sharing one cell
canonicaliser + one comparator:

  round_trip()       -- the CURATED build-time oracle (design S4 / S10 D2). Asserts
                        BOTH directions over the curated authored surface:
                            import(export(DB)) == DB     (curated DB rows identical)
                            export(import(CSVs)) == CSVs (the seed CSVs identical)
                        Rebuilds the curated half via the EXPERT-only from-dump path
                        (run_rebuild). The GUI/build-time curated correctness gate;
                        re-exported on the data-core API surface (data_core.round_trip).

  round_trip_full()  -- the WIDENED completeness oracle (D38; seeds-to-tracked-csv-
                        migration P1.4). Asserts the FULL corpus round-trips through
                        the D38 CSV-genesis path -- rebuild-from-CSV -> DB -> re-export
                        -> byte-identical for BOTH the curated half AND the bulk half
                        (statements / referenced_vars / call_edges + the kcdx_id-NULL
                        bulk address_versions rows). This is D38's standing completeness
                        bar: the durable proof the export captures ALL the data the dump
                        carried, so the dump-retirement is safe. The import half is
                        run_rebuild_from_csv (NO dump, the 1.3 genesis); the bar is
                        EVERY real table byte-identical, not the curated subset alone.

A divergence in either oracle is a TOOL BUG -- a column the export invents or the
import drops, a diff the export reformats, a derived value leaking onto the authored
surface, a bulk column that does not survive the CSV bounce. round_trip_full() FAILS
LOUD (AP14), naming the divergent table/column.

WHY THIS LIVES IN seeds_shared/ (design S5): the round-trip is a data-core concern
-- a headless, Qt-free callable the GUI calls and the tests drive. Both oracles reuse
the REAL export (csv_exporter.export_seeds / bulk_exporter.export_bulk) and the REAL
import (import_to_sqlite.run_rebuild / run_rebuild_from_csv) -- re-implementing neither.
import_to_sqlite is imported LAZILY inside each function so this seeds_shared submodule
carries no import-time dependency on it (import_to_sqlite imports seeds_shared).

WHAT round_trip() "== DB" COMPARES (the curated authored surface only): the per-table
content hash of the USER curated tables -- schema.USER_TABLES. The DEV-only bulk
discovery tables are NOT part of the curated authored surface, so the curated oracle
does not compare them (round_trip_full() is the oracle that does).

WHAT round_trip_full() COMPARES (the FULL corpus, both halves): EVERY real table in
BOTH the USER and DEV DBs -- the curated authored tables AND the bulk DEV tables
(statements / referenced_vars / call_edges) AND the full address_versions table split
bulk (kcdx_id IS NULL) vs curated (kcdx_id IS NOT NULL). Only the _dict_* encoding-
artifact lookups + sqlite_sequence are excluded (the documented CSV-genesis gap, per
test_rebuild_from_csv.py: the bulk-only _dict_* string mapping lives only in the dump;
the REAL-table data is unaffected, the rows keep their verbatim int dict-ids). The
comparison is at the CONTENT-HASH level (every cell canonicalised, ORDER-INDEPENDENT
so an autoincrement renumber across two independent builds is not a false mismatch) --
a byte/value-identity check, not a loose field compare.

WHAT "== CSVs" COMPARES (round_trip only): the raw bytes of each of the three seed
CSVs before vs. after the DB bounce -- the strongest diff-preservation assertion (row
order, #-comments, QUOTE_MINIMAL quoting, line terminator, trailing newline, in one
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
from .bulk_exporter import export_bulk

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


# ---------------------------------------------------------------------------
# Full-corpus content hashing -- the WIDENED comparator (round_trip_full, D38).
# Drops the curated-only exclusion hash_curated_tables applies: it hashes EVERY
# real table (the curated authored tables AND the bulk DEV tables statements /
# referenced_vars / call_edges), excluding ONLY the _dict_* encoding-artifact
# lookups + sqlite_sequence (the documented CSV-genesis gap; the real-table data
# is unaffected). Order-independent (rows sorted by canonical tuple) so an
# autoincrement renumber across two independent builds is not a false mismatch --
# the round-trip preserves CONTENT, not the internal rowid. Mirrors the
# _hash_tables / _real_table_names comparator test_rebuild_from_csv.py (P1.3) uses,
# lifted here so the standing oracle owns it.
# ---------------------------------------------------------------------------
def _real_table_names(con):
    """The real tables in `con`: every table except the _dict_* encoding-artifact
    lookups + sqlite_sequence (which a CSV-genesis rebuild cannot reconstruct from
    the CSVs -- the documented gap; the real-table data keeps its verbatim int
    dict-ids regardless)."""
    return [r[0] for r in con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE '#_dict#_%' ESCAPE '#' "
        "AND name <> 'sqlite_sequence' ORDER BY name")]


# The USER-projection tables whose `id` PK is a NON-SEMANTIC autoincrement that
# legitimately RENUMBERS between the from-dump and CSV-genesis builds (the dump
# USER build inserts statements/referenced_vars with no explicit id -> 1..N; the
# CSV build reads them VERBATIM with their DEV-stored ids). The engine joins these
# via address_version_id, NEVER the statement PK -- so the byte-identity compare
# excludes the id in the USER projection (CONTENT must match, not the renumbering
# artifact). The DEV statements/referenced_vars round-trip WITH the id, verbatim.
# (Same exclusion + rationale as test_rebuild_from_csv.py's _USER_AUTOINC_ID_TABLES.)
_USER_AUTOINC_ID_TABLES = frozenset({"statements", "referenced_vars"})


def hash_real_tables(db_path, *, user_projection=False):
    """Return {table: {"count": N, "hash": sha256hex}} for EVERY real table in
    `db_path` (the widened oracle's comparator -- the bulk DEV tables INCLUDED,
    only the _dict_* lookups + sqlite_sequence excluded). Order-independent. When
    `user_projection` is set, the statements/referenced_vars autoincrement `id`
    PK is excluded from the hash (it legitimately renumbers between the two genesis
    paths; the engine joins via address_version_id)."""
    con = sqlite3.connect(db_path)
    try:
        out = {}
        for t in _real_table_names(con):
            cols = [c[1] for c in con.execute(f'PRAGMA table_info("{t}")')]
            if user_projection and t in _USER_AUTOINC_ID_TABLES:
                cols = [c for c in cols if c != "id"]
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


# ---------------------------------------------------------------------------
# round_trip_full -- the WIDENED completeness oracle (D38; P1.4).
# ---------------------------------------------------------------------------
def _av_hash(db_path, where):
    """Content hash + count of the address_versions rows matching `where`, ALL
    columns (no exclusion -- the curated subset must carry the merged fingerprint +
    the promoted id). Order-independent. Used to split the av byte-identity check
    bulk (kcdx_id IS NULL) vs curated (kcdx_id IS NOT NULL) so a failure localises."""
    con = sqlite3.connect(db_path)
    try:
        cols = [c[1] for c in con.execute('PRAGMA table_info("address_versions")')]
        rendered = []
        for row in con.execute(
                f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} '
                f"FROM address_versions WHERE {where}"):
            rendered.append("\x1e".join(_canon(c) for c in row))
        h = hashlib.sha256()
        for line in sorted(rendered):
            h.update((line + "\x1d").encode("utf-8", "surrogatepass"))
        return len(rendered), h.hexdigest()
    finally:
        con.close()


def round_trip_full(dump_dir, curated_export_dir, *, work_dir=None):
    """Assert the D38 FULL-corpus round-trip: rebuild-from-CSV -> DB -> re-export ->
    byte-identical for BOTH the curated half AND the bulk half (the P1.4 widened
    oracle -- D38's standing completeness bar). Raises RoundTripError on ANY
    divergence (AP14: the message names the divergent table/column); returns a dict
    report on success.

    The mechanism (every half reuses production code -- nothing re-implemented):

      1. Build a REFERENCE DB pair from `dump_dir` + the curated CSVs at
         `curated_export_dir` via the EXPERT-only from-dump path (run_rebuild). This
         is the ground-truth the CSV-genesis must reproduce -- it carries the FULL
         corpus (curated + bulk discovery rows, statements/referenced_vars/call_edges).
      2. Export BOTH halves FROM that reference: export_seeds (curated, from the DEV
         DB so the curated WHERE applies) + export_bulk (the bulk DEV tables + the
         kcdx_id-NULL av rows + the curated-derived overlay).
      3. Rebuild BOTH DBs from the CSV export ALONE via run_rebuild_from_csv -- NO
         dump (the D38 routine genesis, the 1.3 path).
      4. Assert EVERY real table is byte-identical between the reference (from-dump)
         and the CSV-genesis rebuild, for BOTH USER and DEV -- the bulk DEV tables
         INCLUDED (the dropped curated-only exclusion). Plus the full address_versions
         table split bulk vs curated so an av failure localises.

    The D38 completeness bar made a standing gate: a divergence in ANY bulk
    table/column (e.g. the exporter dropping a statements field the rebuild cannot
    reconstruct) FAILS here -- this oracle asserts FULL-corpus byte-identity, not the
    curated subset alone.

    Parameters:
      dump_dir           -- the Ghidra dump the REFERENCE build reads (the test passes
                            the committed mini-dump excerpt -- fast; the full ~1.3 GB
                            DEV dump is the expert-regenerate input, not the routine
                            gate -- see the SCALE note in tests/test_round_trip.py).
      curated_export_dir -- the curated CSV export dir (D38's data/db-export/; the
                            three seed CSVs the from-dump build reads as its curated
                            seeds AND the CSV-genesis reads as its curated half). NOT
                            mutated.
      work_dir           -- optional scratch dir for the build/export/rebuild
                            artifacts; a temp dir is created + removed when omitted.

    HEADLESS + Qt-free (design S5). Owns NO export/import logic -- every half is a
    production function (run_rebuild / export_seeds / export_bulk / run_rebuild_from_csv).
    """
    import import_to_sqlite as imp

    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="round_trip_full_")
    try:
        # 1. Reference build: dump + curated CSVs -> both DBs (the ground truth).
        #    Point the importer's seed-path constants at curated_export_dir (the
        #    standard oracle convention) so run_rebuild reads the curated half there.
        ref_out = os.path.join(work_dir, "ref_db")
        os.makedirs(ref_out, exist_ok=True)
        saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
                 imp.ADDRESS_VERSIONS_SEED_CSV)
        imp.MODULE_SEED_CSV = os.path.join(curated_export_dir, MODULE_SEED_NAME)
        imp.ADDRESS_NAMES_SEED_CSV = os.path.join(curated_export_dir,
                                                  ADDRESS_NAMES_SEED_NAME)
        imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(curated_export_dir,
                                                     ADDRESS_VERSIONS_SEED_NAME)
        try:
            imp.run_rebuild(dump_dir, ref_out)
        finally:
            (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV) = saved
        ref_user = os.path.join(ref_out, "reference.sqlite")
        ref_dev = os.path.join(ref_out, "reference-dev.sqlite")

        # 2. Export BOTH halves from the reference. Curated from the DEV DB (so the
        #    curated WHERE filter applies); seed the curated dir with the committed
        #    CSV format first (diff-preservation honours the existing header/comments).
        curated_csv = os.path.join(work_dir, "export_curated")
        os.makedirs(curated_csv, exist_ok=True)
        for f in SEED_FILES:
            shutil.copy2(os.path.join(curated_export_dir, f),
                         os.path.join(curated_csv, f))
        export_seeds(ref_dev, curated_csv)
        bulk_csv = os.path.join(work_dir, "export_bulk")
        export_bulk(ref_dev, bulk_csv)

        # 3. Rebuild BOTH DBs from the CSV export ALONE -- NO dump (the D38 genesis).
        csv_out = os.path.join(work_dir, "csv_db")
        imp.run_rebuild_from_csv(csv_out, curated_csv, bulk_csv)
        csv_user = os.path.join(csv_out, "reference.sqlite")
        csv_dev = os.path.join(csv_out, "reference-dev.sqlite")

        # 4. Assert EVERY real table byte-identical, BOTH DBs -- bulk INCLUDED.
        diffs = []
        for label, ref_db, gen_db in (("user", ref_user, csv_user),
                                      ("dev", ref_dev, csv_dev)):
            is_user = (label == "user")
            rh = hash_real_tables(ref_db, user_projection=is_user)
            gh = hash_real_tables(gen_db, user_projection=is_user)
            rt, gt = set(rh), set(gh)
            if rt != gt:
                diffs.append(
                    f"[{label}] real-table set differs: only-reference="
                    f"{sorted(rt - gt)} only-csv-genesis={sorted(gt - rt)}")
            for t in sorted(rt & gt):
                if rh[t]["count"] != gh[t]["count"]:
                    diffs.append(
                        f"[{label}.{t}] row count {gh[t]['count']} != reference "
                        f"{rh[t]['count']}")
                elif rh[t]["hash"] != gh[t]["hash"]:
                    diffs.append(
                        f"[{label}.{t}] content hash differs (reference "
                        f"{rh[t]['hash'][:12]}.., csv-genesis {gh[t]['hash'][:12]}..)")

        # The full address_versions split bulk vs curated (DEV DB) -- so an av
        # divergence localises to the half that broke. The curated subset compares
        # ALL columns (the merged fingerprint + the promoted id must reproduce the
        # dump's PROMOTE byte-identical -- the load-bearing seam the bulk export's
        # derived-overlay closes).
        for half, where in (("bulk-av (kcdx_id IS NULL)", "kcdx_id IS NULL"),
                            ("curated-av (kcdx_id IS NOT NULL)", "kcdx_id IS NOT NULL")):
            rc, rhsh = _av_hash(ref_dev, where)
            gc, ghsh = _av_hash(csv_dev, where)
            if rc != gc:
                diffs.append(
                    f"[dev.address_versions {half}] row count {gc} != reference {rc}")
            elif rhsh != ghsh:
                diffs.append(
                    f"[dev.address_versions {half}] content hash differs "
                    f"(reference {rhsh[:12]}.., csv-genesis {ghsh[:12]}..)")

        if diffs:
            raise RoundTripError(
                "round_trip_full FAILED -- the CSV export is NOT lossless over the "
                "full corpus (a real-table column did not round-trip through the D38 "
                "CSV-genesis):\n  " + "\n  ".join(diffs))

        # The table set actually compared (for the test's assertion + the report).
        compared = sorted(set(hash_real_tables(ref_dev)) | set(hash_real_tables(ref_user)))
        return {
            "full_identical": True,
            "real_tables_compared": compared,
            "bulk_av_identical": True,
            "curated_av_identical": True,
        }
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)
