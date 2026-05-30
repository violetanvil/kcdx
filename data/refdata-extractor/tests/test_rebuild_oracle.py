"""test_rebuild_oracle.py -- the behaviour-preserving gate for the seeds_shared
extraction (db-updator Phase 1, step 1).

WHAT THIS PROVES
----------------
The seeds_shared/ extraction is a behaviour-preserving refactor: the rebuild
path's output (both DBs, every table, including the _dict_* lookup tables) must
be BYTE-IDENTICAL pre- and post-refactor. This test rebuilds reference.sqlite +
reference-dev.sqlite from the local dump at
  data/refdata-extractor/dump/refdata-1.5.1164953
into a temp out_dir, then asserts a per-table content hash + row count against a
recorded baseline snapshot (tests/oracle_baseline.json).

The baseline was captured from the PRE-refactor code (run with --capture once,
before the extraction). After the extraction, this test must pass unchanged
against that same baseline -- that is the proof the row sets did not drift.

The hash is over the ORDERED rows of each table exactly as the importer emits
them (the importer fixes row order: address_versions by id, dump-driven tables
in dump-iteration order, dict tables in first-seen order). Order is part of the
contract, so we do NOT sort -- a re-ordering would itself be a behaviour change.
Every cell is canonicalized (BLOB -> hex, None -> a sentinel) so the hash is
stable across the sqlite driver's Python type mapping.

RUN
---
    # one-time baseline capture from the current code (DESTRUCTIVE: overwrites
    # tests/oracle_baseline.json):
    python tests/test_rebuild_oracle.py --capture

    # the gate (used by pytest or run directly):
    python tests/test_rebuild_oracle.py
    pytest tests/test_rebuild_oracle.py

The real dump is ~321K functions; one rebuild takes tens of seconds. That is
expected.
"""
import hashlib
import json
import os
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "..", "dump", "refdata-1.5.1164953"))
BASELINE_PATH = os.path.join(HERE, "oracle_baseline.json")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402


def _canon(v):
    """Stable string form of one cell, independent of the sqlite driver's
    Python type choices. BLOB -> 'b:<hex>'; None -> a NULL sentinel; everything
    else -> repr-ish text with a type tag so 1 (int) and '1' (text) differ."""
    if v is None:
        return "\x00NULL\x00"
    if isinstance(v, (bytes, bytearray, memoryview)):
        return "b:" + bytes(v).hex()
    if isinstance(v, int):
        return "i:" + str(v)
    if isinstance(v, float):
        return "f:" + repr(v)
    return "t:" + str(v)


def _table_names(con):
    rows = con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name"
    ).fetchall()
    return [r[0] for r in rows]


def _hash_db(db_path):
    """Return {table_name: {"count": N, "hash": sha256hex}} for every table in
    the db, INCLUDING the _dict_* lookup tables. Rows are read in the table's
    natural rowid order (the order the importer inserted them); columns in
    declared order. No sorting -- order is part of the behaviour contract."""
    con = sqlite3.connect(db_path)
    try:
        out = {}
        for t in _table_names(con):
            cols = [c[1] for c in con.execute(
                f'PRAGMA table_info("{t}")').fetchall()]
            h = hashlib.sha256()
            n = 0
            cur = con.execute(
                f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} FROM "{t}"')
            for row in cur:
                h.update(("\x1e".join(_canon(c) for c in row) + "\x1d").encode(
                    "utf-8", "surrogatepass"))
                n += 1
            out[t] = {"count": n, "hash": h.hexdigest()}
        return out
    finally:
        con.close()


def rebuild_and_snapshot():
    """Run the importer's REBUILD path against the local dump into a temp dir,
    then snapshot both DBs. Returns {"user": {...}, "dev": {...}}."""
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(
            f"dump dir not found: {DUMP_DIR}\n"
            f"  this oracle needs the local refdata-1.5.1164953 dump present.")
    tmp = tempfile.mkdtemp(prefix="rebuild_oracle_")
    try:
        imp.run_rebuild(DUMP_DIR, tmp)
        user_db = os.path.join(tmp, "reference.sqlite")
        dev_db = os.path.join(tmp, "reference-dev.sqlite")
        return {"user": _hash_db(user_db), "dev": _hash_db(dev_db)}
    finally:
        # Best-effort cleanup; leave nothing behind in the repo.
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)


def _load_baseline():
    if not os.path.isfile(BASELINE_PATH):
        raise SystemExit(
            f"no baseline at {BASELINE_PATH}\n"
            f"  capture it from the current code first:\n"
            f"    python {os.path.relpath(__file__)} --capture")
    with open(BASELINE_PATH, encoding="utf-8") as f:
        return json.load(f)


def _compare(expected, actual):
    """Return a list of human-readable mismatch strings ([] == identical)."""
    problems = []
    for db in ("user", "dev"):
        exp_tables = set(expected[db])
        act_tables = set(actual[db])
        if exp_tables != act_tables:
            problems.append(
                f"[{db}] table set differs: "
                f"missing={sorted(exp_tables - act_tables)} "
                f"extra={sorted(act_tables - exp_tables)}")
        for t in sorted(exp_tables & act_tables):
            e, a = expected[db][t], actual[db][t]
            if e["count"] != a["count"]:
                problems.append(
                    f"[{db}.{t}] row count {a['count']} != baseline {e['count']}")
            if e["hash"] != a["hash"]:
                problems.append(
                    f"[{db}.{t}] content hash differs "
                    f"(baseline {e['hash'][:12]}.., got {a['hash'][:12]}..)")
    return problems


def test_rebuild_matches_baseline():
    """The behaviour-preserving gate: a fresh rebuild reproduces the recorded
    pre-refactor per-table row counts + content hashes, for every table in both
    the USER and DEV DBs (including the _dict_* lookup tables)."""
    expected = _load_baseline()
    actual = rebuild_and_snapshot()
    problems = _compare(expected, actual)
    assert not problems, "rebuild output drifted from baseline:\n  " + \
        "\n  ".join(problems)


def _capture():
    snap = rebuild_and_snapshot()
    with open(BASELINE_PATH, "w", encoding="utf-8") as f:
        json.dump(snap, f, indent=2, sort_keys=True)
    print(f"\nbaseline written -> {BASELINE_PATH}")
    for db in ("user", "dev"):
        print(f"\n== {db} db ==")
        for t in sorted(snap[db]):
            print(f"  {t:24s} count={snap[db][t]['count']:>8d} "
                  f"hash={snap[db][t]['hash']}")


def _verify():
    expected = _load_baseline()
    actual = rebuild_and_snapshot()
    problems = _compare(expected, actual)
    if problems:
        print("\nFAIL: rebuild output drifted from baseline:")
        for p in problems:
            print("  " + p)
        sys.exit(1)
    print("\nPASS: rebuild output is byte-identical to the recorded baseline.")
    for db in ("user", "dev"):
        print(f"  {db}: {len(actual[db])} tables, all hashes match")


if __name__ == "__main__":
    if "--capture" in sys.argv[1:]:
        _capture()
    else:
        _verify()
