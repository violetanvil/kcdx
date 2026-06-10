"""test_db_editor_batch.py -- the D32 batch save-spine (N UPDATEs as ONE atomic
transaction; maintainer-tool Phase 6, step 6.2 batch confirm).

WHAT THIS PROVES
----------------
db_editor.update_version_rows_batch applies N validated version-row UPDATEs as ONE
deferred-commit transaction -- the design D32 / §7 batch mutation (all-UPDATE,
all-or-nothing). It COMPOSES the single-edit primitive: ONE export(committed DB),
ALL N edits folded onto that ONE prospective seed, ONE apply_direct_edit drive (one
held outer txn per DB, one scoped restore-point), ONE DeferredCommit handle. It
invents NO new write mechanism (no per-row commit -- that would NOT be
all-or-nothing).

The cases (each falsifiable):

  1. ALL N LAND (batch success): N valid edits via update_version_rows_batch
     (defer_commit) -> ONE handle -> commit(handle) -> every one of the N rows holds
     its new value in BOTH DBs. FALSIFIABLE: a missing/unchanged row fails it.

  2. CONVERGENCE: the batch (deferred-then-commit) produces a DB byte-identical to
     applying the SAME N edits as N SEQUENTIAL single-edit immediate calls. The batch
     seam changes WHEN/how-many-times it commits, not WHAT it writes. FALSIFIABLE: a
     fingerprint divergence fails it.

  3. ALL-OR-NOTHING (the D21 invariant -- the load-bearing case): a batch whose LAST
     row is invalid (an out-of-enum evidence_kind the shared validator rejects) raises
     and leaves the DB BYTE-IDENTICAL to before -- the earlier valid rows are NOT
     committed. FALSIFIABLE BY DESIGN: a per-row-commit implementation would have
     landed rows 1..K-1 before hitting the bad row K -> the DB would DIFFER -> this
     assertion fails. The single canonical guard against a fake-batch (a commit-per-row
     loop): it asserts the DB is byte-identical AFTER a mid-batch failure.

  4. EMPTY BATCH: an empty edits_list raises DbEditError (a batch confirms >= 1 edit),
     no write.

  5. STALE KEY: a batch row whose identity key matches no row raises DbEditError and
     leaves the DB byte-identical -- a bad key fails the WHOLE batch before any write
     (no partial fold lands).

REAL EVERYTHING; reuses the apply-oracle baseline (a module-scoped rebuild from the
committed seeds, copied per-case). Skips gracefully if the mini-dump / DLL is absent.

RUN
---
    python tests/test_db_editor_batch.py
    pytest tests/test_db_editor_batch.py
"""
import contextlib
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
DLL_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "third-party-ghidra", "WHGame.dll"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import db_editor  # noqa: E402

GVT = imp.GAME_VERSION_TAG   # "1.5.1164953"
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# --------------------------------------------------------------------------
# Seed-dir pointing + baseline rebuild (the apply-oracle convention, shared with
# test_deferred_commit / test_db_editor_update).
# --------------------------------------------------------------------------
def _copy_seeds(dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(dst_dir, f))


@contextlib.contextmanager
def _seeds_pointed_at(seed_dir):
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir,
                                                 "address_versions_seed.csv")
    try:
        yield
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def _rebuild_into(seed_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    with _seeds_pointed_at(seed_dir):
        imp.run_rebuild(DUMP_DIR, out_dir)


# --------------------------------------------------------------------------
# The byte-identity comparator (the apply-oracle's per-table content hash over every
# table in both DBs -- the no-change oracle for cases 2/3/5; opens FRESH connections
# so it sees only COMMITTED state).
# --------------------------------------------------------------------------
def _hash_table(db_path, table):
    con = sqlite3.connect(db_path)
    try:
        cols = [c[1] for c in con.execute(f'PRAGMA table_info("{table}")')]
        rows = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} FROM "{table}"'
        ).fetchall()
        h = hashlib.sha256()

        def canon(v):
            if v is None:
                return "\x00"
            if isinstance(v, (bytes, bytearray, memoryview)):
                return "b:" + bytes(v).hex()
            return f"{type(v).__name__}:{v}"

        for r in sorted(repr(tuple(canon(c) for c in row)) for row in rows):
            h.update(r.encode("utf-8"))
        return h.hexdigest()
    finally:
        con.close()


def _db_fingerprint(out_dir):
    """A whole-DB byte fingerprint over EVERY table in both reference DBs (incl.
    sqlite_sequence -- a table, so the PK watermark is in the hash). Two out_dirs with
    the same fingerprint hold byte-identical DBs. FRESH connection per read -> sees
    only committed state."""
    fp = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        con = sqlite3.connect(dbp)
        try:
            tables = [r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
        finally:
            con.close()
        for t in tables:
            fp[(which, t)] = _hash_table(dbp, t)
    return fp


def _gv_tag_to_id(con, tag):
    return con.execute("SELECT id FROM game_versions WHERE tag = ?",
                       (tag,)).fetchone()[0]


def _read_verified_by(db_path, kcdx_id, valid_from_tag):
    con = sqlite3.connect(db_path)
    try:
        vf = _gv_tag_to_id(con, valid_from_tag)
        row = con.execute(
            "SELECT verified_by FROM address_versions WHERE kcdx_id = ? "
            "AND valid_from = ?", (kcdx_id, vf)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _pick_function_trio_rows(db_path, n):
    """The first `n` curated FUNCTION-kind rows that each carry a full audit trio (so
    a re-verify edit has a trio to change). Returns a list of (kcdx_id, valid_from_tag,
    current_verified_by). Never a hardcoded id -- read from the built DB."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        out = []
        for kid, vf, vby, kindid in con.execute(
                "SELECT kcdx_id, valid_from, verified_by, kind "
                "FROM address_versions WHERE kcdx_id IS NOT NULL "
                "AND last_verified_at_version IS NOT NULL AND rva IS NOT NULL "
                "ORDER BY kcdx_id"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                out.append((kid, gv.get(vf), vby))
                if len(out) == n:
                    break
        return out
    finally:
        con.close()


# --------------------------------------------------------------------------
# Module-scoped baseline (built ONCE, copied per-case).
# --------------------------------------------------------------------------
_BASELINE = {}


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}")
    if not os.path.isfile(DLL_PATH):
        raise SystemExit(f"WHGame.dll not found: {DLL_PATH}")


def _get_baseline():
    if "root" not in _BASELINE:
        _require_inputs()
        root = tempfile.mkdtemp(prefix="batch_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        from seeds_shared import resolve_version
        tag, ordinal = resolve_version(DLL_PATH)
        assert tag == GVT, f"fixture DLL resolved {tag!r}, expected {GVT!r}"
        _BASELINE.update({"root": root, "out": out, "tag": tag, "ordinal": ordinal})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    out = tempfile.mkdtemp(prefix="batch_run_")
    for f in ("reference.sqlite", "reference-dev.sqlite"):
        shutil.copy2(os.path.join(b["out"], f), os.path.join(out, f))
    return out


try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


# Distinct edit values per batch row, so each row's landing is independently checkable.
def _batch_edits(rows):
    """One {kcdx_id, valid_from_version, edits} spec per (kcdx_id, vf_tag, _vby) row,
    each setting a DISTINCT verified_by (so case 1 proves every row landed its OWN
    value, not one value smeared across all)."""
    specs = []
    for i, (kid, vf_tag, _vby) in enumerate(rows):
        specs.append({
            "kcdx_id": kid,
            "valid_from_version": vf_tag,
            "edits": {
                "verified_by": f"batch_signer_{i}",
                "verified_date": "2099-12-31",
                "last_verified_at_version": GVT,
            },
        })
    return specs


# --------------------------------------------------------------------------
# Case 1: all N edits land on ONE commit -- every row holds its OWN new value in BOTH DBs.
# --------------------------------------------------------------------------
def _batch_all_land(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        rows = _pick_function_trio_rows(user_db, 3)
        assert len(rows) == 3, "fixture lacks 3 editable function-trio rows"
        specs = _batch_edits(rows)

        from seeds_shared import DeferredCommit, commit
        handle = db_editor.update_version_rows_batch(
            out, DLL_PATH, specs, defer_commit=True)
        assert isinstance(handle, DeferredCommit), (
            f"batch deferred did not return ONE DeferredCommit handle: {handle!r}")

        # BEFORE commit: a separate connection still sees every OLD value (the whole
        # batch is in ONE held, invisible txn).
        for (kid, vf_tag, old_vby), spec in zip(rows, specs):
            assert _read_verified_by(user_db, kid, vf_tag) == old_vby, (
                f"row kid={kid} was visible before commit (held txn leaked)")

        commit(handle)

        # AFTER commit: EVERY row holds its OWN distinct new value in BOTH DBs.
        for (kid, vf_tag, _old), spec in zip(rows, specs):
            want = spec["edits"]["verified_by"]
            assert _read_verified_by(user_db, kid, vf_tag) == want, (
                f"USER DB row kid={kid} did not land its batch value {want!r}")
            assert _read_verified_by(dev_db, kid, vf_tag) == want, (
                f"DEV DB row kid={kid} did not land its batch value {want!r}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: the batch CONVERGES on N sequential single-edit immediate applies.
# --------------------------------------------------------------------------
def _batch_converges_with_sequential(b):
    out_seq = _fresh_db(b)
    out_batch = _fresh_db(b)
    try:
        user_db = os.path.join(out_seq, "reference.sqlite")
        rows = _pick_function_trio_rows(user_db, 3)
        specs = _batch_edits(rows)

        # Route A: N SEQUENTIAL single-edit immediate applies (each commits internally).
        for spec in specs:
            db_editor.update_version_row(
                out_seq, DLL_PATH, spec["kcdx_id"], spec["valid_from_version"],
                dict(spec["edits"]))
        fp_seq = _db_fingerprint(out_seq)

        # Route B: ONE batch (deferred-then-commit) of the SAME N edits.
        from seeds_shared import commit
        handle = db_editor.update_version_rows_batch(
            out_batch, DLL_PATH, [dict(s, edits=dict(s["edits"])) for s in specs],
            defer_commit=True)
        commit(handle)
        fp_batch = _db_fingerprint(out_batch)

        assert fp_seq == fp_batch, (
            "the batch did NOT converge on N sequential single-edit applies (the batch "
            "seam must change how MANY times it commits, not WHAT it writes); differing "
            f"tables: {sorted(k for k in fp_seq if fp_seq.get(k) != fp_batch.get(k))}")
    finally:
        shutil.rmtree(out_seq, ignore_errors=True)
        shutil.rmtree(out_batch, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3 (THE load-bearing all-or-nothing case, D21/D32): a batch whose LAST row is
# invalid leaves the DB BYTE-IDENTICAL -- the earlier valid rows are NOT committed.
# This is the falsifiable guard against a per-row-commit fake-batch: a commit-per-row
# loop would have landed rows 0..K-1 before the bad row K -> the DB would DIFFER.
# --------------------------------------------------------------------------
def _batch_one_bad_row_rolls_back_whole(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        rows = _pick_function_trio_rows(user_db, 3)
        assert len(rows) == 3, "fixture lacks 3 editable function-trio rows"

        before_fp = _db_fingerprint(out)

        specs = _batch_edits(rows)
        # Make the LAST row invalid (an out-of-enum evidence_kind -- the shared
        # validator's HARD ERROR over the whole prospective state). The earlier rows
        # are perfectly valid. A per-row-commit impl would commit rows 0,1 and die on 2.
        specs[-1]["edits"]["evidence_kind"] = "not_a_real_tier"

        raised = None
        result = None
        try:
            result = db_editor.update_version_rows_batch(
                out, DLL_PATH, specs, defer_commit=True)
        except RuntimeError as e:   # the shared validator's verdict
            raised = e
        assert raised is not None, (
            "an invalid batch row did not raise (the validator must reject the whole "
            "prospective state)")
        assert result is None, "a handle escaped a failed batch (the txn must not open)"

        # ALL-OR-NOTHING: the DB is byte-identical to before -- NONE of the batch
        # landed, not even the valid rows 0/1. THIS fails a per-row-commit impl.
        after_fp = _db_fingerprint(out)
        assert after_fp == before_fp, (
            "a batch with one invalid row left a CHANGED DB -- the earlier valid rows "
            "committed (NOT all-or-nothing; a per-row-commit fake-batch). Differing "
            f"tables: {sorted(k for k in before_fp if before_fp.get(k) != after_fp.get(k))}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: an empty batch raises DbEditError (>= 1 edit), no write.
# --------------------------------------------------------------------------
def _batch_empty_raises(b):
    out = _fresh_db(b)
    try:
        before_fp = _db_fingerprint(out)
        raised = None
        try:
            db_editor.update_version_rows_batch(out, DLL_PATH, [], defer_commit=True)
        except db_editor.DbEditError as e:
            raised = e
        assert raised is not None, "an empty batch did not raise DbEditError"
        assert _db_fingerprint(out) == before_fp, "an empty batch changed the DB"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 5: a stale identity key in any batch row fails the WHOLE batch (DbEditError)
# before any DB write -- the DB stays byte-identical (no partial fold lands).
# --------------------------------------------------------------------------
def _batch_stale_key_rolls_back_whole(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        rows = _pick_function_trio_rows(user_db, 2)
        before_fp = _db_fingerprint(out)

        specs = _batch_edits(rows)
        # Inject a row whose identity key matches nothing (a huge kcdx_id) BETWEEN the
        # valid rows -- the whole batch must fail before any DB open.
        specs.insert(1, {
            "kcdx_id": 99999999,
            "valid_from_version": GVT,
            "edits": {"verified_by": "ghost"},
        })
        raised = None
        try:
            db_editor.update_version_rows_batch(out, DLL_PATH, specs, defer_commit=True)
        except db_editor.DbEditError as e:
            raised = e
        assert raised is not None, "a stale-key batch row did not raise DbEditError"
        assert _db_fingerprint(out) == before_fp, (
            "a stale-key batch changed the DB (a partial fold/write leaked)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_batch_all_n_edits_land_on_one_commit(baseline):  # noqa: F811
    _batch_all_land(baseline)


def test_batch_converges_with_sequential_single_edits(baseline):  # noqa: F811
    _batch_converges_with_sequential(baseline)


def test_batch_one_invalid_row_rolls_back_the_whole_batch(baseline):  # noqa: F811
    _batch_one_bad_row_rolls_back_whole(baseline)


def test_batch_empty_raises(baseline):  # noqa: F811
    _batch_empty_raises(baseline)


def test_batch_stale_key_rolls_back_the_whole_batch(baseline):  # noqa: F811
    _batch_stale_key_rolls_back_whole(baseline)


if __name__ == "__main__":
    b = _get_baseline()
    try:
        _batch_all_land(b)
        _batch_converges_with_sequential(b)
        _batch_one_bad_row_rolls_back_whole(b)
        _batch_empty_raises(b)
        _batch_stale_key_rolls_back_whole(b)
        print("all batch cases passed")
    finally:
        _cleanup_baseline()
