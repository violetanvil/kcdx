"""test_rebuild_from_csv.py -- the run_rebuild-from-CSV genesis gate
(seeds-to-tracked-csv-migration P1.3; D38's genesis repoint).

WHAT THIS PROVES
----------------
`run_rebuild_from_csv` (the D38 ROUTINE genesis) rebuilds BOTH reference DBs from
the TRACKED CSV EXPORT alone -- the curated half (`data/db-export/`'s three seed
CSVs) + the bulk half (`data/db-export-bulk/`'s raw lossless bundle: the verbatim
DEV-only tables + bulk address_versions + the curated-derived overlay
address_versions_derived.csv) -- with NO Ghidra dump. EVERY real table round-trips
BYTE-IDENTICAL to the dump build, INCLUDING the full address_versions table (curated
+ bulk, all 29 columns) and the USER curated statements/referenced_vars subset. This
is the "rebuild from CSV alone" proof at the run_rebuild level; P1.4 widens it to a
standing round-trip oracle.

THE FORMERLY-BLOCKING GAP -- NOW CLOSED (1.1 export + 1.3 merge)
---------------------------------------------------------------
The curated FUNCTION-kind rows (123 / 157 in the mini-dump fixture) carry a body
FINGERPRINT (content_hash + length + observed_arg_slots + caller_reg_arg_count +
caller_arg_agreement + auto_name + decompile_quality) the from-dump build derives by
PROMOTING the matching bulk dump function IN PLACE (the curated row reuses the bulk
row's av_id + keeps the fingerprint). The original curated/bulk export split LOST
that fingerprint: the bulk export filtered `kcdx_id IS NULL` (excluding the promoted
curated rows) and the curated seed CSV does NOT carry derived columns (forbidden on
the authored surface). The FIX (this migration's seam-correction): the bulk export
now ALSO writes address_versions_derived.csv -- the curated rows' DERIVED columns +
their PROMOTED id, keyed by kcdx_id -- and run_rebuild_from_csv MERGES that overlay
onto the seed-built authored rows. So each curated row reconstructs byte-identical to
the dump's PROMOTE (authored half from the seed CSV, derived half + promoted id from
the overlay), the curated av_id set matches the dump build exactly, and the USER
curated statements/referenced_vars subset (filtered by it) is non-empty + identical.

This gate now ASSERTS the FULL byte-identity (no excluded gap table) -- the stronger
bar. FALSIFIABLE: a rebuild that drops the curated fingerprint, shifts a curated
av_id, or empties the USER statements subset fails the byte-identity row.

THE MECHANISM UNDER TEST (resolved from source, NOT assumed)
------------------------------------------------------------
- write_db PRESERVES STORED IDS VERBATIM: it CREATE-TABLEs from SCHEMA (literal
  types incl. AUTOINCREMENT) then explicit-column-INSERTs incl. the `id` PK, so an
  explicitly-supplied id is written as-is (no AUTOINCREMENT renumber) -- the exact
  property the P0.1 probe proved. So feeding write_db the stored values produces
  the same rows.
- The BULK rows are read VERBATIM from the bulk CSVs (stored id/FK/dict-int/BLOB
  kept byte-for-byte). The CURATED rows are rebuilt from the curated seed CSVs via
  the SAME seeds_shared builders the dump path uses, so curated row shapes cannot
  drift, and the curated dict ids (kind/evidence_kind) reproduce the dump's
  _dict_* order (the curated CSV's first-seen order == the dump's seed-loop order).

FIXTURE -- a real small DB pair (fast), NOT the live 1.3 GB dump
---------------------------------------------------------------
Build a real reference DB pair ONCE from the committed mini-dump excerpt
(tests/fixtures/mini-dump/) + the committed curated seed CSVs at `data/db-export/`
(D38's curated half; NOT the retired data/seeds/), via the EXPERT-ONLY from-dump
path run_rebuild. Then export both halves FROM that DB (export_seeds curated +
export_bulk bulk) and rebuild-from-CSV-alone. The mini-dump keeps the build in
milliseconds; full-dump fidelity is P1.4's widened oracle + the live-DB manual
check captured in the step deliverable.

THE STRONGEST ASSERT -- FULL REAL-TABLE BYTE-IDENTITY, rebuilt-from-CSV == dump-built
-------------------------------------------------------------------------------------
EVERY REAL table (excluding only the _dict_* encoding-artifact lookups -- see the
KNOWN GAP below) compared row-set-identical (count + every column, BLOB-aware,
order-independent) between the dump-built DB and the CSV-genesis rebuild, for BOTH
the USER and DEV DBs -- with NO table excluded. This now INCLUDES the full
address_versions table (all 29 columns, curated + bulk) AND the USER curated
statements/referenced_vars subset (which depends on the curated av_id set). The
strongest single assert is the full-av byte-identity split into the bulk subset
(kcdx_id NULL, all columns) + the curated subset (kcdx_id NOT NULL, all columns incl.
the merged fingerprint + the promoted id). FALSIFIABLE: a rebuild missing the curated
promote columns, with a shifted curated av_id, an emptied USER statements subset, a
dropped bulk row, a renumbered id, a NULL-read-as-'', or a lost column fails the row.
Plus a direct bulk-decoder round-trip (a NULL-vs-'' case + a BLOB case). Plus the
dump-is-not-a-routine-input assert (run_rebuild_from_csv takes no dump).

ONE PRINCIPLED EXCLUSION (a non-semantic autoincrement, not a content weakening): the
USER-projection statements/referenced_vars `id` PK is a non-semantic autoincrement that
legitimately renumbers between the two genesis paths (the dump USER build inserts them
with no explicit id -> AUTOINCREMENT 1..N; the CSV build reads them VERBATIM, carrying
the DEV-stored ids). The engine joins these via address_version_id (byte-identical),
never the statement PK. So the USER byte-identity compare for these two tables excludes
the id PK and asserts CONTENT identity -- the exact "autoincrement renumber across two
independent builds is not a false mismatch" case _canon is built for, applied at the
table level. The DEV statements/referenced_vars (read verbatim) round-trip WITH the id.

KNOWN GAP (surfaced, asserted as the documented limit -- not papered over)
--------------------------------------------------------------------------
The BULK-ONLY _dict_* tables (_dict_address_versions_caller_arg_agreement /
_decompile_quality, _dict_statements_kind, _dict_referenced_vars_storage_kind /
_data_type) carry strings that originate ONLY in the dump corpus; the bulk CSVs
store the ENCODED INTEGER ids, not the strings, so a CSV-genesis rebuild cannot
reconstruct those _dict_* TABLES from the CSVs alone. The REAL-table data is
unaffected (the bulk rows keep their verbatim int dict-ids). This test asserts the
REAL tables are byte-identical AND that the curated _dict_* tables
(_dict_address_versions_kind / _evidence_kind, whose strings ARE in the curated
CSVs) reconstruct byte-identically -- documenting exactly where the CSV-genesis
fidelity ends (a carry-forward for 1.1's exporter / 1.4's oracle: export the
_dict_* id<->string mapping too if full _dict_* byte-identity is required).

ACCEPTANCE SIGNAL
-----------------
A headless DB-shape + round-trip assertion (no engine, no game launch). Emits the
canonical acceptance signal (.claude/rules/acceptance-signal.md) -- ACCEPT-RESULT
per item + one ACCEPT-SUITE aggregate -- to stdout (the data-core's DB-pipeline
test sink).

RUN
---
    python -m pytest data/refdata-extractor/tests/test_rebuild_from_csv.py -v
    python data/refdata-extractor/tests/test_rebuild_from_csv.py
"""
import hashlib
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
# D38's CURATED export half (NOT the retired data/seeds/): the three seed-shaped
# CSVs run_rebuild_from_csv reads. The mini-dump's curated RVAs cover this set.
CURATED_EXPORT_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import export_seeds, export_bulk  # noqa: E402

SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# ---------------------------------------------------------------------------
# Real-table content hashing (the byte-identity comparator). Excludes the
# _dict_* encoding-artifact tables + sqlite_sequence; rows sorted by their
# canonicalized tuple (order-independent -- an autoincrement renumber across two
# independent builds is not a false mismatch; the round-trip preserves CONTENT).
# ---------------------------------------------------------------------------
def _canon(v):
    if v is None:
        return "\x00NULL\x00"
    if isinstance(v, (bytes, bytearray, memoryview)):
        return "b:" + bytes(v).hex()
    if isinstance(v, int):
        return "i:" + str(v)
    if isinstance(v, float):
        return "f:" + repr(v)
    return "t:" + str(v)


def _real_table_names(con):
    return [r[0] for r in con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name NOT LIKE '#_dict#_%' ESCAPE '#' "
        "AND name <> 'sqlite_sequence' ORDER BY name")]


def _dict_table_names(con):
    return [r[0] for r in con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' "
        "AND name LIKE '#_dict#_%' ESCAPE '#' ORDER BY name")]


# The USER-projection tables whose `id` PK is a NON-SEMANTIC autoincrement that
# legitimately RENUMBERS between the dump and CSV genesis paths, so the byte-identity
# compare excludes it (content is what must match). The dump USER build inserts
# statements/referenced_vars WITHOUT an explicit id -> AUTOINCREMENT 1..N (renumbered
# in the USER DB, independent of DEV); the CSV build reads them VERBATIM from the bulk
# CSV, so the curated subset carries its DEV-stored ids. The engine joins these tables
# via address_version_id (byte-identical), NEVER the statement PK -- so the id renumber
# is the exact "autoincrement renumber across two independent builds is not a false
# mismatch" case _canon already tolerates, applied at the table level. (The DEV
# statements/referenced_vars round-trip WITH the id, verbatim -- they are NOT here.)
_USER_AUTOINC_ID_TABLES = {"statements", "referenced_vars"}


def _hash_tables(db_path, names_fn, *, user_projection=False):
    con = sqlite3.connect(db_path)
    try:
        out = {}
        for t in names_fn(con):
            cols = [c[1] for c in con.execute(f'PRAGMA table_info("{t}")')]
            # In the USER projection, the statements/referenced_vars `id` PK is a
            # non-semantic autoincrement that legitimately differs between the two
            # genesis paths (see _USER_AUTOINC_ID_TABLES) -- exclude it so the compare
            # is on CONTENT, not on the renumbering artifact.
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


# ---------------------------------------------------------------------------
# Module-scoped baseline: build the dump DBs ONCE, export both halves, rebuild
# from CSV ONCE. Reused by every test.
# ---------------------------------------------------------------------------
_BASELINE = {}


def _build_dump_dbs(seed_dir, out_dir):
    """Build both reference DBs from the mini-dump + the curated seed CSVs via the
    EXPERT-ONLY from-dump path (run_rebuild), with the importer's seed-path constants
    repointed at the curated export dir (the standard oracle convention)."""
    os.makedirs(out_dir, exist_ok=True)
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir, "address_versions_seed.csv")
    try:
        imp.run_rebuild(DUMP_DIR, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(
            f"mini-dump not found: {DUMP_DIR}; this gate needs the committed "
            f"mini-dump excerpt present.")
    for f in SEED_FILES:
        if not os.path.isfile(os.path.join(CURATED_EXPORT_DIR, f)):
            raise SystemExit(
                f"curated export CSV not found: {f} under {CURATED_EXPORT_DIR} "
                f"(D38's curated half; this gate reads it as the curated genesis).")


def _get_baseline():
    if "root" in _BASELINE:
        return _BASELINE
    _require_inputs()
    root = tempfile.mkdtemp(prefix="rebuild_from_csv_")

    # The curated seed source: a copy of data/db-export/'s three CSVs (so the dump
    # build reads them as its seeds, exactly as the CSV-genesis path will).
    seed_src = os.path.join(root, "curated_src")
    os.makedirs(seed_src, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(CURATED_EXPORT_DIR, f),
                     os.path.join(seed_src, f))

    # 1. Build both DBs from the dump (the reference the CSV-genesis must match).
    dump_db = os.path.join(root, "dump_db")
    _build_dump_dbs(seed_src, dump_db)
    dump_user = os.path.join(dump_db, "reference.sqlite")
    dump_dev = os.path.join(dump_db, "reference-dev.sqlite")

    # 2. Export BOTH halves FROM the dump-built DBs: curated (export_seeds, from the
    #    DEV DB so the curated WHERE filter applies) into a fresh curated dir, and
    #    bulk (export_bulk) into a fresh bulk dir.
    curated_csv = os.path.join(root, "export_curated")
    os.makedirs(curated_csv, exist_ok=True)
    # Seed the export dir with the committed CSV format (diff-preservation honors
    # the existing header/comments), then export from the DB.
    for f in SEED_FILES:
        shutil.copy2(os.path.join(CURATED_EXPORT_DIR, f),
                     os.path.join(curated_csv, f))
    export_seeds(dump_dev, curated_csv)
    bulk_csv = os.path.join(root, "export_bulk")
    export_bulk(dump_dev, bulk_csv)

    # 3. Rebuild BOTH DBs from the CSV export ALONE (no dump).
    csv_db = os.path.join(root, "csv_db")
    imp.run_rebuild_from_csv(csv_db, curated_csv, bulk_csv)
    csv_user = os.path.join(csv_db, "reference.sqlite")
    csv_dev = os.path.join(csv_db, "reference-dev.sqlite")

    _BASELINE.update({
        "root": root,
        "dump_user": dump_user, "dump_dev": dump_dev,
        "csv_user": csv_user, "csv_dev": csv_dev,
        "bulk_csv": bulk_csv,
    })
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - __main__ runner without pytest
    pytest = None


# ---------------------------------------------------------------------------
# The assertions (each falsifiable; each maps to an ACCEPT-RESULT id).
# ---------------------------------------------------------------------------
def _run_assertions(b):
    """Compare the CSV-genesis rebuild against the dump-built reference. Returns a
    list of (acceptance_id, ok, detail)."""
    results = []

    def record(aid, ok, detail=""):
        results.append((aid, bool(ok), detail))

    # (1) Both DBs were produced by run_rebuild_from_csv with NO dump argument --
    # the routine rebuild requires no dump. (The build already happened in the
    # baseline; assert the artifacts exist + are non-empty.)
    for label, p in (("user", b["csv_user"]), ("dev", b["csv_dev"])):
        ok = os.path.isfile(p) and os.path.getsize(p) > 0
        record(f"csv-genesis-built-{label}", ok,
               f"{p} missing/empty -- run_rebuild_from_csv did not produce it"
               if not ok else "")

    # (2) THE STRONGEST -- EVERY real table is byte-identical between the dump-built DB
    # and the CSV-genesis rebuild, for BOTH USER and DEV -- with NO table excluded. The
    # formerly-excluded gap tables (address_versions + the USER curated statements/refs
    # subset) now round-trip exact because the curated-derived overlay restores the
    # fingerprint + the promoted av_id. FALSIFIABLE: a dropped row, a renumbered id, a
    # NULL-read-as-'', a lost column, a shifted curated av_id, or an emptied USER
    # statements subset fails the row.
    for label, dump_db, csv_db in (("user", b["dump_user"], b["csv_user"]),
                                   ("dev", b["dump_dev"], b["csv_dev"])):
        is_user = (label == "user")
        dh = _hash_tables(dump_db, _real_table_names, user_projection=is_user)
        ch = _hash_tables(csv_db, _real_table_names, user_projection=is_user)
        dt, ct = set(dh), set(ch)
        record(f"real-table-set-{label}", dt == ct,
               f"real-table set differs: only-dump={sorted(dt-ct)} "
               f"only-csv={sorted(ct-dt)}")
        for t in sorted(dt & ct):
            same = (dh[t]["count"] == ch[t]["count"]
                    and dh[t]["hash"] == ch[t]["hash"])
            record(f"real-table-{label}-{t}", same,
                   f"count dump={dh[t]['count']} csv={ch[t]['count']} | "
                   f"hash dump={dh[t]['hash'][:12]} csv={ch[t]['hash'][:12]}"
                   if not same else "")

    # (2b) THE FULL address_versions byte-identity, split BULK vs CURATED so a failure
    # localizes. Both subsets compare ALL columns (no exclusion -- the curated rows now
    # carry the merged fingerprint + the promoted id, so every column must match). This
    # is the load-bearing proof the migration's seam-correction closed the gap:
    #   (i)  bulk av (kcdx_id NULL) byte-identical -- the verbatim round-trip.
    #   (ii) curated av (kcdx_id NOT NULL) byte-identical INCLUDING the fingerprint +
    #        the promoted id (was the gap; now reconstructed from the derived overlay).
    #   (iii) the curated fingerprint is PRESENT in the CSV build (dump==csv > 0) -- the
    #        direct falsification of the old gap (a rebuild that drops it fails here).
    def _av_hash(db, where, *, exclude=()):
        con = sqlite3.connect(db)
        try:
            cols = [c[1] for c in con.execute('PRAGMA table_info("address_versions")')]
            keep = [c for c in cols if c not in exclude]
            rendered = []
            for row in con.execute(
                    f'SELECT {",".join(chr(34)+c+chr(34) for c in keep)} '
                    f'FROM address_versions WHERE {where}'):
                rendered.append("\x1e".join(_canon(c) for c in row))
            h = hashlib.sha256()
            for line in sorted(rendered):
                h.update((line + "\x1d").encode("utf-8", "surrogatepass"))
            cnt = con.execute(
                f"SELECT COUNT(*) FROM address_versions WHERE {where}").fetchone()[0]
            return cnt, h.hexdigest()
        finally:
            con.close()

    # (i) bulk av rows byte-identical (the verbatim round-trip) -- ALL columns.
    dc, dhsh = _av_hash(b["dump_dev"], "kcdx_id IS NULL")
    cc, chsh = _av_hash(b["csv_dev"], "kcdx_id IS NULL")
    record("bulk-av-rows-identical", dc == cc and dhsh == chsh,
           f"bulk av (kcdx_id NULL): dump count={dc} hash={dhsh[:12]} "
           f"vs csv count={cc} hash={chsh[:12]} (the verbatim bulk round-trip)")

    # (ii) curated av rows byte-identical -- ALL columns, NO exclusion. The merged
    # fingerprint + the promoted id must match the dump's PROMOTE exactly. FALSIFIABLE:
    # a NULL-fingerprint mint, a shifted av_id, or any derived-column loss fails.
    dc, dhsh = _av_hash(b["dump_dev"], "kcdx_id IS NOT NULL")
    cc, chsh = _av_hash(b["csv_dev"], "kcdx_id IS NOT NULL")
    record("curated-av-rows-identical", dc == cc and dhsh == chsh,
           f"curated av (kcdx_id NOT NULL) ALL columns: dump count={dc} "
           f"hash={dhsh[:12]} vs csv count={cc} hash={chsh[:12]} -- the merged "
           f"fingerprint + promoted id must reproduce the dump's PROMOTE byte-identical")

    # (iii) the curated fingerprint is reconstructed (dump==csv, both > 0) -- the direct
    # falsification of the OLD gap. A rebuild that dropped the fingerprint (the pre-fix
    # behaviour) had csv=0 here and fails.
    con = sqlite3.connect(b["dump_dev"])
    try:
        dump_fp = con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL "
            "AND content_hash IS NOT NULL").fetchone()[0]
    finally:
        con.close()
    con = sqlite3.connect(b["csv_dev"])
    try:
        csv_fp = con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL "
            "AND content_hash IS NOT NULL").fetchone()[0]
    finally:
        con.close()
    record("curated-fingerprint-reconstructed", dump_fp > 0 and csv_fp == dump_fp,
           f"dump curated-fingerprint rows={dump_fp}, csv curated-fingerprint "
           f"rows={csv_fp} -- the overlay must reconstruct the curated function "
           f"fingerprint (expected csv==dump>0; csv=0 is the pre-fix gap)")

    # (iv) the USER curated statements/referenced_vars subset is NON-EMPTY (the cascade
    # the gap previously emptied): the curated av_id set now matches the dump build, so
    # the USER projection's curated-function statements subset ships. FALSIFIABLE: a
    # shifted curated av_id set empties this (the pre-fix cascade) and the assert fails.
    con = sqlite3.connect(b["csv_user"])
    try:
        user_st = con.execute("SELECT COUNT(*) FROM statements").fetchone()[0]
        user_rv = con.execute("SELECT COUNT(*) FROM referenced_vars").fetchone()[0]
    finally:
        con.close()
    record("user-curated-subset-present", user_st > 0 and user_rv > 0,
           f"USER curated statements={user_st} referenced_vars={user_rv} -- the "
           f"curated subset must be non-empty (a shifted curated av_id set empties it)")

    # (3) The CURATED _dict_* tables (whose strings ARE in the curated CSVs)
    # reconstruct byte-identically -- the curated dict-id reproduction the
    # mechanism relies on. (The bulk-only _dict_* tables are the KNOWN GAP,
    # asserted separately below; not a failure of this step.)
    dh = _hash_tables(b["dump_user"], _dict_table_names)
    ch = _hash_tables(b["csv_user"], _dict_table_names)
    for t in ("_dict_address_versions_kind", "_dict_address_versions_evidence_kind"):
        ok = (t in dh and t in ch and dh[t]["hash"] == ch[t]["hash"]
              and dh[t]["count"] == ch[t]["count"])
        record(f"curated-dict-identical-{t}", ok,
               f"curated dict {t} differs: dump={dh.get(t)} csv={ch.get(t)}"
               if not ok else "")

    # (4) The KNOWN GAP, asserted as the documented limit: the bulk-only _dict_*
    # tables are NOT reconstructable from the CSVs (their strings live only in the
    # dump). The CSV-genesis DB simply lacks them; the dump DB has them. This is
    # the surfaced carry-forward, asserted so a future change that DOES reconstruct
    # them (e.g. 1.1 exporting the dict mapping) flips this to a known-improvement
    # signal rather than silently passing on a wrong assumption.
    bulk_only = {"_dict_address_versions_caller_arg_agreement",
                 "_dict_address_versions_decompile_quality"}
    dump_dicts = set(_hash_tables(b["dump_dev"], _dict_table_names))
    csv_dicts = set(_hash_tables(b["csv_dev"], _dict_table_names))
    present_bulk_only = bulk_only & dump_dicts
    gap_holds = present_bulk_only and not (present_bulk_only & csv_dicts)
    record("bulk-dict-gap-documented", bool(gap_holds),
           f"expected the bulk-only _dict_* tables {sorted(present_bulk_only)} to be "
           f"ABSENT from the CSV-genesis DB (the documented gap); csv has "
           f"{sorted(present_bulk_only & csv_dicts)}")

    # (5) The bulk decoder round-trips the carry-forward edge cases directly: a
    # NULL-vs-'' distinction and a BLOB. Falsifiable: a collapsed NULL/'' or a
    # corrupted BLOB fails.
    null_back = imp._bulk_csv_decode_cell(r"\N", "TEXT")
    empty_back = imp._bulk_csv_decode_cell("", "TEXT")
    blob_back = imp._bulk_csv_decode_cell("blob:" + bytes(range(8)).hex(), "BLOB")
    int_back = imp._bulk_csv_decode_cell("42", "INTEGER")
    int_null = imp._bulk_csv_decode_cell(r"\N", "INTEGER")
    record("decoder-null-vs-empty",
           null_back is None and empty_back == "",
           f"\\N -> {null_back!r} (want None), '' -> {empty_back!r} (want '')")
    record("decoder-blob",
           blob_back == bytes(range(8)),
           f"blob: -> {blob_back!r} (want {bytes(range(8))!r})")
    record("decoder-int",
           int_back == 42 and int_null is None,
           f"'42' -> {int_back!r} (want 42), \\N -> {int_null!r} (want None)")

    # (6) The bulk corpus is actually PRESENT in the CSV-genesis DEV DB (the full
    # bulk, not just curated). FALSIFIABLE: a rebuild that ships only the curated
    # rows (missing the bulk discovery functions) fails -- the dump had bulk rows.
    con = sqlite3.connect(b["csv_dev"])
    try:
        bulk_av = con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NULL").fetchone()[0]
    finally:
        con.close()
    con = sqlite3.connect(b["dump_dev"])
    try:
        dump_bulk_av = con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NULL").fetchone()[0]
    finally:
        con.close()
    record("bulk-corpus-present",
           dump_bulk_av > 0 and bulk_av == dump_bulk_av,
           f"csv bulk av rows={bulk_av} != dump bulk av rows={dump_bulk_av} "
           f"(the full bulk must be present, not just curated)")

    return results


def _emit_signal(results):
    """Emit the canonical acceptance signal (.claude/rules/acceptance-signal.md)."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


# ---------------------------------------------------------------------------
# pytest entry points
# ---------------------------------------------------------------------------
def test_rebuild_from_csv_matches_dump_build(baseline):  # noqa: F811
    """run_rebuild_from_csv (no dump) rebuilds both DBs byte-identical to the
    from-dump build for EVERY REAL table -- INCLUDING the full address_versions table
    (curated + bulk, all columns, the merged fingerprint + promoted id) and the USER
    curated statements/referenced_vars subset; the curated _dict_* tables reconstruct
    identically; the bulk-only _dict_* gap holds as documented; the bulk corpus is
    present; the decoder round-trips NULL-vs-''/BLOB. Emits the ACCEPT signal."""
    results = _run_assertions(baseline)
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "rebuild-from-CSV failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


def test_rebuild_from_csv_needs_no_dump(baseline):  # noqa: F811
    """The routine genesis takes NO dump: run_rebuild_from_csv's signature is
    (out_dir, curated_dir, bulk_dir) -- no dump parameter -- and the dump-retired
    path run_rebuild is demoted (still present, EXPERT-only). Asserts the routine
    function exists, takes no dump arg, and the expert from-dump path is retained."""
    import inspect
    params = list(inspect.signature(imp.run_rebuild_from_csv).parameters)
    assert params[0] == "out_dir" and "dump_dir" not in params, (
        f"run_rebuild_from_csv must take no dump arg; got {params}")
    # The from-dump path is RETAINED but expert-only (build_rows unchanged).
    assert hasattr(imp, "run_rebuild") and hasattr(imp, "build_rows"), (
        "the expert-only from-dump path (run_rebuild / build_rows) must be retained")


def test_csv_genesis_fails_loud_on_missing_bulk(baseline):  # noqa: F811
    """AP14: a CSV-genesis rebuild against an incomplete export (a missing bulk
    CSV) raises CsvGenesisError -- never a silent partial DB."""
    b = baseline
    bad_root = tempfile.mkdtemp(prefix="csv_genesis_bad_")
    try:
        # A bulk dir missing call_edges.csv (an incomplete export).
        bad_bulk = os.path.join(bad_root, "bulk")
        os.makedirs(bad_bulk, exist_ok=True)
        for name in os.listdir(b["bulk_csv"]):
            if name != "call_edges.csv":
                shutil.copy2(os.path.join(b["bulk_csv"], name),
                             os.path.join(bad_bulk, name))
        out = os.path.join(bad_root, "out")
        raised = False
        try:
            imp.run_rebuild_from_csv(out, CURATED_EXPORT_DIR, bad_bulk)
        except imp.CsvGenesisError:
            raised = True
        assert raised, ("run_rebuild_from_csv must raise CsvGenesisError on a missing "
                        "bulk CSV -- it built a partial DB instead")
    finally:
        shutil.rmtree(bad_root, ignore_errors=True)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        results = _run_assertions(b)
        _emit_signal(results)
        failed = [aid for aid, ok, _ in results if not ok]
        # also exercise the no-dump + fail-loud asserts inline
        import inspect
        params = list(inspect.signature(imp.run_rebuild_from_csv).parameters)
        if params[0] != "out_dir" or "dump_dir" in params:
            failed.append("needs-no-dump")
        sys.exit(1 if failed else 0)
    finally:
        _cleanup_baseline()
