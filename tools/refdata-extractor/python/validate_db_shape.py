"""validate_db_shape.py -- THE DB-SHAPE GATE for the flattened address-name/
address-version schema (2026-05-28).

Builds BOTH reference DBs (USER + DEV) from a dump dir via import_to_sqlite.py,
then asserts the locked schema's shape against falsifiable answers: table
presence per db, the kcdx_id baseline count, the all-open baseline intervals,
the partial-unique-open-interval invariant, the USER/DEV column projection, the
address_names seeding count (from seed.csv), the content_hash BLOB round-trip,
and FK resolution.

Mirrors validate_extractor_output.py's shape: check()/PASS/FAIL, a VERDICT line,
sys.exit(1) on any FAIL.

RUN
---
    python validate_db_shape.py [dump_dir]
(default dump_dir = C:\\kcdx-refdata\\refdata-full-20260527-105617). Builds into a
temp out dir, runs the checks, cleans up.

NOTE: against the real dump (~321K rows) the build takes ~a minute. The real-dump
run is the maintainer's gate; this harness can also point at a synthetic dump dir
for a fast smoke test (any dir with the six dump-table subdirs + matching headers).
"""
import csv
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import import_to_sqlite as imp   # noqa: E402

DEFAULT_DUMP = r"C:\kcdx-refdata\refdata-full-20260527-105617"

_results = []


def check(name, ok, detail=""):
    _results.append((name, bool(ok), detail))
    status = "PASS" if ok else "FAIL"
    line = "  [%s] %s" % (status, name)
    if detail:
        line += "  -- " + detail
    print(line, flush=True)


# ---------------------------------------------------------------------------
# DB introspection helpers.
# ---------------------------------------------------------------------------
def tables(con):
    return {r[0] for r in con.execute(
        "SELECT name FROM sqlite_master WHERE type='table'").fetchall()}


def columns(con, table):
    return [r[1] for r in con.execute(f'PRAGMA table_info("{table}")').fetchall()]


def scalar(con, sql, params=()):
    return con.execute(sql, params).fetchone()[0]


def triggers(con):
    return {r[0] for r in con.execute(
        "SELECT name FROM sqlite_master WHERE type='trigger'").fetchall()}


# ---------------------------------------------------------------------------
# Independent dump-side answers (read the CSVs, not the DB).
# ---------------------------------------------------------------------------
def count_dump_functions(dump_dir):
    n = 0
    for _ in imp.iter_table(dump_dir, "functions"):
        n += 1
    return n


def dump_hash_for_rva(dump_dir, rva_int):
    for r in imp.iter_table(dump_dir, "functions"):
        if imp.parse_int(r.get("rva", "")) == rva_int:
            return (r.get("content_hash") or "").strip()
    return None


# ---------------------------------------------------------------------------
# The checks.
# ---------------------------------------------------------------------------
def run_checks(dump_dir, user_db, dev_db):
    uc = sqlite3.connect(user_db)
    dc = sqlite3.connect(dev_db)

    ut = tables(uc)
    dt = tables(dc)

    # --- 1. table presence per db ---
    dev_set = {"modules", "game_versions", "address_names", "address_versions",
               "meta", "statements", "referenced_vars", "call_edges"}
    check("DEV has all 8 schema tables", dev_set.issubset(dt),
          "missing=%s" % (dev_set - dt))
    user_set = {"modules", "game_versions", "address_names", "address_versions",
                "meta"}
    check("USER has the 5 user tables", user_set.issubset(ut),
          "missing=%s" % (user_set - ut))
    dev_only = {"statements", "referenced_vars", "call_edges"}
    check("USER does NOT have statements/referenced_vars/call_edges",
          not (dev_only & ut), "present=%s" % (dev_only & ut))

    # --- 2. address_versions: every function rva from the dump has a row, +
    #        curated-minted-with-rva rows + curated-minted-no-rva rows.
    n_fn = count_dump_functions(dump_dir)
    n_av = scalar(dc, "SELECT COUNT(*) FROM address_versions")
    check("DEV address_versions count >= functions count",
          n_av >= n_fn,
          "address_versions=%d functions=%d" % (n_av, n_fn))
    # every kcdx_id 1..n_fn present (the function baseline).
    present = scalar(dc, "SELECT COUNT(DISTINCT kcdx_id) FROM address_versions "
                         "WHERE kcdx_id BETWEEN 1 AND ?", (n_fn,))
    check("every kcdx_id 1..N(functions) present in address_versions",
          present == n_fn, "present=%d of %d" % (present, n_fn))

    # --- 3. all baseline-open intervals + partial-unique-open ---
    n_open = scalar(dc, "SELECT COUNT(*) FROM address_versions "
                        "WHERE valid_through IS NULL")
    check("address_versions all baseline-open (valid_through IS NULL)",
          n_open == n_av, "open=%d of %d" % (n_open, n_av))
    dup_open = scalar(dc,
        "SELECT COUNT(*) FROM (SELECT kcdx_id FROM address_versions "
        "WHERE valid_through IS NULL GROUP BY kcdx_id HAVING COUNT(*) > 1)")
    check("no entity has 2 open address_versions rows", dup_open == 0,
          "entities-with-2-open=%d" % dup_open)

    # --- 4. USER address_versions has NO auto_name/decompile_quality; DEV does
    ucols = columns(uc, "address_versions")
    dcols = columns(dc, "address_versions")
    check("USER address_versions excludes auto_name + decompile_quality",
          "auto_name" not in ucols and "decompile_quality" not in ucols,
          "user cols=%s" % ucols)
    check("DEV address_versions includes auto_name + decompile_quality",
          "auto_name" in dcols and "decompile_quality" in dcols, "")

    # --- 5. address_names row count == seed.csv row count; USER excludes
    #         source/notes, DEV includes. The expected count is derived from
    #         the live seed.csv (not hardcoded) so seed additions pass without
    #         a harness edit. ---
    n_seed = len(imp.read_seed(imp.SEED_CSV))
    n_an = scalar(dc, "SELECT COUNT(*) FROM address_names")
    check("address_names row count == seed.csv row count",
          n_an == n_seed, "address_names=%d seed.csv=%d" % (n_an, n_seed))
    uacols = columns(uc, "address_names")
    dacols = columns(dc, "address_names")
    check("USER address_names excludes source + notes",
          "source" not in uacols and "notes" not in uacols,
          "user cols=%s" % uacols)
    check("DEV address_names includes source + notes",
          "source" in dacols and "notes" in dacols, "")

    # --- 6. game_versions + meta singletons ---
    gv = dc.execute("SELECT tag, ordinal FROM game_versions").fetchall()
    check("game_versions one row tag=1.5.1164953 ordinal=1164953",
          len(gv) == 1 and gv[0][0] == "1.5.1164953" and gv[0][1] == 1164953,
          "got %s" % gv)
    mt = dc.execute("SELECT schema_version FROM meta").fetchall()
    check("meta one row schema_version=1",
          len(mt) == 1 and mt[0][0] == 1, "got %s" % mt)

    # --- 7. content_hash round-trip ---
    # DEV: 0x1050 (an uncurated bulk function -- shipped only in DEV).
    dump_hash = dump_hash_for_rva(dump_dir, 0x1050)
    row = dc.execute(
        "SELECT v.content_hash FROM address_versions v WHERE v.rva = ?",
        (0x1050,)).fetchone()
    blob = row[0] if row else None
    got_hex = blob.hex() if isinstance(blob, (bytes, bytearray)) else None
    check("DEV: content_hash BLOB for rva 0x1050 round-trips to dump hex",
          dump_hash is not None and got_hex == dump_hash,
          "db=%s dump=%s" % ((got_hex or "")[:16], (dump_hash or "")[:16]))
    # USER: 0x71a5a4 (lua_pcall -- curated, present in USER).
    dump_hash_curated = dump_hash_for_rva(dump_dir, 0x71a5a4)
    urow = uc.execute(
        "SELECT v.content_hash FROM address_versions v WHERE v.rva = ?",
        (0x71a5a4,)).fetchone()
    ublob = urow[0] if urow else None
    ugot_hex = ublob.hex() if isinstance(ublob, (bytes, bytearray)) else None
    check("USER: content_hash BLOB for curated rva 0x71a5a4 (lua_pcall) round-trips",
          dump_hash_curated is not None and ugot_hex == dump_hash_curated,
          "db=%s dump=%s" % ((ugot_hex or "")[:16], (dump_hash_curated or "")[:16]))

    # --- 8. STREAMLINE: USER narrowed to curated kcdx_ids only.
    # address_names.id IS the kcdx_id (PK; one row per entity).
    curated_ids = scalar(dc, "SELECT COUNT(*) FROM address_names")
    u_av = scalar(uc, "SELECT COUNT(*) FROM address_versions")
    check("STREAMLINE: USER address_versions count == address_names count",
          u_av == curated_ids,
          "USER address_versions=%d address_names=%d" % (u_av, curated_ids))
    # Sanity: an uncurated bulk RVA (0x1050) must NOT have an address_versions row in USER.
    u_bulk_row = uc.execute(
        "SELECT 1 FROM address_versions WHERE rva = ?", (0x1050,)).fetchone()
    check("STREAMLINE: USER does NOT contain bulk function rva 0x1050",
          u_bulk_row is None, "found a row" if u_bulk_row else "")

    # --- 9. End-to-end resolution: a curated name resolves to a single row
    #        with kind+rva+signature (the path a plugin's target = "..." takes).
    nm = "IConsole_GetCVar"
    row = uc.execute("""
        SELECT n.id, v.rva, v.signature
          FROM address_names n
          JOIN address_versions v ON v.kcdx_id = n.id
                                  AND v.valid_through IS NULL
         WHERE n.name = ?""", (nm,)).fetchone()
    check("USER end-to-end: name=%r resolves to address+signature" % nm,
          row is not None and row[1] is not None and row[2],
          "got %s" % (row,))

    # --- 10. FK sanity: every address_names.id has at least one address_versions row.
    orphan_an = scalar(dc, """
        SELECT COUNT(*) FROM address_names n
        LEFT JOIN (SELECT DISTINCT kcdx_id FROM address_versions) v
          ON v.kcdx_id = n.id
        WHERE v.kcdx_id IS NULL""")
    check("every address_names.id has >=1 address_versions row",
          orphan_an == 0, "orphans=%d" % orphan_an)

    # Statements/edges FK to a kcdx_id present in DEV address_versions.
    orphan_st = scalar(dc, """
        SELECT COUNT(*) FROM statements s
        LEFT JOIN (SELECT DISTINCT kcdx_id FROM address_versions) v
          ON v.kcdx_id = s.kcdx_id
        WHERE v.kcdx_id IS NULL""")
    check("every statements.kcdx_id (DEV) resolves to an address_versions row",
          orphan_st == 0, "orphans=%d" % orphan_st)

    uc.close()
    dc.close()


def main():
    dump_dir = sys.argv[1] if len(sys.argv) > 1 else DEFAULT_DUMP
    if not os.path.isdir(dump_dir):
        sys.exit("dump dir not found: " + dump_dir)

    scratch = tempfile.mkdtemp(prefix="kcdx-dbshape-validate-")
    try:
        print("==> building both DBs from %s ..." % dump_dir, flush=True)
        # Drive the real importer's REBUILD path directly (CLI-independent).
        imp.run_rebuild(dump_dir, scratch)
        user_db = os.path.join(scratch, "reference.sqlite")
        dev_db = os.path.join(scratch, "reference-dev.sqlite")

        print("\n" + "=" * 70, flush=True)
        print("CHECKS", flush=True)
        print("=" * 70, flush=True)
        run_checks(dump_dir, user_db, dev_db)
    finally:
        shutil.rmtree(scratch, ignore_errors=True)
        shutil.rmtree(os.path.join(HERE, "__pycache__"), ignore_errors=True)

    passed = sum(1 for _, ok, _ in _results if ok)
    total = len(_results)
    print("-" * 70, flush=True)
    print("VERDICT: %d/%d checks PASS" % (passed, total), flush=True)
    if passed == total:
        print("RESULT: PASS -- DB shape matches the locked schema.", flush=True)
        sys.exit(0)
    else:
        for name, ok, detail in _results:
            if not ok:
                print("  FAILED: %s -- %s" % (name, detail), flush=True)
        print("RESULT: FAIL -- %d/%d." % (passed, total), flush=True)
        sys.exit(1)


if __name__ == "__main__":
    main()
