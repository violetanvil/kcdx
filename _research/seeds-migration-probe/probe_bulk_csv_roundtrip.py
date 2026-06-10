#!/usr/bin/env python3
"""PROBE (seeds-to-tracked-csv-migration P0.1): does the DEV bulk corpus round-trip
DB -> CSV -> DB byte-identical, with NO Ghidra dump?

The D38-gating unknown. The dump is TODAY the sole source of the bulk corpus
(build_rows reads functions/statements/referenced_vars/call_edges via
iter_table(dump_dir) and DERIVES address_version_id from function_rva via the
rva_to_av_id map built in that same pass). Once built, the DEV DB stores the
RESOLVED FK integers (address_version_id, caller/callee_address_version_id, the id
PKs), not the raw rvas. So a DB->CSV->DB round-trip is lossless IFF the export
preserves every column (incl. the id/FK integers + the BLOB content_hash) and the
rebuild inserts them VERBATIM (no AUTOINCREMENT renumber, no re-derivation).

This probe verifies exactly that, over the bulk tables + the kcdx_id-NULL bulk
address_versions rows. Outcome -> meaning (pre-committed, theory-independent):
  - byte/value-identical for every bulk table (row count AND every column) ->
    the DB tables capture the full bulk; the export CAN be lossless from the DB
    alone -> D38 buildable, Phase 1 proceeds.
  - ANY divergence (a missing row, a renumbered id, a dropped/changed column) ->
    the export-then-rebuild loses data -> STOP+surface the exact table/column
    (the export must capture it, or the rebuild must preserve it verbatim) before
    Phase 1 builds on the premise.

Scratch verification artifact (working-artifacts): reads the live
data/reference-dev.sqlite, writes a temp CSV bundle + a temp rebuilt DB, diffs,
prints the verdict. Touches NO production code. Run from the repo root:
  python _research/seeds-migration-probe/probe_bulk_csv_roundtrip.py
"""
import csv
import os
import sqlite3
import sys
import tempfile

REPO = os.getcwd()
SRC = os.path.join(REPO, "data", "reference-dev.sqlite")

# The bulk-relevant tables. We round-trip each in full. address_versions is
# included WHOLE (curated + bulk) so the kcdx_id-NULL discovery rows are covered;
# the curated 157 are a free superset check.
TABLES = ["address_versions", "statements", "referenced_vars", "call_edges"]


def table_columns(con, table):
    return [r[1] for r in con.execute(f"PRAGMA table_info({table})").fetchall()]


def table_create_sql(con, table):
    row = con.execute(
        "SELECT sql FROM sqlite_master WHERE type='table' AND name=?", (table,)
    ).fetchone()
    return row[0] if row else None


def cell_to_csv(v):
    """Encode one cell for CSV, distinguishing NULL from '' and round-tripping BLOB.
    NULL -> the sentinel \\N (so an empty TEXT '' is preserved distinct from NULL).
    BLOB -> hex with a 'blob:' prefix. int/float/str -> str."""
    if v is None:
        return r"\N"
    if isinstance(v, (bytes, bytearray)):
        return "blob:" + bytes(v).hex()
    return str(v)


def csv_to_cell(s, decl_type):
    if s == r"\N":
        return None
    if s.startswith("blob:"):
        return bytes.fromhex(s[5:])
    t = (decl_type or "").upper()
    if "INT" in t:
        return int(s) if s != "" else None
    if t in ("REAL", "FLOA", "DOUB"):
        return float(s) if s != "" else None
    return s  # TEXT (incl. the empty string, preserved)


def export_table(con, table, out_dir):
    cols = table_columns(con, table)
    path = os.path.join(out_dir, f"{table}.csv")
    n = 0
    with open(path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for row in con.execute(f"SELECT {', '.join(cols)} FROM {table}"):
            w.writerow([cell_to_csv(v) for v in row])
            n += 1
    return path, cols, n


def rebuild_table(dst_con, src_con, table, csv_path):
    # Recreate the table with the EXACT original CREATE TABLE sql (preserves PK /
    # AUTOINCREMENT / types), then INSERT every column VERBATIM (incl. the id PK)
    # so no AUTOINCREMENT renumber can occur.
    dst_con.execute(table_create_sql(src_con, table))
    decl = {r[1]: r[2] for r in src_con.execute(f"PRAGMA table_info({table})").fetchall()}
    with open(csv_path, newline="", encoding="utf-8") as f:
        r = csv.reader(f)
        cols = next(r)
        ph = ", ".join("?" for _ in cols)
        ins = f"INSERT INTO {table} ({', '.join(cols)}) VALUES ({ph})"
        batch = []
        for line in r:
            batch.append([csv_to_cell(line[i], decl[cols[i]]) for i in range(len(cols))])
            if len(batch) >= 10000:
                dst_con.executemany(ins, batch)
                batch.clear()
        if batch:
            dst_con.executemany(ins, batch)
    dst_con.commit()


def diff_table(src_con, dst_con, table):
    """Full equality: row count + every row's every column. Streams ordered by the
    id PK so the compare is O(N) and order-stable. Returns (ok, detail)."""
    cols = table_columns(src_con, table)
    order = "id" if "id" in cols else cols[0]
    sel = f"SELECT {', '.join(cols)} FROM {table} ORDER BY {order}"
    ns = src_con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
    nd = dst_con.execute(f"SELECT COUNT(*) FROM {table}").fetchone()[0]
    if ns != nd:
        return False, f"row count {ns} (orig) != {nd} (rebuilt)"
    a = src_con.execute(sel)
    b = dst_con.execute(sel)
    i = 0
    while True:
        ra = a.fetchone()
        rb = b.fetchone()
        if ra is None and rb is None:
            break
        if ra != rb:
            # find the first differing column for a precise message
            for ci, cn in enumerate(cols):
                if ra[ci] != rb[ci]:
                    return False, (f"row {i} col '{cn}': orig={ra[ci]!r} != "
                                   f"rebuilt={rb[ci]!r}")
            return False, f"row {i}: tuple differs ({ra!r} != {rb!r})"
        i += 1
    return True, f"{ns} rows, all {len(cols)} columns identical"


def main():
    if not os.path.exists(SRC):
        print(f"FAIL: {SRC} absent — the probe needs the live DEV DB.")
        return 2
    src = sqlite3.connect(SRC)
    tmp = tempfile.mkdtemp(prefix="bulk_roundtrip_")
    csv_dir = os.path.join(tmp, "csv")
    os.makedirs(csv_dir)
    dst_path = os.path.join(tmp, "rebuilt-dev.sqlite")
    dst = sqlite3.connect(dst_path)

    print(f"PROBE bulk CSV round-trip — src={SRC}")
    print(f"  scratch: {tmp}")
    all_ok = True
    for t in TABLES:
        _, _, n = export_table(src, t, csv_dir)
        rebuild_table(dst, src, t, os.path.join(csv_dir, f"{t}.csv"))
        ok, detail = diff_table(src, dst, t)
        mark = "OK  " if ok else "FAIL"
        print(f"  [{mark}] {t}: {detail}")
        all_ok = all_ok and ok

    src.close()
    dst.close()
    print()
    if all_ok:
        print("VERDICT: PASS — every bulk table round-trips DB->CSV->DB byte-identical "
              "(row count + every column, incl. the id/FK integers + BLOB content_hash). "
              "The DEV DB tables capture the full bulk; the export can be lossless from "
              "the DB alone with NO dump. -> D38 buildable; Phase 1 proceeds.")
        return 0
    print("VERDICT: FAIL — a bulk table did NOT round-trip (see the FAIL line above). "
          "The export-then-rebuild loses/changes data -> STOP and surface: the export "
          "must capture the divergent column, or the rebuild must preserve it verbatim, "
          "before Phase 1 builds on the rebuild-from-CSV premise.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
