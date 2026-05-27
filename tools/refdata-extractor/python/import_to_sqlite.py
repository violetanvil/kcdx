"""import_to_sqlite.py -- the maintainer-side import: CSV-per-table dump dirs ->
two encoded SQLite reference DBs (parallel-ghidra-research.md §8 step 3b).

PRODUCES TWO artifacts from one full-dump dir:
  - USER DB  (data/reference.sqlite, ships in every kcdx release): the tables a
    mod USER needs at runtime -- functions + signatures + caller_reg_args. Powers
    the per-launch cross-version SURVIVAL CHECK (function content_hash) + the ABI
    floor for hooked functions. ~tens of MB; ships uncompressed inside the release
    zip (the zip's deflate handles download size; the engine opens the plain
    .sqlite, no decompression step).
  - DEV DB   (the author/on-demand download): the FULL set incl. statements +
    referenced_vars + call_edges -- the discovery/inspection surface
    (kcdx.find, kcdx_dev_inspect). Mod AUTHORS fetch it; not in the user ship.

  (call_edges is DEV-only: it powers kcdx.find's caller-graph ranking, an author
  discovery feature -- a mod user's installed plugin already knows its target.)

ENCODING (lossless; shrinks the on-disk file, not just the compressed download):
  - content_hash 64-hex TEXT  -> 32-byte BLOB         (halves every hash column)
  - low-cardinality repetitive TEXT (storage_kind, kind, data_type, edge_reason,
    signature_source, ...) -> small INTEGER FK into a per-(table,column) `_dict_*`
    lookup table  (kills the repetitive-key bloat)
  - hex/decimal address + count columns -> INTEGER     (rva, byte_range_*, idx,
    callee_rva, sizes, counts)
  Everything else stays TEXT. VACUUM at the end. Stock SQLite, no extension.

IDs: this first cut imports the dump as-is (the dump keys rows by rva). The
append-only STABLE-ID assignment (functions.id matched across game versions by
name+signature+caller-graph fingerprint, restructure-plan §9.1) is a SEPARATE
maintainer step layered on top -- NOT done here (a single-version import has no
prior IDs to match against; IDs get assigned when a second game version arrives).
A `--assign-ids` follow-up will add the id column + the cross-version matcher.

Run:
  python import_to_sqlite.py <dump_dir> <out_dir>
    -> <out_dir>/reference.sqlite       (USER)
       <out_dir>/reference-dev.sqlite   (DEV / full)
"""
import csv
import glob
import os
import sqlite3
import sys
import time

# ---- per-table column encoding policy ------------------------------------
# Columns to DICTIONARY-encode (low-cardinality repetitive text -> INTEGER FK).
DICT_COLS = {
    "functions":       ["signature_source", "decompile_quality", "module", "game_version"],
    "statements":      ["kind"],
    "referenced_vars": ["storage_kind", "data_type", "storage_detail"],
    "call_edges":      ["edge_reason", "module", "game_version"],
    "signatures":      ["signature_source", "abi_confidence", "module", "game_version"],
    "caller_reg_args": ["agreement", "edge_reason", "module", "game_version"],
}
# Columns that are content_hash (64-hex TEXT -> 32-byte BLOB).
def is_hash_col(c):
    return "content_hash" in c
# Columns that are numeric (hex "0x..." or decimal) -> INTEGER.
INT_COLS = {
    "rva", "length", "byte_range_start", "byte_range_len", "idx", "statement_idx",
    "callee_rva", "callsite_rva", "caller_rva", "size_bytes",
    "caller_reg_arg_count", "caller_count", "observed_arg_slots",
}

USER_TABLES = ["functions", "signatures", "caller_reg_args"]
DEV_TABLES = ["functions", "statements", "referenced_vars", "call_edges",
              "signatures", "caller_reg_args"]


def parse_int(v):
    if v is None or v == "":
        return None
    try:
        return int(v, 16) if v.startswith("0x") else int(v)
    except ValueError:
        return None


def build_db(db_path, dump_dir, tables):
    """Build one encoded SQLite from the named tables of the dump. Returns
    (per_table_rowcount dict, dict_entry_count)."""
    if os.path.exists(db_path):
        os.remove(db_path)
    con = sqlite3.connect(db_path)
    con.executescript("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA page_size=4096;")
    dicts = {}            # (table, col) -> { value: int_id }
    rowcounts = {}

    for t in tables:
        shards = sorted(glob.glob(os.path.join(dump_dir, t, f"{t}_*.csv")))
        if not shards:
            print(f"  WARNING: no shards for table '{t}' in {dump_dir}", flush=True)
            continue
        # Read the header from the first shard; all shards share it.
        with open(shards[0], newline="", encoding="utf-8", errors="replace") as f:
            hdr = next(csv.reader(f))
        dcols = {i for i, c in enumerate(hdr) if c in DICT_COLS.get(t, [])}
        hcols = {i for i, c in enumerate(hdr) if is_hash_col(c)}
        icols = {i for i, c in enumerate(hdr) if c in INT_COLS}

        def decl(i, c):
            if i in dcols or i in icols:
                return f'"{c}" INTEGER'
            if i in hcols:
                return f'"{c}" BLOB'
            return f'"{c}" TEXT'
        con.execute(f'CREATE TABLE "{t}" ({",".join(decl(i, c) for i, c in enumerate(hdr))})')
        qs = ",".join("?" * len(hdr))
        ins = f'INSERT INTO "{t}" VALUES ({qs})'

        n = 0
        batch = []
        for shard in shards:
            with open(shard, newline="", encoding="utf-8", errors="replace") as f:
                rd = csv.reader(f)
                next(rd)  # header
                for row in rd:
                    if len(row) != len(hdr):
                        row = (row + [""] * len(hdr))[:len(hdr)]
                    else:
                        row = list(row)
                    for i in dcols:
                        d = dicts.setdefault((t, hdr[i]), {})
                        row[i] = d.setdefault(row[i], len(d))
                    for i in hcols:
                        v = row[i]
                        row[i] = bytes.fromhex(v) if (isinstance(v, str) and len(v) == 64) else (None if v == "" else v)
                    for i in icols:
                        row[i] = parse_int(row[i])
                    batch.append(row)
                    n += 1
                    if len(batch) >= 20000:
                        con.executemany(ins, batch)
                        batch = []
                        if n % 200000 == 0:
                            print(f"    {t}: {n} rows...", flush=True)
        if batch:
            con.executemany(ins, batch)
        rowcounts[t] = n
        print(f"  {t}: {n} rows imported", flush=True)

    # Materialize the dict lookup tables.
    dict_entries = 0
    for (t, c), d in dicts.items():
        con.execute(f'CREATE TABLE "_dict_{t}_{c}" (id INTEGER PRIMARY KEY, val TEXT)')
        con.executemany(f'INSERT INTO "_dict_{t}_{c}" VALUES (?,?)',
                        [(i, v) for v, i in d.items()])
        dict_entries += len(d)

    # Indexes the engine queries (functions survival lookup + statement joins).
    if "functions" in tables:
        con.execute('CREATE INDEX ix_fn_rva ON functions(rva)')
    if "statements" in tables:
        con.execute('CREATE INDEX ix_st_fn ON statements(function_rva, idx)')
    if "call_edges" in tables:
        con.execute('CREATE INDEX ix_ce_caller ON call_edges(caller_rva)')
        con.execute('CREATE INDEX ix_ce_callee ON call_edges(callee_rva)')

    con.commit()
    con.execute("VACUUM")
    con.close()
    return rowcounts, dict_entries


def main():
    if len(sys.argv) < 3:
        print("usage: python import_to_sqlite.py <dump_dir> <out_dir>")
        sys.exit(2)
    dump_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    bar = "=" * 70
    print(bar)
    print(f"[import_to_sqlite] dump: {dump_dir}")
    print(bar)

    print(f"\n== USER DB ({', '.join(USER_TABLES)}) -> {user_db}")
    t0 = time.time()
    urc, ud = build_db(user_db, dump_dir, USER_TABLES)
    usz = os.path.getsize(user_db)
    print(f"  built in {time.time()-t0:.0f}s; size {usz/1e6:.1f} MB; dict entries {ud}")

    print(f"\n== DEV DB ({', '.join(DEV_TABLES)}) -> {dev_db}")
    t0 = time.time()
    drc, dd = build_db(dev_db, dump_dir, DEV_TABLES)
    dsz = os.path.getsize(dev_db)
    print(f"  built in {time.time()-t0:.0f}s; size {dsz/1e6:.1f} MB; dict entries {dd}")

    print(bar)
    print("SUMMARY")
    print(f"  USER reference.sqlite     : {usz/1e6:8.1f} MB  ({sum(urc.values())} rows)")
    print(f"  DEV  reference-dev.sqlite : {dsz/1e6:8.1f} MB  ({sum(drc.values())} rows)")
    print(bar)
    print("NOTE: stable-ID assignment (functions.id matched across game versions)")
    print("is a separate maintainer step, NOT done here (single-version import).")


if __name__ == "__main__":
    main()
