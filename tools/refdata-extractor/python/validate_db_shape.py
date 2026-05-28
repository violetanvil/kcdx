"""validate_db_shape.py -- THE DB-SHAPE GATE for the entity/version-schema import.

Builds BOTH reference DBs (USER + DEV) from a dump dir via import_to_sqlite.py,
then asserts the LOCKED schema's shape against falsifiable answers: table
presence per db, the kcdx_id authority count, the all-open baseline intervals,
the partial-unique-open-interval invariant, the USER/DEV column projection, the
overlay seeding count, the content_hash BLOB round-trip, the pairing trigger's
existence + its no-fire-at-baseline guarantee, and FK resolution.

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
    nine = {"modules", "game_versions", "entities", "entity_versions",
            "kcdx_overlay", "kcdx_overlay_versions", "meta",
            "statements", "referenced_vars", "call_edges"}
    check("DEV has all 10 schema tables", nine.issubset(dt),
          "missing=%s" % (nine - dt))
    six = {"modules", "game_versions", "entities", "entity_versions",
           "kcdx_overlay", "kcdx_overlay_versions", "meta"}
    check("USER has the 7 user tables", six.issubset(ut), "missing=%s" % (six - ut))
    dev_only = {"statements", "referenced_vars", "call_edges"}
    check("USER does NOT have statements/referenced_vars/call_edges",
          not (dev_only & ut), "present=%s" % (dev_only & ut))

    # --- 2. entities count == functions + 6 curated; kcdx_id 1..N present ---
    n_fn = count_dump_functions(dump_dir)
    n_ent = scalar(dc, "SELECT COUNT(*) FROM entities")
    # entities = functions + 6 curated vtable + any minted seed-code-unmapped rows.
    n_curated_vtable = scalar(
        dc, "SELECT COUNT(*) FROM entities WHERE entity_type = "
            "(SELECT id FROM _dict_entities_entity_type WHERE val='vtable_slot')")
    check("entities count == functions + 6 curated vtable (+ minted seed)",
          n_ent >= n_fn + 6 and n_curated_vtable == 6,
          "entities=%d functions=%d curated_vtable=%d" % (n_ent, n_fn, n_curated_vtable))
    # every kcdx_id 1..n_fn present (the function baseline).
    present = scalar(dc, "SELECT COUNT(*) FROM entities WHERE kcdx_id BETWEEN 1 AND ?",
                     (n_fn,))
    check("every kcdx_id 1..N(functions) present in entities",
          present == n_fn, "present=%d of %d" % (present, n_fn))

    # --- 3. entity_versions: one row per entity, all open; count match ---
    n_ev = scalar(dc, "SELECT COUNT(*) FROM entity_versions")
    n_open = scalar(dc, "SELECT COUNT(*) FROM entity_versions WHERE valid_through IS NULL")
    check("entity_versions count == entities count (one row per entity)",
          n_ev == n_ent, "ev=%d entities=%d" % (n_ev, n_ent))
    check("entity_versions all baseline-open (valid_through IS NULL)",
          n_open == n_ev, "open=%d of %d" % (n_open, n_ev))

    # --- 4. partial-unique-open-interval: no entity has 2 open ev rows ---
    dup_open = scalar(dc,
        "SELECT COUNT(*) FROM (SELECT kcdx_id FROM entity_versions "
        "WHERE valid_through IS NULL GROUP BY kcdx_id HAVING COUNT(*) > 1)")
    check("no entity has 2 open entity_versions rows", dup_open == 0,
          "entities-with-2-open=%d" % dup_open)

    # --- 5. USER ev has NO auto_name/decompile_quality; DEV does ---
    ucols = columns(uc, "entity_versions")
    dcols = columns(dc, "entity_versions")
    check("USER entity_versions excludes auto_name + decompile_quality",
          "auto_name" not in ucols and "decompile_quality" not in ucols,
          "user cols=%s" % ucols)
    check("DEV entity_versions includes auto_name + decompile_quality",
          "auto_name" in dcols and "decompile_quality" in dcols, "")

    # --- 6. kcdx_overlay == 139; USER excludes source/notes, DEV includes ---
    n_ov = scalar(dc, "SELECT COUNT(*) FROM kcdx_overlay")
    check("kcdx_overlay row count == 139 (the seed)", n_ov == 139,
          "got %d" % n_ov)
    uocols = columns(uc, "kcdx_overlay")
    docols = columns(dc, "kcdx_overlay")
    check("USER kcdx_overlay excludes source + notes",
          "source" not in uocols and "notes" not in uocols, "user cols=%s" % uocols)
    check("DEV kcdx_overlay includes source + notes",
          "source" in docols and "notes" in docols, "")

    # --- 7. kcdx_overlay_versions == 139, all open ---
    n_ovv = scalar(dc, "SELECT COUNT(*) FROM kcdx_overlay_versions")
    n_ovv_open = scalar(dc, "SELECT COUNT(*) FROM kcdx_overlay_versions WHERE valid_through IS NULL")
    check("kcdx_overlay_versions row count == 139", n_ovv == 139, "got %d" % n_ovv)
    check("kcdx_overlay_versions all open (valid_through IS NULL)",
          n_ovv_open == n_ovv, "open=%d of %d" % (n_ovv_open, n_ovv))

    # --- 8. game_versions + meta singletons ---
    gv = dc.execute("SELECT tag, ordinal FROM game_versions").fetchall()
    check("game_versions one row tag=1.5.1164953 ordinal=1164953",
          len(gv) == 1 and gv[0][0] == "1.5.1164953" and gv[0][1] == 1164953,
          "got %s" % gv)
    mt = dc.execute("SELECT schema_version FROM meta").fetchall()
    check("meta one row schema_version=1",
          len(mt) == 1 and mt[0][0] == 1, "got %s" % mt)

    # --- 9. content_hash round-trip for a known function (rva 0x1050) ---
    dump_hash = dump_hash_for_rva(dump_dir, 0x1050)
    row = dc.execute(
        "SELECT ev.content_hash FROM entity_versions ev WHERE ev.rva = ?",
        (0x1050,)).fetchone()
    blob = row[0] if row else None
    got_hex = blob.hex() if isinstance(blob, (bytes, bytearray)) else None
    check("content_hash BLOB for rva 0x1050 round-trips to dump hex",
          dump_hash is not None and got_hex == dump_hash,
          "db=%s dump=%s" % ((got_hex or "")[:16], (dump_hash or "")[:16]))

    # --- 10. trigger exists in BOTH dbs ---
    check("trg_pair_overlay_version exists in DEV",
          "trg_pair_overlay_version" in triggers(dc), "")
    check("trg_pair_overlay_version exists in USER",
          "trg_pair_overlay_version" in triggers(uc), "")

    # --- 11. no duplicate overlay_versions (trigger did NOT fire at baseline) ---
    # one overlay_versions row per overlay at baseline; >139 would mean the
    # trigger forked extra rows.
    check("no extra kcdx_overlay_versions rows (trigger silent at baseline)",
          n_ovv == n_ov, "ovv=%d overlay=%d" % (n_ovv, n_ov))
    dup_ovv = scalar(dc,
        "SELECT COUNT(*) FROM (SELECT overlay_id FROM kcdx_overlay_versions "
        "GROUP BY overlay_id HAVING COUNT(*) > 1)")
    check("no overlay has 2 kcdx_overlay_versions rows", dup_ovv == 0,
          "overlays-with-2=%d" % dup_ovv)

    # --- 12. FK sanity: ev.kcdx_id, overlay.kcdx_id, statements.kcdx_id resolve ---
    orphan_ev = scalar(dc,
        "SELECT COUNT(*) FROM entity_versions ev "
        "LEFT JOIN entities e ON e.kcdx_id = ev.kcdx_id WHERE e.kcdx_id IS NULL")
    check("every entity_versions.kcdx_id resolves to an entities row",
          orphan_ev == 0, "orphans=%d" % orphan_ev)
    orphan_ov = scalar(dc,
        "SELECT COUNT(*) FROM kcdx_overlay o "
        "LEFT JOIN entities e ON e.kcdx_id = o.kcdx_id WHERE e.kcdx_id IS NULL")
    check("every kcdx_overlay.kcdx_id resolves to an entities row",
          orphan_ov == 0, "orphans=%d" % orphan_ov)
    orphan_st = scalar(dc,
        "SELECT COUNT(*) FROM statements s "
        "LEFT JOIN entities e ON e.kcdx_id = s.kcdx_id WHERE e.kcdx_id IS NULL")
    check("every statements.kcdx_id (DEV) resolves to an entities row",
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
