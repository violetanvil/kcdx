"""test_bulk_exporter.py -- the DB->CSV->DB lossless round-trip oracle for the
BULK exporter (seeds-to-tracked-csv-migration P1.1; D38's bulk half).

WHAT THIS PROVES
----------------
`export_bulk` (bulk_exporter) captures the DEV DB's bulk half -- the DEV-only
tables statements / referenced_vars / call_edges + the kcdx_id-NULL bulk
address_versions discovery rows -- to CSV LOSSLESSLY: every row, every column
verbatim (incl. the id PK, the FK integers, the dict-encoded ids as stored, and
the BLOB content_hash), with NULL distinguished from the empty string ''. The
production counterpart to the P0.1 round-trip probe, which proved the ~17.7M-row
DEV bulk round-trips DB->CSV->DB byte/value-identical.

PLUS the curated-derived OVERLAY (the seam-correction this migration adds):
address_versions_derived.csv carries, per CURATED av row (kcdx_id NOT NULL), the
row's DERIVED columns (content_hash / length / observed_arg_slots /
caller_reg_arg_count / caller_arg_agreement / auto_name / decompile_quality /
valid_through) + its promoted id, keyed by id + kcdx_id. The curated seed CSV
cannot carry these (a derived column is forbidden on the authored surface), so
without this overlay the curated function fingerprint falls through both CSV halves
-- D38's lossless-round-trip gap. This test asserts the overlay is present, has the
expected columns, covers exactly the curated rows, and carries the curated function
fingerprint with its exact stored values (FALSIFIABLE: a curated row exported
WITHOUT its fingerprint -- the pre-fix behaviour -- fails).

FIXTURE -- a tiny SYNTHETIC DEV DB (not the live dump)
-----------------------------------------------------
The round-trip is a pure DB->CSV->DB property; it needs no real dump data, only a
DB with the production schema and rows that exercise the edge cases. So this test
builds a small synthetic DEV DB from seeds_shared.schema.SCHEMA's OWN CREATE TABLE
statements (so the fixture schema == the production schema by construction) and
seeds it with hand-chosen rows that hit every lossless-property edge:

  - a NULL cell AND an empty-string '' cell in the same TEXT column (the \\N vs ''
    distinction the probe's sentinel preserves),
  - a BLOB content_hash with real bytes AND a NULL content_hash (the blob: hex
    round-trip),
  - sparse / non-1..N id PKs and FK integers (so a renumber-on-reinsert is caught),
  - both curated (kcdx_id NOT NULL) and bulk (kcdx_id NULL) address_versions rows
    (so the kcdx_id-NULL filter is exercised -- the curated rows must NOT appear in
    the bulk CSV).

This is fast (in-memory-sized tables, no rebuild). Full-dump fidelity is the P0.1
probe's domain + P1.4's widened oracle.

THE STRONGEST ASSERT -- LOSSLESS ROUND-TRIP
-------------------------------------------
export -> reinsert each CSV into a FRESH table (recreated from the same SCHEMA
CREATE TABLE sql, explicit-id INSERT, no AUTOINCREMENT renumber) -> diff the
rebuilt table against the source value-by-value. FALSIFIABLE: a renumbered id, a
NULL read back as '', a corrupted/dropped BLOB, a missing or short row each makes
the diff fail. The other asserts (table present, row counts, per-column header
completeness) are weaker checks that localize a failure; the round-trip is the
real bar.

ACCEPTANCE SIGNAL
-----------------
A headless DB-shape + round-trip assertion (no engine, no game launch). Emits the
canonical acceptance signal (.claude/rules/acceptance-signal.md) -- ACCEPT-RESULT
per item + one ACCEPT-SUITE aggregate -- to stdout (the data-core's DB-pipeline
test sink), so the agent reads one greppable result-line set.

RUN
---
    python -m pytest data/refdata-extractor/tests/test_bulk_exporter.py -v
    python data/refdata-extractor/tests/test_bulk_exporter.py
"""
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))

sys.path.insert(0, PYDIR)
from seeds_shared import (  # noqa: E402
    export_bulk, BulkExportError, BULK_DEV_ONLY_TABLES, BULK_AV_TABLE,
    AV_DERIVED_TABLE, AV_DERIVED_CSV_COLS, BULK_CSV_NAMES, SCHEMA,
)


# ---------------------------------------------------------------------------
# Synthetic DEV DB fixture -- built from SCHEMA's own CREATE TABLE statements.
# ---------------------------------------------------------------------------
def _create_sql(table):
    """The CREATE TABLE statement for `table`, built from SCHEMA (the production
    column names + SQL types). The fixture schema == production schema by
    construction -- a schema drift in production is reflected here automatically."""
    cols = ", ".join(f'"{name}" {sqltype}' for (name, sqltype) in SCHEMA[table])
    return f'CREATE TABLE "{table}" ({cols})'


def _insert(con, table, row):
    """Insert one row dict into `table`, naming every column explicitly (so the id
    PK is written verbatim, never autoincrement-assigned)."""
    cols = list(row.keys())
    ph = ", ".join("?" for _ in cols)
    con.execute(
        f'INSERT INTO "{table}" ({", ".join(chr(34)+c+chr(34) for c in cols)}) '
        f"VALUES ({ph})",
        [row[c] for c in cols])


# Real-ish bytes for a content_hash BLOB (a 32-byte sha256-shaped value).
_HASH_A = bytes(range(32))
_HASH_B = bytes((255 - i) for i in range(32))


def _build_synthetic_dev_db(path):
    """Create a small DEV DB at `path` with rows exercising every lossless edge:
    NULL vs '', BLOB vs NULL-BLOB, sparse ids/FKs, curated + bulk av rows."""
    con = sqlite3.connect(path)
    try:
        # Create the bulk tables this export captures (the DEV-only set + av).
        for table in BULK_DEV_ONLY_TABLES + [BULK_AV_TABLE]:
            con.execute(_create_sql(table))

        # address_versions: a CURATED row (kcdx_id NOT NULL -- must NOT appear in
        # the bulk CSV) and BULK rows (kcdx_id NULL -- the discovery rows). Sparse
        # ids. One bulk row carries a content_hash BLOB; one carries NULL hash; one
        # exercises signature='' (empty TEXT) vs another's NULL signature.
        _insert(con, "address_versions", {
            "id": 5, "kcdx_id": 101, "kind": 1, "module_id": 1, "rva": 0x1000,
            "length": 64, "content_hash": _HASH_A, "value": None,
            "signature": "i64 (i64)", "observed_arg_slots": 1,
            "caller_reg_arg_count": 1, "caller_arg_agreement": None,
            "offset": None, "vtable_slot": None,
            "last_verified_at_version": 1, "verified_by": "tester",
            "verified_date": "2026-06-10", "evidence_kind": 1,
            "auto_name": None, "decompile_quality": None,
            "valid_from": 1, "valid_through": None, "struct_offset": None,
            "aob": None, "anchor_string": None, "rule": None, "slot_count": None,
            "expect_unique": None, "derives_from": None,
        })
        _insert(con, "address_versions", {
            "id": 42, "kcdx_id": None, "kind": 1, "module_id": 1, "rva": 0x2000,
            "length": 128, "content_hash": _HASH_B, "value": None,
            "signature": "",  # empty TEXT -- must round-trip as '' not NULL
            "observed_arg_slots": 0, "caller_reg_arg_count": 0,
            "caller_arg_agreement": None, "offset": None, "vtable_slot": None,
            "last_verified_at_version": None, "verified_by": None,
            "verified_date": None, "evidence_kind": None,
            "auto_name": "FUN_2000", "decompile_quality": 2,
            "valid_from": 1, "valid_through": None, "struct_offset": None,
            "aob": None, "anchor_string": None, "rule": None, "slot_count": None,
            "expect_unique": None, "derives_from": None,
        })
        _insert(con, "address_versions", {
            "id": 99, "kcdx_id": None, "kind": 1, "module_id": 1, "rva": 0x3000,
            "length": None, "content_hash": None,  # NULL BLOB -- distinct from b''
            "value": None,
            "signature": None,  # NULL -- must round-trip as NULL not ''
            "observed_arg_slots": None, "caller_reg_arg_count": None,
            "caller_arg_agreement": None, "offset": None, "vtable_slot": None,
            "last_verified_at_version": None, "verified_by": None,
            "verified_date": None, "evidence_kind": None,
            "auto_name": "FUN_3000", "decompile_quality": None,
            "valid_from": 1, "valid_through": None, "struct_offset": None,
            "aob": None, "anchor_string": None, "rule": None, "slot_count": None,
            "expect_unique": None, "derives_from": None,
        })

        # statements: rows with a BLOB content_hash, a NULL hash, an empty '' vs
        # NULL pseudo_text/callee, sparse ids + FK to av rows.
        _insert(con, "statements", {
            "id": 3, "address_version_id": 42, "kcdx_id": None, "idx": 0,
            "kind": 1, "pseudo_text": "mov eax, ebx", "byte_range_start": 0,
            "byte_range_len": 2, "content_hash": _HASH_A, "callee": "FUN_9000",
            "string_ref": "",  # empty TEXT
        })
        _insert(con, "statements", {
            "id": 7, "address_version_id": 99, "kcdx_id": None, "idx": 1,
            "kind": 2, "pseudo_text": "",  # empty TEXT vs the NULL below
            "byte_range_start": 2, "byte_range_len": 5, "content_hash": None,
            "callee": None,  # NULL vs the '' string_ref above
            "string_ref": "L\"hello, world\"",  # comma + quote -> csv-quoted cell
        })

        # referenced_vars: sparse ids, NULL + value mix.
        _insert(con, "referenced_vars", {
            "id": 11, "address_version_id": 42, "kcdx_id": None,
            "statement_idx": 0, "var_name": "local_8", "storage_kind": 1,
            "storage_detail": "rbp-8", "size_bytes": 8, "data_type": 1,
        })
        _insert(con, "referenced_vars", {
            "id": 12, "address_version_id": 99, "kcdx_id": None,
            "statement_idx": 1, "var_name": None, "storage_kind": None,
            "storage_detail": "", "size_bytes": None, "data_type": None,
        })

        # call_edges: both endpoints, sparse ids, NULL kcdx ids.
        _insert(con, "call_edges", {
            "id": 21, "caller_address_version_id": 42,
            "callee_address_version_id": 99, "caller_kcdx_id": None,
            "callee_kcdx_id": None, "callsite_rva": 0x2010,
        })
        con.commit()
    finally:
        con.close()


# ---------------------------------------------------------------------------
# Round-trip helpers (mirroring the P0.1 probe's verbatim-reinsert recipe).
# ---------------------------------------------------------------------------
def _csv_to_cell(s, decl_type):
    """The inverse of bulk_exporter.cell_to_csv: \\N -> None, blob: -> bytes, and a
    type-aware decode of the raw value. This is the rebuild-side (P1.3) decode; the
    test carries its own copy to prove the export is reversible without depending on
    P1.3's not-yet-built importer."""
    if s == r"\N":
        return None
    if s.startswith("blob:"):
        return bytes.fromhex(s[5:])
    t = (decl_type or "").upper()
    if "INT" in t:
        return int(s) if s != "" else None
    if t in ("REAL", "FLOA", "DOUB"):
        return float(s) if s != "" else None
    return s  # TEXT (incl. the empty string '', preserved)


def _reinsert_csv(dst_con, src_con, table, csv_path, *, where=None):
    """Recreate `table` in dst from the SAME SCHEMA CREATE TABLE sql, then reinsert
    every CSV row VERBATIM (explicit-id INSERT, no autoincrement renumber). `where`
    mirrors the export filter so the source-side diff compares the same row set."""
    import csv as _csv
    dst_con.execute(_create_sql(table))
    decl = {name: sqltype for (name, sqltype) in SCHEMA[table]}
    with open(csv_path, newline="", encoding="utf-8") as f:
        r = _csv.reader(f)
        cols = next(r)
        ph = ", ".join("?" for _ in cols)
        ins = (f'INSERT INTO "{table}" '
               f'({", ".join(chr(34)+c+chr(34) for c in cols)}) VALUES ({ph})')
        batch = []
        for line in r:
            batch.append([_csv_to_cell(line[i], decl[cols[i]])
                          for i in range(len(cols))])
        if batch:
            dst_con.executemany(ins, batch)
    dst_con.commit()


def _table_columns(con, table):
    return [r[1] for r in con.execute(f'PRAGMA table_info("{table}")')]


def _diff_table(src_con, dst_con, table, *, where=None):
    """Full value-equality: row count + every row's every column, ordered by id so
    the compare is order-stable. The source side applies the SAME `where` the export
    did (so the curated av rows the bulk CSV omits are not counted against it).
    Returns (ok, detail)."""
    cols = _table_columns(src_con, table)
    sel = f'SELECT {", ".join(chr(34)+c+chr(34) for c in cols)} FROM "{table}"'
    src_sel = sel + (f" WHERE {where}" if where else "") + " ORDER BY id"
    dst_sel = sel + " ORDER BY id"  # the rebuilt table only holds the exported rows
    src_rows = src_con.execute(src_sel).fetchall()
    dst_rows = dst_con.execute(dst_sel).fetchall()
    if len(src_rows) != len(dst_rows):
        return False, f"row count {len(src_rows)} (src) != {len(dst_rows)} (rebuilt)"
    for i, (ra, rb) in enumerate(zip(src_rows, dst_rows)):
        if ra != rb:
            for ci, cn in enumerate(cols):
                if ra[ci] != rb[ci]:
                    return False, (f"row {i} col '{cn}': src={ra[ci]!r} != "
                                   f"rebuilt={rb[ci]!r}")
            return False, f"row {i}: tuple differs ({ra!r} != {rb!r})"
    return True, f"{len(src_rows)} rows, all {len(cols)} columns identical"


def _src_count(con, table, *, where=None):
    sql = f'SELECT COUNT(*) FROM "{table}"'
    if where:
        sql += f" WHERE {where}"
    return con.execute(sql).fetchone()[0]


# ---------------------------------------------------------------------------
# The assertions (each falsifiable; each maps to an ACCEPT-RESULT id).
# ---------------------------------------------------------------------------
# The export filter per table -- DEV-only tables export whole; av exports the
# kcdx_id-NULL subset only.
def _export_where(table):
    return "kcdx_id IS NULL" if table == BULK_AV_TABLE else None


def _run_assertions(dev_db, out_dir):
    """Export the bulk from `dev_db` into `out_dir`, then run every falsifiable
    check. Returns a list of (acceptance_id, ok, detail)."""
    results = []

    def record(aid, ok, detail=""):
        results.append((aid, bool(ok), detail))

    written = export_bulk(dev_db, out_dir)
    all_tables = BULK_DEV_ONLY_TABLES + [BULK_AV_TABLE]

    src = sqlite3.connect(dev_db)
    try:
        # (1) Every bulk table present in the output.
        for table in all_tables:
            name = BULK_CSV_NAMES[table]
            present = (name in written
                       and os.path.isfile(os.path.join(out_dir, name)))
            record(f"table-present-{table}", present,
                   f"{name} missing from output" if not present else "")

        # (2) Row counts match the DEV DB's bulk row counts (with the export
        # filter). FALSIFIABLE: a short export fails.
        for table in all_tables:
            name = BULK_CSV_NAMES[table]
            where = _export_where(table)
            expected = _src_count(src, table, where=where)
            got = written.get(name, -1)
            record(f"rowcount-{table}", got == expected,
                   f"exported {got} != DEV count {expected} (where={where})")

        # The av export must EXCLUDE the curated rows (kcdx_id NOT NULL). Falsifiable:
        # if the filter were dropped, the bulk count would include the curated row.
        curated_av = _src_count(src, BULK_AV_TABLE, where="kcdx_id IS NOT NULL")
        bulk_av = _src_count(src, BULK_AV_TABLE, where="kcdx_id IS NULL")
        av_count = written.get(BULK_CSV_NAMES[BULK_AV_TABLE], -1)
        record("av-excludes-curated",
               curated_av > 0 and av_count == bulk_av and av_count < (curated_av + bulk_av),
               f"bulk_av_exported={av_count} bulk={bulk_av} curated={curated_av} "
               f"(curated must NOT be in the bulk CSV)")

        # (3) Every column captured -- the CSV header equals the table's columns,
        # in order. FALSIFIABLE: a dropped or misordered column fails.
        import csv as _csv
        for table in all_tables:
            name = BULK_CSV_NAMES[table]
            path = os.path.join(out_dir, name)
            with open(path, newline="", encoding="utf-8") as f:
                header = next(_csv.reader(f))
            db_cols = _table_columns(src, table)
            record(f"columns-complete-{table}", header == db_cols,
                   f"header={header} != db_cols={db_cols}")

        # (4) THE STRONGEST -- lossless round-trip per table: export -> reinsert
        # verbatim into a fresh table -> value-identical diff (incl. a NULL-vs-''
        # case + a BLOB case, both seeded into the fixture). FALSIFIABLE: a
        # renumbered id, a NULL-read-as-'', a corrupted BLOB, or a short row fails.
        rebuilt = tempfile.mkdtemp(prefix="bulk_rt_")
        try:
            dst_path = os.path.join(rebuilt, "rebuilt-dev.sqlite")
            dst = sqlite3.connect(dst_path)
            try:
                for table in all_tables:
                    where = _export_where(table)
                    csv_path = os.path.join(out_dir, BULK_CSV_NAMES[table])
                    _reinsert_csv(dst, src, table, csv_path, where=where)
                    ok, detail = _diff_table(src, dst, table, where=where)
                    record(f"roundtrip-{table}", ok, detail)
            finally:
                dst.close()
        finally:
            shutil.rmtree(rebuilt, ignore_errors=True)

        # (5) THE CURATED-DERIVED OVERLAY -- the seam-correction this migration adds.
        # address_versions_derived.csv carries, per CURATED av row (kcdx_id NOT NULL),
        # the row's DERIVED columns + its promoted id + kcdx_id key. The curated seed
        # CSV cannot carry these (a derived column is forbidden on the authored
        # surface), so without this overlay the curated function fingerprint falls
        # through both CSV halves (D38's lossless-round-trip gap).
        avd_name = BULK_CSV_NAMES[AV_DERIVED_TABLE]
        avd_path = os.path.join(out_dir, avd_name)
        # (5a) overlay present + header == AV_DERIVED_CSV_COLS (id, kcdx_id, then the
        # derived payload). FALSIFIABLE: a dropped/misordered column fails.
        import csv as _csv2
        present = avd_name in written and os.path.isfile(avd_path)
        record("av-derived-overlay-present", present,
               f"{avd_name} missing from output" if not present else "")
        if present:
            with open(avd_path, newline="", encoding="utf-8") as f:
                avd_header = next(_csv2.reader(f))
            record("av-derived-header", avd_header == AV_DERIVED_CSV_COLS,
                   f"header={avd_header} != expected={AV_DERIVED_CSV_COLS}")

        # (5b) the overlay carries exactly the CURATED av rows (kcdx_id NOT NULL),
        # keyed by id + kcdx_id. FALSIFIABLE: a short/over export fails.
        curated_av = _src_count(src, BULK_AV_TABLE, where="kcdx_id IS NOT NULL")
        record("av-derived-rowcount", written.get(avd_name, -1) == curated_av,
               f"overlay rows={written.get(avd_name, -1)} != curated av rows={curated_av}")

        # (5c) THE LOAD-BEARING falsifiable assert: the curated row's DERIVED columns
        # (content_hash/length/observed_arg_slots/caller_reg_arg_count/...) are present
        # in the overlay with their EXACT stored values. The fixture's curated row
        # (id=5, kcdx_id=101) seeds content_hash=_HASH_A, length=64,
        # observed_arg_slots=1, caller_reg_arg_count=1. FALSIFIABLE: a curated function
        # row exported WITHOUT its fingerprint (the pre-fix behaviour -- the gap)
        # leaves these NULL/absent and fails. Reinsert the overlay verbatim + read back.
        if present:
            rebuilt2 = tempfile.mkdtemp(prefix="avd_rt_")
            try:
                d2 = sqlite3.connect(os.path.join(rebuilt2, "ov.sqlite"))
                try:
                    # A table holding just the overlay columns, reinserted verbatim.
                    coldefs = ", ".join(f'"{c}"' for c in AV_DERIVED_CSV_COLS)
                    d2.execute(f'CREATE TABLE av_derived ({coldefs})')
                    av_decl = {n: t for (n, t) in SCHEMA["address_versions"]}
                    with open(avd_path, newline="", encoding="utf-8") as f:
                        r = _csv2.reader(f)
                        cols = next(r)
                        ph = ", ".join("?" for _ in cols)
                        ins = (f'INSERT INTO av_derived '
                               f'({", ".join(chr(34)+c+chr(34) for c in cols)}) '
                               f"VALUES ({ph})")
                        rows = [[_csv_to_cell(line[i], av_decl[cols[i]])
                                 for i in range(len(cols))] for line in r]
                        d2.executemany(ins, rows)
                    d2.commit()
                    rec = d2.execute(
                        "SELECT id, content_hash, length, observed_arg_slots, "
                        "caller_reg_arg_count FROM av_derived WHERE kcdx_id=101"
                    ).fetchone()
                    ok5c = (rec is not None and rec[0] == 5
                            and bytes(rec[1]) == _HASH_A and rec[2] == 64
                            and rec[3] == 1 and rec[4] == 1)
                    record("av-derived-carries-curated-fingerprint", ok5c,
                           f"curated kcdx_id=101 overlay row={rec!r} -- expected "
                           f"(id=5, content_hash=_HASH_A, length=64, "
                           f"observed_arg_slots=1, caller_reg_arg_count=1); a curated "
                           f"function row exported WITHOUT its fingerprint fails here "
                           f"(the pre-fix gap)")
                finally:
                    d2.close()
            finally:
                shutil.rmtree(rebuilt2, ignore_errors=True)
    finally:
        src.close()

    return results


def _emit_signal(results):
    """Emit the canonical acceptance signal (.claude/rules/acceptance-signal.md) to
    stdout -- one ACCEPT-RESULT per item, one ACCEPT-SUITE aggregate last."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


def _with_fixture(fn):
    """Build the synthetic DEV DB + an out dir, run fn(dev_db, out_dir), clean up."""
    root = tempfile.mkdtemp(prefix="bulk_export_")
    try:
        dev_db = os.path.join(root, "reference-dev.sqlite")
        out_dir = os.path.join(root, "db-export-bulk")
        _build_synthetic_dev_db(dev_db)
        return fn(dev_db, out_dir, root)
    finally:
        shutil.rmtree(root, ignore_errors=True)


# ---------------------------------------------------------------------------
# pytest entry points
# ---------------------------------------------------------------------------
def test_bulk_export_lossless_round_trip():
    """The bulk exporter captures statements/referenced_vars/call_edges + the
    kcdx_id-NULL address_versions losslessly: every table present, row counts
    match, every column captured, and each table round-trips DB->CSV->DB
    value-identical (incl. NULL-vs-'' + BLOB). Emits the canonical ACCEPT signal."""
    def run(dev_db, out_dir, _root):
        results = _run_assertions(dev_db, out_dir)
        _emit_signal(results)
        failures = [(aid, detail) for aid, ok, detail in results if not ok]
        assert not failures, "bulk-export round-trip failures:\n  " + \
            "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)
    _with_fixture(run)


def test_bulk_export_fails_loud_on_missing_table():
    """AP14: a DEV DB missing a bulk table (e.g. a USER DB, which lacks call_edges)
    raises BulkExportError -- never a silent empty/partial bulk CSV."""
    def run(_dev_db, _out_dir, root):
        # A DB with only address_versions (call_edges absent -- the USER-DB shape).
        bad = os.path.join(root, "user-like.sqlite")
        con = sqlite3.connect(bad)
        try:
            con.execute(_create_sql("address_versions"))
            con.commit()
        finally:
            con.close()
        out = os.path.join(root, "out-bad")
        raised = False
        try:
            export_bulk(bad, out)
        except BulkExportError:
            raised = True
        assert raised, ("export_bulk must raise BulkExportError on a DB missing a "
                        "bulk table -- it silently produced output instead")
    _with_fixture(run)


def test_bulk_export_null_distinct_from_empty_string():
    """The load-bearing fidelity case, asserted directly: a NULL TEXT cell and an
    empty-string '' TEXT cell in the same column survive the round-trip DISTINCT.
    FALSIFIABLE: if the encoding collapsed NULL and '' (the classic CSV bug), the
    rebuilt signature cells would both read '' (or both NULL) and this fails."""
    def run(dev_db, out_dir, root):
        export_bulk(dev_db, out_dir)
        # Reinsert the av bulk CSV and read the two signature cells back.
        rebuilt = os.path.join(root, "rebuilt.sqlite")
        dst = sqlite3.connect(rebuilt)
        src = sqlite3.connect(dev_db)
        try:
            _reinsert_csv(dst, src, BULK_AV_TABLE,
                          os.path.join(out_dir, BULK_CSV_NAMES[BULK_AV_TABLE]),
                          where="kcdx_id IS NULL")
            # id=42 seeded signature='' ; id=99 seeded signature=NULL.
            sig_empty = dst.execute(
                "SELECT signature FROM address_versions WHERE id=42").fetchone()[0]
            sig_null = dst.execute(
                "SELECT signature FROM address_versions WHERE id=99").fetchone()[0]
            assert sig_empty == "", f"empty-string signature round-tripped as {sig_empty!r}"
            assert sig_null is None, f"NULL signature round-tripped as {sig_null!r}"
            # content_hash: id=42 BLOB, id=99 NULL -- the BLOB case round-trips.
            hash_blob = dst.execute(
                "SELECT content_hash FROM address_versions WHERE id=42").fetchone()[0]
            hash_null = dst.execute(
                "SELECT content_hash FROM address_versions WHERE id=99").fetchone()[0]
            assert bytes(hash_blob) == bytes((255 - i) for i in range(32)), \
                "BLOB content_hash did not round-trip byte-identical"
            assert hash_null is None, f"NULL content_hash round-tripped as {hash_null!r}"
        finally:
            dst.close()
            src.close()
    _with_fixture(run)


def test_curated_derived_overlay_carries_fingerprint():
    """The seam-correction: address_versions_derived.csv carries each CURATED av row's
    DERIVED columns + promoted id, so the curated function fingerprint round-trips
    (D38's lossless bar). The fixture's curated row (kcdx_id=101) seeds a real
    fingerprint (content_hash=_HASH_A, length=64, observed_arg_slots=1,
    caller_reg_arg_count=1); the overlay must carry it with the exact stored values.
    FALSIFIABLE: a curated function row exported WITHOUT its fingerprint -- the pre-fix
    behaviour the curated/bulk split caused -- leaves these absent and fails."""
    def run(dev_db, out_dir, root):
        export_bulk(dev_db, out_dir)
        avd_path = os.path.join(out_dir, BULK_CSV_NAMES[AV_DERIVED_TABLE])
        assert os.path.isfile(avd_path), (
            "export_bulk must write address_versions_derived.csv -- the curated-derived "
            "overlay is missing, the curated fingerprint would be lost")
        import csv as _csv
        rebuilt = os.path.join(root, "ov.sqlite")
        d2 = sqlite3.connect(rebuilt)
        try:
            coldefs = ", ".join(f'"{c}"' for c in AV_DERIVED_CSV_COLS)
            d2.execute(f'CREATE TABLE av_derived ({coldefs})')
            av_decl = {n: t for (n, t) in SCHEMA["address_versions"]}
            with open(avd_path, newline="", encoding="utf-8") as f:
                r = _csv.reader(f)
                cols = next(r)
                assert cols == AV_DERIVED_CSV_COLS, (
                    f"overlay header {cols} != expected {AV_DERIVED_CSV_COLS}")
                ph = ", ".join("?" for _ in cols)
                ins = (f'INSERT INTO av_derived '
                       f'({", ".join(chr(34)+c+chr(34) for c in cols)}) VALUES ({ph})')
                rows = [[_csv_to_cell(line[i], av_decl[cols[i]])
                         for i in range(len(cols))] for line in r]
                d2.executemany(ins, rows)
            d2.commit()
            # The curated row (kcdx_id=101) seeded id=5, content_hash=_HASH_A, length=64,
            # observed_arg_slots=1, caller_reg_arg_count=1 in the fixture.
            rec = d2.execute(
                "SELECT id, content_hash, length, observed_arg_slots, "
                "caller_reg_arg_count FROM av_derived WHERE kcdx_id=101").fetchone()
            assert rec is not None, "curated kcdx_id=101 absent from the overlay"
            assert rec[0] == 5, f"promoted id round-tripped as {rec[0]!r} (want 5)"
            assert bytes(rec[1]) == _HASH_A, "curated content_hash did not round-trip"
            assert rec[2] == 64, f"curated length round-tripped as {rec[2]!r} (want 64)"
            assert rec[3] == 1, f"observed_arg_slots round-tripped as {rec[3]!r} (want 1)"
            assert rec[4] == 1, (
                f"caller_reg_arg_count round-tripped as {rec[4]!r} (want 1)")
        finally:
            d2.close()
    _with_fixture(run)


def test_av_derived_partition_coverage_assert_is_real():
    """The av-column PARTITION coverage assert in _curated_av_derived_cols is REAL,
    not a tautology: a NEW SCHEMA address_versions column classified into NONE of the
    three explicit sets (authored / key / derived) makes the partition incomplete and
    FIRES at call time, naming the unclassified column.

    This is the guard the module comment promises -- the future-gap protection a
    column added to the SCHEMA but to no partition set is NOT silently swept into the
    derived overlay. FALSIFIABLE: if `derived` were still computed as
    (all_cols - authored - keys), the injected column would land in `derived` by
    construction, the assert could never fire, and this test would FAIL (no
    AssertionError raised). The test injects a synthetic SCHEMA column, confirms the
    assert raises and names it, then restores the SCHEMA so no other test is affected.
    """
    from seeds_shared import bulk_exporter as _bx

    sentinel = "synthetic_unclassified_col_for_test"
    av = _bx.SCHEMA[BULK_AV_TABLE]
    assert sentinel not in {n for (n, _t) in av}, (
        "the sentinel column must not already exist in the SCHEMA")
    # Inject a SCHEMA column that is in NONE of authored/key/derived.
    av.append((sentinel, "INTEGER"))
    try:
        raised = False
        msg = ""
        try:
            _bx._curated_av_derived_cols()
        except AssertionError as e:
            raised = True
            msg = str(e)
        assert raised, (
            "_curated_av_derived_cols did NOT raise on an unclassified SCHEMA av "
            "column -- the coverage assert is a tautology (a new column would be "
            "silently swept into the derived overlay)")
        assert sentinel in msg, (
            f"the assert fired but did not name the unclassified column {sentinel!r} "
            f"(AP14: the failure must name what was misclassified); message={msg!r}")
    finally:
        # Restore the SCHEMA exactly (remove only the column this test appended).
        if av and av[-1][0] == sentinel:
            av.pop()
        assert sentinel not in {n for (n, _t) in _bx.SCHEMA[BULK_AV_TABLE]}, (
            "SCHEMA restoration failed -- the sentinel column leaked to other tests")
    # The unpatched partition is well-formed: the normal call returns the 8 derived
    # columns in SCHEMA order, no assert. (Confirms restore + the happy path.)
    derived = _bx._curated_av_derived_cols()
    assert derived == [
        "length", "content_hash", "observed_arg_slots", "caller_reg_arg_count",
        "caller_arg_agreement", "auto_name", "decompile_quality", "valid_through",
    ], f"derived payload drifted from the expected 8 columns: {derived}"


if __name__ == "__main__":
    def run(dev_db, out_dir, _root):
        results = _run_assertions(dev_db, out_dir)
        _emit_signal(results)
        return [aid for aid, ok, _ in results if not ok]
    failed = _with_fixture(run)
    sys.exit(1 if failed else 0)
