"""test_curated_statements_projection.py -- the DB-shape gate for shipping the
curated statement metadata into the USER reference.sqlite projection
(statement-resolution-layer Phase 1, step 1).

WHAT THIS PROVES
----------------
The USER projection of reference.sqlite now carries `statements` +
`referenced_vars` for the CURATED-function subset only (rows whose
address_version_id belongs to a curated entity -- kcdx_id IS NOT NULL), with the
PINNED runtime column contract:

  statements      -> id, address_version_id, idx, kind, callee, string_ref,
                     byte_range_start, byte_range_len, pseudo_text
                     (DROPS content_hash + kcdx_id; KEEPS pseudo_text -- it is
                     the sole backing of the return_value(v) +
                     matching{condition_contains=} runtime locator forms)
  referenced_vars -> id, address_version_id, statement_idx, var_name,
                     storage_kind, storage_detail, size_bytes, data_type
                     (DROPS kcdx_id)

The 5.24M-row bulk `statements` and all of `call_edges` stay DEV-only. The
column contract is the cross-lane invariant the engine-side refdb API (step 2)
reads against; a deviation (a stray column, a MISSING pseudo_text, the bulk
leaking into USER, call_edges present) is a contract FAIL caught here.

Spec authority:
  docs/outstanding-work/statement-resolution-layer/HANDOFF-db-curated-statements.md
    §3 (the pinned columns) + §5 (the acceptance contract)
  docs/outstanding-work/statement-resolution-layer/plan-spec.md
    §"The column contract"

THE STRONGEST FORM -- SET-EQUALITY (on the semantic payload)
-----------------------------------------------------------
The load-bearing assertion is SET-EQUALITY (handoff §5): the USER `statements`
row set == { DEV statements WHERE address_version_id in the curated av-id set },
projected to the pinned columns. Likewise for `referenced_vars`. If set-equality
holds, every count below holds BY CONSTRUCTION (the curated subset count, the
133-function coverage, "the bulk did not leak"). The counts are also asserted
explicitly so a single broken assertion names the specific drift.

The set-equality compares the SEMANTIC PAYLOAD -- the pinned columns EXCEPT the
autoincrement `id`. `id` is an `INTEGER PRIMARY KEY AUTOINCREMENT` internal row
handle (schema.py), NOT a stable cross-DB key: the curated-only USER insert
renumbers it 1..N, while DEV carries the original sparse bulk ids, so a row-set
comparison that included `id` would never match (and should not -- the engine
joins via address_version_id, never `id`; handoff §3). `id` is instead asserted
separately as a present, unique, contiguous PK in the USER projection. Every
other pinned column (the actual resolution data) must be set-equal to the DEV
curated subset row-for-row.

The curated row counts are NOT frozen literals -- they are COMPUTED from the same
rebuilt DEV tables (the curated-address_version_id subset), so they track the
dump. The handoff measured ~2,385 statements / ~5,595 referenced_vars; this is a
sanity sense-check, not the pinned expectation.

ACCEPTANCE SIGNAL
-----------------
This is a headless DB-shape assertion (rebuild + a sqlite check; no engine, no
game launch). It emits the canonical acceptance signal
(.claude/rules/acceptance-signal.md) -- ACCEPT-RESULT / ACCEPT-SUITE -- to the
test's stdout (the data-core's DB-pipeline test sink), so the agent reads one
greppable result line set rather than the whole log.

RUN
---
    python -m pytest data/refdata-extractor/tests/test_curated_statements_projection.py -q
    python data/refdata-extractor/tests/test_curated_statements_projection.py

The rebuild is the full ~321K-function / 5.24M-statement dump; one rebuild takes
~a minute. That is expected (same cost as test_rebuild_oracle).
"""
import os
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "..", "dump", "refdata-1.5.1164953"))

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import USER_COLUMNS  # noqa: E402

# The pinned USER column contract (handoff §3). Asserted to equal what
# USER_COLUMNS declares AND what the rebuilt DB materializes -- so a drift in the
# schema declaration OR the build is caught here, not only against a literal.
PINNED_STATEMENTS_COLUMNS = [
    "id", "address_version_id", "idx", "kind", "callee", "string_ref",
    "byte_range_start", "byte_range_len", "pseudo_text",
]
PINNED_REFERENCED_VARS_COLUMNS = [
    "id", "address_version_id", "statement_idx", "var_name", "storage_kind",
    "storage_detail", "size_bytes", "data_type",
]


def _rebuild_user_db():
    """Run the importer's REBUILD path against the local dump into a temp dir,
    returning the USER + DEV db paths and the temp dir to clean up. The caller
    cleans up (the DEV db is ~1.3 GB)."""
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(
            f"dump dir not found: {DUMP_DIR}\n"
            f"  this gate needs the local refdata-1.5.1164953 dump present.")
    tmp = tempfile.mkdtemp(prefix="curated_stmt_proj_")
    imp.run_rebuild(DUMP_DIR, tmp)
    return (os.path.join(tmp, "reference.sqlite"),
            os.path.join(tmp, "reference-dev.sqlite"), tmp)


def _columns(con, table):
    return [r[1] for r in con.execute(f'PRAGMA table_info("{table}")')]


def _has_table(con, table):
    return con.execute(
        "SELECT 1 FROM sqlite_master WHERE type='table' AND name=?",
        (table,)).fetchone() is not None


def _canon(v):
    """Stable string form of one cell -- BLOB->hex, None->sentinel, typed tag --
    so a row set compares independent of the sqlite driver's Python type map."""
    if v is None:
        return "\x00NULL\x00"
    if isinstance(v, (bytes, bytearray, memoryview)):
        return "b:" + bytes(v).hex()
    if isinstance(v, int):
        return "i:" + str(v)
    if isinstance(v, float):
        return "f:" + repr(v)
    return "t:" + str(v)


def _row_set(con, table, columns):
    """The set of rows of `table` projected to `columns`, each row canonicalized
    to a hashable tuple. A SET (not a list): id is the PK so rows are unique, and
    USER vs DEV-subset ship the rows in different orders -- set-equality is the
    order-independent contract."""
    sel = ",".join(f'"{c}"' for c in columns)
    return {tuple(_canon(c) for c in r)
            for r in con.execute(f'SELECT {sel} FROM "{table}"')}


def _curated_av_ids(dev):
    """The curated address_version id set (kcdx_id IS NOT NULL) in the DEV DB --
    the same curated set the USER projection row-filters statements/
    referenced_vars to. Computed from the rebuilt DEV DB, not a frozen literal."""
    return {r[0] for r in
            dev.execute("SELECT id FROM address_versions WHERE kcdx_id IS NOT NULL")}


def _dev_subset_row_set(dev, table, columns, curated_av_ids):
    """The DEV `table` rows whose address_version_id is in the curated av-id set,
    projected to `columns` -- the independent oracle the USER row set must equal.
    Built via a chunked IN-filter (157 ids today; chunk to stay well under the
    SQLite variable cap regardless)."""
    sel = ",".join(f'"{c}"' for c in columns)
    ids = list(curated_av_ids)
    out = set()
    CHUNK = 500
    for i in range(0, len(ids), CHUNK):
        chunk = ids[i:i + CHUNK]
        ph = ",".join("?" * len(chunk))
        for r in dev.execute(
                f'SELECT {sel} FROM "{table}" '
                f'WHERE address_version_id IN ({ph})', chunk):
            out.add(tuple(_canon(c) for c in r))
    return out


# ---------------------------------------------------------------------------
# The assertions (each falsifiable; each maps to an ACCEPT-RESULT id below).
# ---------------------------------------------------------------------------
def _run_assertions(user_db, dev_db):
    """Return a list of (acceptance_id, ok: bool, detail: str) -- one per §5
    acceptance item. Pure data; the signal emission + pytest asserts read it."""
    results = []

    def record(aid, ok, detail=""):
        results.append((aid, bool(ok), detail))

    uc = sqlite3.connect(user_db)
    dc = sqlite3.connect(dev_db)
    try:
        # The schema declaration matches the pinned contract (catches a drift in
        # USER_COLUMNS itself, before the DB is even consulted).
        record("schema-decl-statements",
               USER_COLUMNS["statements"] == PINNED_STATEMENTS_COLUMNS,
               f"USER_COLUMNS['statements']={USER_COLUMNS['statements']}")
        record("schema-decl-referenced_vars",
               USER_COLUMNS["referenced_vars"] == PINNED_REFERENCED_VARS_COLUMNS,
               f"USER_COLUMNS['referenced_vars']={USER_COLUMNS['referenced_vars']}")

        # §5.1 -- the two tables EXIST in reference.sqlite (USER).
        has_st = _has_table(uc, "statements")
        has_rv = _has_table(uc, "referenced_vars")
        record("user-tables-exist", has_st and has_rv,
               f"statements={has_st} referenced_vars={has_rv}")

        # §5.2 -- EXACT pinned columns: pseudo_text present; content_hash + kcdx_id
        # absent from statements; kcdx_id absent from referenced_vars. A stray or
        # missing column is a contract-drift FAIL.
        st_cols = _columns(uc, "statements") if has_st else []
        rv_cols = _columns(uc, "referenced_vars") if has_rv else []
        record("statements-columns-pinned",
               set(st_cols) == set(PINNED_STATEMENTS_COLUMNS),
               f"extra={sorted(set(st_cols) - set(PINNED_STATEMENTS_COLUMNS))} "
               f"missing={sorted(set(PINNED_STATEMENTS_COLUMNS) - set(st_cols))}")
        record("statements-keeps-pseudo_text", "pseudo_text" in st_cols,
               f"statements cols={st_cols}")
        record("statements-drops-content_hash-and-kcdx_id",
               "content_hash" not in st_cols and "kcdx_id" not in st_cols,
               f"statements cols={st_cols}")
        record("referenced_vars-columns-pinned",
               set(rv_cols) == set(PINNED_REFERENCED_VARS_COLUMNS),
               f"extra={sorted(set(rv_cols) - set(PINNED_REFERENCED_VARS_COLUMNS))} "
               f"missing={sorted(set(PINNED_REFERENCED_VARS_COLUMNS) - set(rv_cols))}")
        record("referenced_vars-drops-kcdx_id", "kcdx_id" not in rv_cols,
               f"referenced_vars cols={rv_cols}")

        # The curated av-id set (computed from the rebuilt DEV DB -- not frozen).
        curated_av_ids = _curated_av_ids(dc)

        # §5.3 -- curated row counts == the curated-address_version_id subset of
        # the DEV tables (computed, not a literal). ~2,385 / ~5,595 is a sense-check.
        user_st = uc.execute("SELECT COUNT(*) FROM statements").fetchone()[0] if has_st else -1
        user_rv = uc.execute("SELECT COUNT(*) FROM referenced_vars").fetchone()[0] if has_rv else -1
        dev_st = _dev_subset_count(dc, "statements", curated_av_ids)
        dev_rv = _dev_subset_count(dc, "referenced_vars", curated_av_ids)
        record("statements-count-equals-curated-subset", user_st == dev_st,
               f"user={user_st} dev-curated-subset={dev_st} (~2385 expected)")
        record("referenced_vars-count-equals-curated-subset", user_rv == dev_rv,
               f"user={user_rv} dev-curated-subset={dev_rv} (~5595 expected)")

        # §5.4 -- the count of distinct address_version_id with >=1 statement
        # (curated functions WITH coverage) matches the computed DEV subset value.
        user_cov = (uc.execute(
            "SELECT COUNT(DISTINCT address_version_id) FROM statements")
            .fetchone()[0] if has_st else -1)
        dev_cov = _dev_subset_distinct_av(dc, "statements", curated_av_ids)
        record("statements-coverage-av-count", user_cov == dev_cov,
               f"user={user_cov} dev-curated-subset={dev_cov} (~133 expected)")

        # §5.5 -- the 5.24M bulk did NOT leak: USER statements is the curated count,
        # orders of magnitude below the DEV bulk total.
        dev_total_st = dc.execute("SELECT COUNT(*) FROM statements").fetchone()[0]
        record("bulk-statements-absent-from-user",
               has_st and user_st == dev_st and user_st < dev_total_st,
               f"user={user_st} dev-bulk-total={dev_total_st}")

        # §5.6 -- call_edges is ABSENT from reference.sqlite (stays DEV-only).
        record("call_edges-absent-from-user", not _has_table(uc, "call_edges"),
               "call_edges present in USER" if _has_table(uc, "call_edges") else "")

        # THE STRONGEST FORM -- SET-EQUALITY (§5 final paragraph), on the SEMANTIC
        # PAYLOAD (the pinned columns except the autoincrement `id`). The USER row
        # set == the DEV curated-subset row set, projected to those columns. If
        # this holds, every count above holds by construction. `id` is excluded
        # because it is a per-DB autoincrement handle (USER renumbers 1..N; DEV
        # keeps sparse bulk ids) -- it is asserted separately below as a present,
        # unique, contiguous PK.
        st_payload = [c for c in PINNED_STATEMENTS_COLUMNS if c != "id"]
        rv_payload = [c for c in PINNED_REFERENCED_VARS_COLUMNS if c != "id"]
        if has_st:
            user_st_rows = _row_set(uc, "statements", st_payload)
            dev_st_rows = _dev_subset_row_set(
                dc, "statements", st_payload, curated_av_ids)
            record("statements-set-equality", user_st_rows == dev_st_rows,
                   f"user_only={len(user_st_rows - dev_st_rows)} "
                   f"dev_only={len(dev_st_rows - user_st_rows)}")
        if has_rv:
            user_rv_rows = _row_set(uc, "referenced_vars", rv_payload)
            dev_rv_rows = _dev_subset_row_set(
                dc, "referenced_vars", rv_payload, curated_av_ids)
            record("referenced_vars-set-equality", user_rv_rows == dev_rv_rows,
                   f"user_only={len(user_rv_rows - dev_rv_rows)} "
                   f"dev_only={len(dev_rv_rows - user_rv_rows)}")

        # `id` is the autoincrement PK: present, unique, NON-NULL, and a
        # contiguous 1..N range in the USER projection (the curated-only insert
        # renumbers from 1). This is what makes excluding `id` from the set
        # comparison above sound -- it is verified here as a well-formed PK, not
        # silently ignored.
        if has_st:
            record("statements-id-contiguous-pk",
                   _id_is_contiguous_pk(uc, "statements", user_st),
                   "statements.id is not a contiguous 1..N PK")
        if has_rv:
            record("referenced_vars-id-contiguous-pk",
                   _id_is_contiguous_pk(uc, "referenced_vars", user_rv),
                   "referenced_vars.id is not a contiguous 1..N PK")
    finally:
        uc.close()
        dc.close()
    return results


def _dev_subset_count(dev, table, curated_av_ids):
    ids = list(curated_av_ids)
    total = 0
    CHUNK = 500
    for i in range(0, len(ids), CHUNK):
        chunk = ids[i:i + CHUNK]
        ph = ",".join("?" * len(chunk))
        total += dev.execute(
            f'SELECT COUNT(*) FROM "{table}" '
            f'WHERE address_version_id IN ({ph})', chunk).fetchone()[0]
    return total


def _dev_subset_distinct_av(dev, table, curated_av_ids):
    ids = list(curated_av_ids)
    seen = set()
    CHUNK = 500
    for i in range(0, len(ids), CHUNK):
        chunk = ids[i:i + CHUNK]
        ph = ",".join("?" * len(chunk))
        for r in dev.execute(
                f'SELECT DISTINCT address_version_id FROM "{table}" '
                f'WHERE address_version_id IN ({ph})', chunk):
            seen.add(r[0])
    return len(seen)


def _id_is_contiguous_pk(con, table, expected_count):
    """The `id` column is a present, unique, non-null, contiguous 1..N PK -- the
    well-formedness that makes excluding `id` from the payload set-equality
    sound (the curated-only insert renumbers from 1)."""
    rows = con.execute(
        f'SELECT id FROM "{table}" ORDER BY id').fetchall()
    ids = [r[0] for r in rows]
    if len(ids) != expected_count:
        return False
    if any(i is None for i in ids):
        return False
    return ids == list(range(1, expected_count + 1))


def _emit_signal(results):
    """Emit the canonical acceptance signal (.claude/rules/acceptance-signal.md)
    to stdout -- one ACCEPT-RESULT per item, one ACCEPT-SUITE aggregate last. The
    agent greps these fixed tokens; the user reads nothing."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


def test_curated_statement_projection():
    """The DB-shape gate: the rebuilt USER reference.sqlite ships statements +
    referenced_vars for the curated subset, with the pinned columns, no bulk
    leak, no call_edges -- proven by set-equality against the DEV curated subset.
    Emits the canonical ACCEPT signal."""
    user_db, dev_db, tmp = _rebuild_user_db()
    try:
        results = _run_assertions(user_db, dev_db)
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "curated-statement projection contract drift:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


if __name__ == "__main__":
    user_db, dev_db, tmp = _rebuild_user_db()
    try:
        results = _run_assertions(user_db, dev_db)
    finally:
        import shutil
        shutil.rmtree(tmp, ignore_errors=True)
    _emit_signal(results)
    failed = [aid for aid, ok, _ in results if not ok]
    sys.exit(1 if failed else 0)
