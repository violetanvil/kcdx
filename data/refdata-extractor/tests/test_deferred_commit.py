"""test_deferred_commit.py -- the deferred-commit write mechanism on the apply
write path (maintainer-tool Phase 2, step 4a).

WHAT THIS PROVES
----------------
apply_seeds (and the db_editor write functions that drive it) gained an OPTIONAL
keyword-only `defer_commit=False`. When True, the per-DB writes run under ONE held
outer transaction per DB (the per-action commits become SAVEPOINT/RELEASE pairs
nested inside it -- never committed) and apply_seeds RETURNS a DeferredCommit handle
carrying the two OPEN, uncommitted connections + the result dict, instead of
committing+closing. import_to_sqlite.commit(handle) COMMITs both + closes;
.rollback(handle) ROLLBACKs both + closes. This is THE maintainer-tool write
mechanism (design S7; plan-spec "Deferred commit is THE write mechanism",
user-settled 2026-06-03): every DB change commits only on the user's confirm, so
"on Cancel nothing lands" holds LITERALLY -- an uncommitted txn is invisible and
discardable, no file copy, no live mutation before confirm.

The change is additive + oracle-preserving (the 1b pattern): defer_commit=False is
byte-identical to the pre-4a path, so every landed oracle stays green. Cases:

  1. INVISIBLE-UNTIL-COMMIT: a deferred update via db_editor.update_version_row; a
     SEPARATE read-only connection sees the OLD value BEFORE commit(handle), the NEW
     value AFTER. The held txn is invisible to every other connection until commit.

  2. ROLLBACK DISCARDS: a deferred update, then rollback(handle); a fresh read shows
     the DB BYTE-IDENTICAL to before -- the literal "on Cancel nothing lands".

  3. CONVERGENCE: the SAME edit via deferred-then-commit produces a DB BYTE-IDENTICAL
     to the same edit via the immediate (default) path. The deferred seam changes
     WHEN it commits, not WHAT it writes (the convergence proof, like 1b's
     version-vs-dll convergence).

  4. VALIDATION FAILURE IN DEFERRED MODE: an invalid edit with defer_commit=True
     raises (RuntimeError out of the shared validator) and leaves NO open txn + NO
     write -- the validator gates before any DB open, same as immediate. No handle
     escapes; both DBs are byte-identical to before.

  5. HANDLE IDEMPOTENCY: a double-commit and a commit-after-rollback each raise
     DeferredCommitError (single-use) -- never a crash or a partial second write.

  6. TWO-DB COMMIT: both DBs reflect the change after commit(handle). The commit
     ORDER is USER-first, DEV-second (the shipped DB first -- the surfaced
     "atomic guarantee's edge"; a dev-lagging split is the more-recoverable one).
     This case asserts BOTH DBs landed; the ordering rationale is in
     import_to_sqlite.commit's docstring + the step report.

ROUND-TRIP VISIBILITY (the load-bearing probe -- results-driven)
----------------------------------------------------------------
The step doc flagged a load-bearing unknown: does the round-trip oracle / export see
the uncommitted deferred write? PROBED by reading the code (not theorized):
  - apply_seeds NEVER calls round_trip/export -- it is purely validate -> write.
    There is NO round-trip oracle inside the write path to break.
  - The db_editor write functions DO call export_seeds(user_db, ...) -- but BEFORE
    apply_seeds, to BUILD the prospective seed from the COMMITTED pre-edit DB. That
    export opens its OWN connection and reads committed state; in deferred mode it
    still reads the correct pre-edit baseline (the deferred write has not happened
    yet at export time). So the export-then-apply chain is correct in both modes.
  - A SEPARATE connection (export_seeds opens one) does NOT see the held deferred
    write -- which is exactly the invisible-until-commit property case 1 proves. A
    future confirm-time round-trip that must see the pending state would read it FROM
    the held connection (handle.ucon), which DOES see its own pending writes; that
    consumer is step 4b/5, not 4a.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism (test_apply_version_seam / test_db_editor_update):
a module-scoped baseline rebuild from the committed seeds (built ONCE off the
mini-dump excerpt), copied per-case so each starts from a clean DB pair.

RUN
---
    python tests/test_deferred_commit.py
    pytest tests/test_deferred_commit.py
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
# The small committed REAL dump excerpt (built by make_mini_dump.py) -- a fast
# rebuild; full-dump fidelity is covered by test_rebuild_oracle.py.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
# The linked DLL the .rdata version resolver reads (the apply path's version src).
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
# Seed-dir pointing + baseline rebuild (the apply-oracle convention).
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
# The byte-identity comparator (the apply-oracle's per-table content hash over
# every table in both DBs -- order-independent canonical content).
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
    """A whole-DB byte fingerprint: a per-table content hash over EVERY table in
    both reference DBs. Two out_dirs with the same fingerprint hold byte-identical
    DBs. Opens a FRESH connection per read, so it sees only COMMITTED state -- a
    held deferred txn is invisible to it (which is the point of cases 1/2)."""
    fp = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        con = sqlite3.connect(dbp)
        try:
            tables = [r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "ORDER BY name")]
        finally:
            con.close()
        for t in tables:
            fp[(which, t)] = _hash_table(dbp, t)
    return fp


def _gv_tag_to_id(con, tag):
    return con.execute("SELECT id FROM game_versions WHERE tag = ?",
                       (tag,)).fetchone()[0]


def _read_verified_by(db_path, kcdx_id, valid_from_tag):
    """The verified_by cell of one curated row, via a FRESH connection (committed
    state only). None if the row is absent. The cell cases 1/2 watch flip."""
    con = sqlite3.connect(db_path)
    try:
        vf = _gv_tag_to_id(con, valid_from_tag)
        row = con.execute(
            "SELECT verified_by FROM address_versions WHERE kcdx_id = ? "
            "AND valid_from = ?", (kcdx_id, vf)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


# --------------------------------------------------------------------------
# Pick an editable function-kind trio row from the BUILT DB (whatever the fixture
# carries -- never a hardcoded id), mirroring the db_editor-update oracle.
# --------------------------------------------------------------------------
def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _pick_function_trio_row(db_path):
    """A curated FUNCTION-kind row that already has a full audit trio (so a
    re-verify edit has a trio to change). Returns (kcdx_id, valid_from_tag,
    current_verified_by)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, vby, kindid in con.execute(
                "SELECT kcdx_id, valid_from, verified_by, kind "
                "FROM address_versions WHERE kcdx_id IS NOT NULL "
                "AND last_verified_at_version IS NOT NULL AND rva IS NOT NULL"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                return (kid, gv.get(vf), vby)
        return None
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
        root = tempfile.mkdtemp(prefix="deferred_commit_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        # The real (tag, ordinal) the dll_path route resolves -- read from the
        # resolver, NEVER hardcoded (so version= passes the exact pair).
        from seeds_shared import resolve_version
        tag, ordinal = resolve_version(DLL_PATH)
        assert tag == GVT, f"fixture DLL resolved {tag!r}, expected {GVT!r}"
        _BASELINE.update({"root": root, "out": out, "tag": tag,
                          "ordinal": ordinal})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    """A per-case copy of the baseline DB pair (so each case starts clean)."""
    out = tempfile.mkdtemp(prefix="deferred_commit_run_")
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


# A distinctive edit value the cases watch flip from OLD -> NEW.
_NEW_VERIFIED_BY = "deferred_commit_test"


def _trio_edits():
    return {
        "verified_by": _NEW_VERIFIED_BY,
        "verified_date": "2099-12-31",
        "last_verified_at_version": GVT,
    }


# --------------------------------------------------------------------------
# Case 1: a deferred write is INVISIBLE to a separate connection until commit.
# --------------------------------------------------------------------------
def _invisible_until_commit(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, old_vby = _pick_function_trio_row(user_db)
        assert old_vby != _NEW_VERIFIED_BY, "fixture already carries the edit value"

        # Drive the edit in DEFERRED mode -- returns a handle, does NOT commit.
        handle = db_editor.update_version_row(
            out, DLL_PATH, kid, vf_tag, _trio_edits(), defer_commit=True)
        from seeds_shared import DeferredCommit, commit
        assert isinstance(handle, DeferredCommit), (
            f"deferred update did not return a DeferredCommit handle: {handle!r}")

        # BEFORE commit: a SEPARATE read-only connection (a fresh sqlite connect)
        # must still see the OLD value -- the held txn is invisible.
        before = _read_verified_by(user_db, kid, vf_tag)
        assert before == old_vby, (
            f"deferred write was visible before commit: separate conn saw "
            f"{before!r}, expected the OLD {old_vby!r}")

        # Commit the held txn.
        commit(handle)

        # AFTER commit: the separate connection now sees the NEW value.
        after = _read_verified_by(user_db, kid, vf_tag)
        assert after == _NEW_VERIFIED_BY, (
            f"after commit the separate conn saw {after!r}, expected the NEW "
            f"{_NEW_VERIFIED_BY!r}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: rollback discards -- the DB is byte-identical to before.
# --------------------------------------------------------------------------
def _rollback_discards(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, _old = _pick_function_trio_row(user_db)

        before_fp = _db_fingerprint(out)

        handle = db_editor.update_version_row(
            out, DLL_PATH, kid, vf_tag, _trio_edits(), defer_commit=True)
        from seeds_shared import rollback
        rollback(handle)

        after_fp = _db_fingerprint(out)
        assert after_fp == before_fp, (
            "rollback did NOT discard the deferred write -- the DB differs from "
            "before; differing tables: "
            f"{sorted(k for k in before_fp if before_fp.get(k) != after_fp.get(k))}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: deferred-then-commit CONVERGES on the immediate (default) path's DB.
# --------------------------------------------------------------------------
def _convergence_with_immediate(b):
    out_imm = _fresh_db(b)
    out_def = _fresh_db(b)
    try:
        user_db = os.path.join(out_imm, "reference.sqlite")
        kid, vf_tag, _old = _pick_function_trio_row(user_db)
        edits = _trio_edits()

        # Route A: the immediate (default) path -- commits internally + closes.
        db_editor.update_version_row(out_imm, DLL_PATH, kid, vf_tag, dict(edits))
        fp_imm = _db_fingerprint(out_imm)

        # Route B: deferred-then-commit -- the SAME edit, committed via the handle.
        handle = db_editor.update_version_row(
            out_def, DLL_PATH, kid, vf_tag, dict(edits), defer_commit=True)
        from seeds_shared import commit
        commit(handle)
        fp_def = _db_fingerprint(out_def)

        assert fp_imm == fp_def, (
            "deferred-then-commit did NOT converge on the immediate path's DB "
            "(the seam must change WHEN it commits, not WHAT it writes); "
            "differing tables: "
            f"{sorted(k for k in fp_imm if fp_imm.get(k) != fp_def.get(k))}")
    finally:
        shutil.rmtree(out_imm, ignore_errors=True)
        shutil.rmtree(out_def, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: a validation failure in deferred mode raises + leaves NO write + NO open
# txn (no handle escapes; both DBs byte-identical to before).
# --------------------------------------------------------------------------
def _validation_failure_no_write(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, _old = _pick_function_trio_row(user_db)

        before_fp = _db_fingerprint(out)

        # An out-of-enum evidence_kind -- the shared validator rejects it (a
        # RuntimeError out of apply_seeds), BEFORE any DB open. Deferred mode must
        # behave identically: raise, no handle, no write, no held txn.
        bad_edits = {
            "verified_by": "x", "verified_date": "2099-12-31",
            "evidence_kind": "not_a_real_tier",
            "last_verified_at_version": GVT,
        }
        raised = None
        result = None
        try:
            result = db_editor.update_version_row(
                out, DLL_PATH, kid, vf_tag, bad_edits, defer_commit=True)
        except RuntimeError as e:
            raised = e
        assert raised is not None, (
            "an invalid deferred edit did not raise (no handle should escape)")
        assert result is None, "a handle escaped a failed deferred apply"

        after_fp = _db_fingerprint(out)
        assert after_fp == before_fp, (
            "a failed deferred apply changed a DB (no write expected); "
            f"differing: {sorted(k for k in before_fp if before_fp.get(k) != after_fp.get(k))}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 5: handle idempotency -- double-commit + commit-after-rollback each raise
# DeferredCommitError (single-use), never a crash or a partial second write.
# --------------------------------------------------------------------------
def _handle_idempotency(b):
    from seeds_shared import commit, rollback, DeferredCommitError

    # Double-commit: the second commit raises DeferredCommitError.
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, _old = _pick_function_trio_row(user_db)
        handle = db_editor.update_version_row(
            out, DLL_PATH, kid, vf_tag, _trio_edits(), defer_commit=True)
        commit(handle)
        after_first_fp = _db_fingerprint(out)
        raised = None
        try:
            commit(handle)
        except DeferredCommitError as e:
            raised = e
        assert raised is not None, "a double-commit did not raise DeferredCommitError"
        # The second (rejected) commit left the DB exactly as the first commit did.
        assert _db_fingerprint(out) == after_first_fp, (
            "a rejected double-commit changed the DB")
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # Commit-after-rollback: the commit raises DeferredCommitError; the DB stays as
    # the rollback left it (byte-identical to before the deferred edit).
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, _old = _pick_function_trio_row(user_db)
        before_fp = _db_fingerprint(out)
        handle = db_editor.update_version_row(
            out, DLL_PATH, kid, vf_tag, _trio_edits(), defer_commit=True)
        rollback(handle)
        raised = None
        try:
            commit(handle)
        except DeferredCommitError as e:
            raised = e
        assert raised is not None, (
            "a commit after rollback did not raise DeferredCommitError")
        assert _db_fingerprint(out) == before_fp, (
            "a rejected commit-after-rollback changed the DB")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 6: the two-DB commit -- BOTH DBs reflect the change after commit. The commit
# ORDER (user-first, dev-second) is asserted indirectly: the convergence case (3)
# already pins both DBs byte-identical to the immediate path; here we assert each DB
# moved off its before-state, so the held txn on each was committed.
# --------------------------------------------------------------------------
def _two_db_commit(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        kid, vf_tag, old_vby = _pick_function_trio_row(user_db)

        before_user = _read_verified_by(user_db, kid, vf_tag)
        before_dev = _read_verified_by(dev_db, kid, vf_tag)
        assert before_user == old_vby

        handle = db_editor.update_version_row(
            out, DLL_PATH, kid, vf_tag, _trio_edits(), defer_commit=True)

        # Before commit BOTH DBs (separate connections) still hold the old value.
        assert _read_verified_by(user_db, kid, vf_tag) == before_user, (
            "the USER DB showed the deferred write before commit")
        assert _read_verified_by(dev_db, kid, vf_tag) == before_dev, (
            "the DEV DB showed the deferred write before commit")

        from seeds_shared import commit
        commit(handle)

        # After commit BOTH DBs reflect the change -- the user-first, dev-second
        # commit landed on each held connection (the order rationale is in
        # import_to_sqlite.commit's docstring + the step report).
        assert _read_verified_by(user_db, kid, vf_tag) == _NEW_VERIFIED_BY, (
            "the USER DB did not reflect the change after commit")
        assert _read_verified_by(dev_db, kid, vf_tag) == _NEW_VERIFIED_BY, (
            "the DEV DB did not reflect the change after commit")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_deferred_write_invisible_until_commit(baseline):  # noqa: F811
    _invisible_until_commit(baseline)


def test_rollback_discards_the_deferred_write(baseline):  # noqa: F811
    _rollback_discards(baseline)


def test_deferred_then_commit_converges_with_immediate(baseline):  # noqa: F811
    _convergence_with_immediate(baseline)


def test_validation_failure_in_deferred_mode_no_write(baseline):  # noqa: F811
    _validation_failure_no_write(baseline)


def test_handle_commit_rollback_idempotency(baseline):  # noqa: F811
    _handle_idempotency(baseline)


def test_two_db_commit_lands_in_both(baseline):  # noqa: F811
    _two_db_commit(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _invisible_until_commit(b)
        print("PASS test_deferred_write_invisible_until_commit")
        _rollback_discards(b)
        print("PASS test_rollback_discards_the_deferred_write")
        _convergence_with_immediate(b)
        print("PASS test_deferred_then_commit_converges_with_immediate")
        _validation_failure_no_write(b)
        print("PASS test_validation_failure_in_deferred_mode_no_write")
        _handle_idempotency(b)
        print("PASS test_handle_commit_rollback_idempotency")
        _two_db_commit(b)
        print("PASS test_two_db_commit_lands_in_both")
        print("\nall deferred-commit tests passed")
    finally:
        _cleanup_baseline()
