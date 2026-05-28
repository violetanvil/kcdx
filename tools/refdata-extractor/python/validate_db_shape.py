"""validate_db_shape.py -- THE DB-SHAPE GATE for the flattened address-name/
address-version schema (2026-05-28).

Builds BOTH reference DBs (USER + DEV) from a dump dir via import_to_sqlite.py,
then asserts the locked schema's shape against falsifiable answers: table
presence per db, the kcdx_id baseline count, the all-open baseline intervals,
the partial-unique-open-interval invariant, the USER/DEV column projection, the
address_names seeding count (from address_names_seed.csv), modules registry
from module_seed.csv, every address_names.id matches an address_names_seed.csv
id, every address_versions baseline-version row matches an
address_versions_seed.csv (kcdx_id, valid_from_version) tuple, every
address_versions.module_id resolves, content_hash BLOB round-trip, FK
resolution.

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
    # every dump-function id 1..n_fn must be a row in address_versions.id (the
    # universal "which function row" handle assigned 1..N in rva order; not
    # kcdx_id, which is curated-only now).
    present = scalar(dc, "SELECT COUNT(*) FROM address_versions WHERE id BETWEEN 1 AND ?",
                     (n_fn,))
    check("every address_versions.id 1..N(functions) present",
          present == n_fn, "present=%d of %d" % (present, n_fn))

    # --- 3. all baseline-open intervals + partial-unique-open (curated only) ---
    n_open = scalar(dc, "SELECT COUNT(*) FROM address_versions "
                        "WHERE valid_through IS NULL")
    check("address_versions all baseline-open (valid_through IS NULL)",
          n_open == n_av, "open=%d of %d" % (n_open, n_av))
    # Partial-unique enforces "at most one open row per CURATED entity" (kcdx_id
    # IS NOT NULL); bulk rows have kcdx_id NULL and don't participate.
    dup_open = scalar(dc,
        "SELECT COUNT(*) FROM (SELECT kcdx_id FROM address_versions "
        "WHERE kcdx_id IS NOT NULL AND valid_through IS NULL "
        "GROUP BY kcdx_id HAVING COUNT(*) > 1)")
    check("no curated entity has 2 open address_versions rows", dup_open == 0,
          "curated-entities-with-2-open=%d" % dup_open)

    # --- 3b. NEW: kcdx_id is nullable; bulk rows = NULL, curated = NOT NULL.
    n_bulk = scalar(dc, "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NULL")
    n_cur  = scalar(dc, "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL")
    names_seed_rows    = imp.read_address_names_seed(imp.ADDRESS_NAMES_SEED_CSV)
    versions_seed_rows = imp.read_address_versions_seed(imp.ADDRESS_VERSIONS_SEED_CSV)
    # Only versions-seed rows for the baseline import version are materialized
    # by the importer today; the baseline curated count == that subset's count.
    baseline_versions = [v for v in versions_seed_rows
                         if v["valid_from_version"].strip() == imp.GAME_VERSION_TAG]
    n_baseline = len(baseline_versions)
    check("DEV address_versions: curated count == address_versions_seed.csv baseline count",
          n_cur == n_baseline,
          "curated=%d baseline-seed=%d" % (n_cur, n_baseline))
    check("DEV address_versions: bulk count == n_av - curated",
          n_bulk == (n_av - n_cur),
          "bulk=%d expected=%d" % (n_bulk, n_av - n_cur))

    # --- 4. USER address_versions has NO auto_name/decompile_quality; DEV does
    ucols = columns(uc, "address_versions")
    dcols = columns(dc, "address_versions")
    check("USER address_versions excludes auto_name + decompile_quality",
          "auto_name" not in ucols and "decompile_quality" not in ucols,
          "user cols=%s" % ucols)
    check("DEV address_versions includes auto_name + decompile_quality",
          "auto_name" in dcols and "decompile_quality" in dcols, "")

    # --- 5. address_names row count == address_names_seed.csv row count; USER
    #         excludes source/notes, DEV includes. The expected count is derived
    #         from the live seed (not hardcoded) so seed additions pass without
    #         a harness edit. ---
    n_seed = len(names_seed_rows)
    n_an = scalar(dc, "SELECT COUNT(*) FROM address_names")
    check("address_names row count == address_names_seed.csv row count",
          n_an == n_seed,
          "address_names=%d address_names_seed.csv=%d" % (n_an, n_seed))
    # Canonical-id authority: every address_names.id matches a canonical id in
    # address_names_seed.csv ("id=address_names_seed.id, no autoincrement"
    # invariant end-to-end).
    seed_ids = sorted(int(r["id"]) for r in names_seed_rows)
    db_ids = sorted(r[0] for r in dc.execute("SELECT id FROM address_names").fetchall())
    check("address_names.id set matches address_names_seed.csv id set",
          db_ids == seed_ids,
          "extra-in-db=%s missing-in-db=%s" % (
              sorted(set(db_ids) - set(seed_ids))[:5],
              sorted(set(seed_ids) - set(db_ids))[:5]))

    # Versions-seed FK closure: every address_versions_seed.kcdx_id resolves to
    # an address_names_seed.id (the importer raises on violation, but defense-
    # in-depth -- a manual edit between rebuilds shouldn't go undetected).
    names_id_set = set(int(r["id"]) for r in names_seed_rows)
    orphan_vsk = [int(v["kcdx_id"]) for v in versions_seed_rows
                  if int(v["kcdx_id"]) not in names_id_set]
    check("every address_versions_seed.kcdx_id resolves to address_names_seed.id",
          not orphan_vsk, "orphan kcdx_ids (first 5)=%s" % (orphan_vsk[:5],))

    # Every named entity has at least one baseline-version resolve fact (the
    # importer enforces this, but the harness re-asserts it as a property of
    # the as-written-on-disk seeds).
    baseline_kids = set(int(v["kcdx_id"]) for v in baseline_versions)
    uncovered = names_id_set - baseline_kids
    check("every address_names_seed.id has a baseline address_versions_seed row",
          not uncovered, "uncovered (first 5)=%s" % (sorted(uncovered)[:5],))
    uacols = columns(uc, "address_names")
    dacols = columns(dc, "address_names")
    # `source` was DROPPED 2026-05-28 -- carries no information vs. the derived
    # status. Verify it's gone from BOTH DBs (the column was meaningless on
    # either side and should not have survived the cut).
    check("USER address_names excludes source + notes",
          "source" not in uacols and "notes" not in uacols,
          "user cols=%s" % uacols)
    check("DEV address_names excludes source (dropped 2026-05-28); includes notes",
          "source" not in dacols and "notes" in dacols,
          "dev cols=%s" % dacols)
    # Verification audit columns ship on BOTH DBs (the engine needs them on
    # USER to derive status at resolve time).
    check("USER address_versions has the verification audit trio + last_verified",
          all(c in ucols for c in ("last_verified_at_version", "verified_by",
                                    "verified_date", "evidence_kind")),
          "user cols=%s" % ucols)
    check("USER address_versions excludes legacy `status` column (dropped 2026-05-28)",
          "status" not in ucols, "found status in user cols=%s" % ucols)

    # --- 6. game_versions + meta singletons ---
    gv = dc.execute("SELECT tag, ordinal FROM game_versions").fetchall()
    check("game_versions one row tag=1.5.1164953 ordinal=1164953",
          len(gv) == 1 and gv[0][0] == "1.5.1164953" and gv[0][1] == 1164953,
          "got %s" % gv)
    mt = dc.execute("SELECT schema_version FROM meta").fetchall()
    check("meta one row schema_version=1",
          len(mt) == 1 and mt[0][0] == 1, "got %s" % mt)

    # --- 6b. modules table from module_seed.csv ---
    module_seed = imp.read_module_seed(imp.MODULE_SEED_CSV)
    # USER includes the `path` column (was added when module_seed got a path).
    umcols = columns(uc, "modules")
    check("USER modules table has (id, name, path)",
          set(["id", "name", "path"]).issubset(set(umcols)),
          "user cols=%s" % umcols)
    # Every module_seed row materialized 1:1 with the canonical id.
    db_mods = {r[0]: (r[1], r[2]) for r in dc.execute(
        "SELECT id, name, path FROM modules").fetchall()}
    seed_mods = {int(m["id"]): (m["name"].strip(), m["path"].strip()) for m in module_seed}
    check("modules table matches module_seed.csv 1:1 (id, name, path)",
          db_mods == seed_mods,
          "db=%s seed=%s" % (db_mods, seed_mods))
    # Every address_versions.module_id resolves to a modules row (FK closure).
    orphan_mod = scalar(dc, """
        SELECT COUNT(*) FROM address_versions v
        LEFT JOIN modules m ON m.id = v.module_id
        WHERE v.module_id IS NOT NULL AND m.id IS NULL""")
    check("every address_versions.module_id resolves to a modules row",
          orphan_mod == 0, "orphans=%d" % orphan_mod)

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

    # --- 10. FK sanity: every address_names.id has a curated address_versions
    #         row (kcdx_id IS NOT NULL) pointing at it.
    orphan_an = scalar(dc, """
        SELECT COUNT(*) FROM address_names n
        LEFT JOIN (SELECT DISTINCT kcdx_id FROM address_versions WHERE kcdx_id IS NOT NULL) v
          ON v.kcdx_id = n.id
        WHERE v.kcdx_id IS NULL""")
    check("every address_names.id has a curated address_versions row",
          orphan_an == 0, "orphans=%d" % orphan_an)

    # Statements/edges FK to address_versions.id (always set; the universal
    # handle that kcdx.find walks). kcdx_id on these tables is NULLABLE
    # (curated only); the address_version_id is the real FK.
    orphan_st = scalar(dc, """
        SELECT COUNT(*) FROM statements s
        LEFT JOIN address_versions v ON v.id = s.address_version_id
        WHERE v.id IS NULL""")
    check("every statements.address_version_id (DEV) resolves to an address_versions row",
          orphan_st == 0, "orphans=%d" % orphan_st)
    # A statement's kcdx_id, when set, must match its function's kcdx_id.
    mismatch_st_kcdx = scalar(dc, """
        SELECT COUNT(*) FROM statements s
        JOIN address_versions v ON v.id = s.address_version_id
        WHERE s.kcdx_id IS NOT NULL AND s.kcdx_id != v.kcdx_id""")
    check("statements.kcdx_id matches its function's address_versions.kcdx_id when set",
          mismatch_st_kcdx == 0, "mismatches=%d" % mismatch_st_kcdx)

    # --- 11. NEW: end-to-end kcdx.find walk -- a bulk statement's string_ref
    #         resolves up to its function via address_version_id.
    nm_walk = dc.execute("""
        SELECT v.rva, v.auto_name
          FROM statements s
          JOIN address_versions v ON v.id = s.address_version_id
         WHERE s.string_ref IS NOT NULL
         LIMIT 1""").fetchone()
    check("DEV kcdx.find walk: any string_ref statement resolves to its owning function",
          nm_walk is not None and nm_walk[0] is not None,
          "got %s" % (nm_walk,))

    # --- 12. SUPERSESSION + DEPRECATION integrity (USER suffices -- both DBs
    #         share the same address_names rows; the importer raises on violations
    #         so a passing build never reaches here with a bad row, but the
    #         harness verifies defense-in-depth against any future direct writer).

    # 12a. Pair integrity: superseded_by IS NULL <=> superseded_at_version IS NULL.
    bad_sup_pair = scalar(uc, """
        SELECT COUNT(*) FROM address_names
        WHERE (superseded_by IS NULL) != (superseded_at_version IS NULL)""")
    check("supersession pair integrity (superseded_by XNOR superseded_at_version)",
          bad_sup_pair == 0, "violations=%d" % bad_sup_pair)

    # 12b. Pair integrity: is_deprecated=1 <=> deprecated_at_version IS NOT NULL.
    bad_dep_pair = scalar(uc, """
        SELECT COUNT(*) FROM address_names
        WHERE (is_deprecated = 1) != (deprecated_at_version IS NOT NULL)""")
    check("deprecation pair integrity (is_deprecated XNOR deprecated_at_version)",
          bad_dep_pair == 0, "violations=%d" % bad_dep_pair)

    # 12c. deprecation_replacement requires is_deprecated=1.
    bad_repl = scalar(uc, """
        SELECT COUNT(*) FROM address_names
        WHERE deprecation_replacement IS NOT NULL AND is_deprecated != 1""")
    check("deprecation_replacement requires is_deprecated=1",
          bad_repl == 0, "violations=%d" % bad_repl)

    # 12d. FK closure: superseded_by + deprecation_replacement resolve to live ids.
    orphan_sup = scalar(uc, """
        SELECT COUNT(*) FROM address_names a
        LEFT JOIN address_names b ON b.id = a.superseded_by
        WHERE a.superseded_by IS NOT NULL AND b.id IS NULL""")
    check("every superseded_by resolves to an address_names row",
          orphan_sup == 0, "orphans=%d" % orphan_sup)
    orphan_repl = scalar(uc, """
        SELECT COUNT(*) FROM address_names a
        LEFT JOIN address_names b ON b.id = a.deprecation_replacement
        WHERE a.deprecation_replacement IS NOT NULL AND b.id IS NULL""")
    check("every deprecation_replacement resolves to an address_names row",
          orphan_repl == 0, "orphans=%d" % orphan_repl)

    # 12e. FK closure: version anchors resolve to game_versions rows.
    orphan_supv = scalar(uc, """
        SELECT COUNT(*) FROM address_names a
        LEFT JOIN game_versions g ON g.id = a.superseded_at_version
        WHERE a.superseded_at_version IS NOT NULL AND g.id IS NULL""")
    check("every superseded_at_version resolves to a game_versions row",
          orphan_supv == 0, "orphans=%d" % orphan_supv)
    orphan_depv = scalar(uc, """
        SELECT COUNT(*) FROM address_names a
        LEFT JOIN game_versions g ON g.id = a.deprecated_at_version
        WHERE a.deprecated_at_version IS NOT NULL AND g.id IS NULL""")
    check("every deprecated_at_version resolves to a game_versions row",
          orphan_depv == 0, "orphans=%d" % orphan_depv)

    # --- 13. VERIFICATION AUDIT integrity (USER + DEV; both DBs share the
    #         same address_versions audit columns).
    #
    # 13a. Trio integrity: last_verified_at_version IS NULL <=> all three of
    #      verified_by / verified_date / evidence_kind are NULL.
    bad_verif_pair = scalar(uc, """
        SELECT COUNT(*) FROM address_versions
        WHERE (last_verified_at_version IS NULL) != (
                  verified_by IS NULL
              AND verified_date IS NULL
              AND evidence_kind IS NULL)""")
    check("verification trio integrity (last_verified_at_version XNOR verified_by+verified_date+evidence_kind)",
          bad_verif_pair == 0, "violations=%d" % bad_verif_pair)

    # 13b. FK closure: last_verified_at_version resolves to game_versions.
    orphan_lvv = scalar(uc, """
        SELECT COUNT(*) FROM address_versions v
        LEFT JOIN game_versions g ON g.id = v.last_verified_at_version
        WHERE v.last_verified_at_version IS NOT NULL AND g.id IS NULL""")
    check("every last_verified_at_version resolves to a game_versions row",
          orphan_lvv == 0, "orphans=%d" % orphan_lvv)

    # 13c. last_verified_at_version.ordinal >= valid_from.ordinal (the row
    #      can't be verified at a version older than where it starts).
    bad_order = scalar(uc, """
        SELECT COUNT(*) FROM address_versions v
        JOIN game_versions gv_from ON gv_from.id = v.valid_from
        JOIN game_versions gv_last ON gv_last.id = v.last_verified_at_version
        WHERE v.last_verified_at_version IS NOT NULL
          AND gv_last.ordinal < gv_from.ordinal""")
    check("last_verified_at_version >= valid_from for every verified row",
          bad_order == 0, "violations=%d" % bad_order)

    # 13d. verified_date format check (YYYY-MM-DD).
    bad_date = scalar(uc, """
        SELECT COUNT(*) FROM address_versions
        WHERE verified_date IS NOT NULL
          AND verified_date NOT GLOB '[0-9][0-9][0-9][0-9]-[0-9][0-9]-[0-9][0-9]'""")
    check("verified_date format is YYYY-MM-DD when set",
          bad_date == 0, "malformed=%d" % bad_date)

    # 12f. Cycle detection on the superseded_by graph (version-ignorant; a
    #      cycle is wrong regardless of which versions gate which edges).
    direct = {r[0]: r[1] for r in uc.execute(
        "SELECT id, superseded_by FROM address_names").fetchall()}
    id_to_name = {r[0]: r[1] for r in uc.execute(
        "SELECT id, name FROM address_names").fetchall()}
    cycle = None
    for start in direct:
        if direct.get(start) is None:
            continue
        seen = [start]
        cur = direct[start]
        while cur is not None:
            if cur in seen:
                seen.append(cur)
                cycle = " -> ".join(id_to_name.get(i, f"#{i}") for i in seen)
                break
            seen.append(cur)
            cur = direct.get(cur)
        if cycle:
            break
    check("no cycle in superseded_by graph", cycle is None,
          "cycle=%s" % cycle if cycle else "")

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
