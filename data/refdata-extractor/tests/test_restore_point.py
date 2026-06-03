"""test_restore_point.py -- the SCOPED restore-point data-core capability
(maintainer-tool Phase 2, step 4d; design D21).

WHAT THIS PROVES
----------------
A POST-commit failure (export / integrity / git, which run AFTER the irreversible DB
commit) must undo the committed write -- "on ANY failure nothing lands, incl. PK
auto-increment reset" (D21). The deferred ROLLBACK (4a) covers a PRE-commit failure;
once commit(handle) runs it is gone (it COMMITs + CLOSES both connections). So a
SCOPED restore-point -- captured by apply_direct_edit BEFORE the commit, restored on a
post-commit failure -- is the post-commit half of the robust rollback. It is a
DATA-CORE capability (D13/law 6): it owns the write semantics + the open connections +
knows the touched rows. It captures ONLY the touched rows + each DB's sqlite_sequence
(a few KB regardless of DB size), NEVER a SELECT * or a ~1.3 GB file copy.

  CAPTURE-COMMIT-RESTORE BYTE-IDENTITY (the load-bearing proof) -- for each job shape
    (re-verify, full-column UPDATE, create-entity, supersede, deprecate, create-
    version-at-a-NEW-tag): apply the edit DEFERRED -> commit(handle) (the change is
    REAL) -> restore(handle) -> assert the WHOLE DB (both DBs, every table, via the
    4c whole-DB fingerprint) is BYTE-IDENTICAL to the pre-edit baseline INCLUDING
    sqlite_sequence (read pre-edit + post-restore + assert equal) AND a subsequent add
    reuses the same next id (the PK-reset proof). This proves the scoped restore is
    COMPLETE -- it misses no touched row for any shape.

  THE NEW-TAG RESTORE -- a create-version-at-a-NEW-tag, committed, then restored: the
    new game_versions row gone, the prior interval re-opened, the new av row gone,
    sqlite_sequence reset -> byte-identical.

  BOTH DBs -- a function-kind add (USER promotes-by-INSERT, DEV promotes-in-place an
    EXISTING bulk row) -> restore leaves BOTH DBs byte-identical (the DEV in-place
    promote is the shape a naive seq-boundary-only restore would MISS).

  SCOPE IS BOUNDED (O(actions), not O(table)) -- the capture's touched-row count is
    small + proportional to the edit, never the table size: a structural assertion that
    it is scoped, not a full-table snapshot.

  IDEMPOTENT -- a double restore lands the same byte-identical state.

  ALL against the real mini-dump DB.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the 4c baseline mechanism (a module-scoped rebuild from the committed seeds off
the mini-dump excerpt, copied per-case so each starts from a clean DB pair) + the 4c
whole-DB fingerprint + sqlite_sequence reader + the row pickers.

RUN
---
    python tests/test_restore_point.py
    pytest tests/test_restore_point.py
"""
import os
import shutil
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import db_editor  # noqa: E402
from seeds_shared import commit, restore, rollback, DeferredCommit  # noqa: E402


def _as_handle(ret):
    """The db_editor entry points return EITHER a bare DeferredCommit handle
    (update_version_row) OR a {"result": handle, ...} dict (create_*/supersede/
    deprecate). Normalise to the handle (the 4c _commit_direct idiom)."""
    h = ret["result"] if isinstance(ret, dict) else ret
    assert isinstance(h, DeferredCommit), f"not a DeferredCommit handle: {h!r}"
    return h

# Reuse 4c's fixture machinery + comparators wholesale (the baseline rebuild, the
# whole-DB fingerprint, the sqlite_sequence reader, the row pickers, the seed-source
# reader). 4d is additive on the SAME data-core, so it shares the same oracle idiom.
import test_direct_write as dw  # noqa: E402

GVT = imp.GAME_VERSION_TAG
NEW_TAG = dw.NEW_TAG


# --------------------------------------------------------------------------
# sqlite_sequence over BOTH DBs (the PK-reset proof reads the autoincrement tables).
# --------------------------------------------------------------------------
_AUTOINC = ("address_versions", "survival", "game_versions")


def _all_seqs(out_dir):
    """Every autoincrement table's sqlite_sequence watermark across BOTH DBs -- the
    PK-reset proof asserts this dict is identical pre-edit and post-restore."""
    seqs = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        for t in _AUTOINC:
            seqs[(which, t)] = dw._seq_value(dbp, t)
    return seqs


# --------------------------------------------------------------------------
# The capture-commit-restore byte-identity driver (the load-bearing proof).
# --------------------------------------------------------------------------
def _capture_commit_restore_is_identical(b, do_edit, label, *, setup=None):
    """Run `do_edit(out)` -> commit -> restore on one fresh DB and assert the WHOLE DB
    (both DBs, every table) is byte-identical to BEFORE the edit, INCLUDING
    sqlite_sequence. `do_edit(out)` returns the DeferredCommit handle of the edit UNDER
    RESTORE (it must NOT commit it -- the driver commits then restores). `setup(out)`,
    if given, runs + commits a PREREQUISITE edit (e.g. minting the successor a supersede
    points at) BEFORE the baseline fingerprint is taken -- so the prerequisite is part
    of the baseline the restore must PRESERVE, never part of what restore undoes. After
    restore, a subsequent add must reuse the same next id (the PK-reset proof)."""
    out = dw._fresh_db(b)
    try:
        if setup is not None:
            setup(out)
        before_fp = dw._db_fingerprint(out)
        before_seqs = _all_seqs(out)

        handle = _as_handle(do_edit(out))
        commit(handle)                 # the committed write is REAL now
        # The committed write actually changed the DB (sanity: restore has work to do).
        committed_fp = dw._db_fingerprint(out)
        assert committed_fp != before_fp, (
            f"[{label}] the edit did not change the DB before restore -- the test "
            f"would prove nothing (a no-op edit)")

        restore(handle)                # undo the committed write (post-commit failure)

        after_fp = dw._db_fingerprint(out)
        assert after_fp == before_fp, (
            f"[{label}] restore did NOT return the DB to byte-identical; differing "
            f"tables: {sorted(k for k in before_fp if before_fp.get(k) != after_fp.get(k))}")

        after_seqs = _all_seqs(out)
        assert after_seqs == before_seqs, (
            f"[{label}] sqlite_sequence NOT restored: "
            f"{[(k, before_seqs[k], after_seqs[k]) for k in before_seqs if before_seqs[k] != after_seqs[k]]} "
            f"(a PK-autoincrement bump survived the restore)")

        # PK-reset proof: a fresh add after restore gets the SAME next av id a fresh add
        # on a baseline at the SAME pre-edit state gets (the bump the restored edit made
        # was undone). The comparison baseline replays `setup` (e.g. the minted
        # successor) so both DBs start from the identical pre-edit point -- the probe
        # measures the edit's bump being undone, NOT the setup's state.
        clean = dw._fresh_db(b)
        try:
            if setup is not None:
                setup(clean)
            id_restored = _add_and_read_new_av_id(b, out)
            id_clean = _add_and_read_new_av_id(b, clean)
            assert id_restored == id_clean, (
                f"[{label}] after restore the next add got av id {id_restored}, but a "
                f"pre-edit baseline got {id_clean} -- the restore did not reset the PK "
                f"sequence (the prior bump must be discarded)")
        finally:
            shutil.rmtree(clean, ignore_errors=True)
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _add_and_read_new_av_id(b, target):
    """Add a fresh entity to `target` (committed) + return its assigned av id -- the
    next-id probe for the PK-reset proof (reuses 4c's non-function-rva picker)."""
    rva = dw._a_non_function_rva()
    first = {"valid_from_version": GVT, "module": "WHGame.dll",
             "kind": "data_slot", "rva": "0x%08X" % rva}
    r = db_editor.create_entity(target, None, "rp_after_probe", dict(first),
                                version=dw._ver(b), defer_commit=True)
    commit(r["result"])
    kid = r["kcdx_id"]
    con = sqlite3.connect(os.path.join(target, "reference.sqlite"))
    try:
        return con.execute(
            "SELECT id FROM address_versions WHERE kcdx_id = ?", (kid,)).fetchone()[0]
    finally:
        con.close()


# --------------------------------------------------------------------------
# Per-job-shape edit closures (return the DeferredCommit handle, do NOT commit).
# Each mirrors the 4c convergence case's edit, but the COMMIT is the test's, not the
# closure's -- so the restore-point captured on the deferred handle survives to restore.
# --------------------------------------------------------------------------
def _edit_reverify(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = dw._pick_function_trio_row(user_db)
    edits = {"verified_by": "rp_oracle", "verified_date": "2099-12-31",
             "last_verified_at_version": GVT}

    def do(out):
        return db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=dw._ver(b),
            defer_commit=True)
    return do


def _edit_full_column(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = dw._pick_function_trio_row(user_db)
    # A trio change so the present-row branch actually writes (a non-trio cell is a
    # no-op; the trio is the only mutable part -- 4c documents this).
    edits = {"verified_by": "rp_oracle_full", "verified_date": "2098-01-01",
             "last_verified_at_version": GVT}

    def do(out):
        return db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=dw._ver(b),
            defer_commit=True)
    return do


def _edit_create_entity(b):
    rva = dw._a_non_function_rva()
    first = {"valid_from_version": GVT, "module": "WHGame.dll",
             "kind": "data_slot", "rva": "0x%08X" % rva}

    def do(out):
        return db_editor.create_entity(
            out, None, "rp_oracle_entity", dict(first),
            version=dw._ver(b), defer_commit=True)
    return do


def _supersede_setup_and_edit(b):
    """Return (setup, edit) for the supersede shape. The successor Y is minted +
    committed in `setup` (part of the BASELINE the restore preserves); `edit` is the
    supersede of X by Y ALONE -- the only write the restore-point captures + undoes."""
    user_db = os.path.join(b["out"], "reference.sqlite")
    x_kid, _ = dw._first_plain_entity(user_db)
    rva = dw._a_non_function_rva()
    first = {"valid_from_version": GVT, "module": "WHGame.dll",
             "kind": "data_slot", "rva": "0x%08X" % rva}

    def setup(out):
        ry = db_editor.create_entity(out, None, "rp_oracle_succ", dict(first),
                                     version=dw._ver(b), defer_commit=True)
        commit(ry["result"])

    def edit(out):
        return db_editor.supersede_entity(
            out, None, x_kid, "rp_oracle_succ", GVT,
            version=dw._ver(b), defer_commit=True)
    return setup, edit


def _edit_deprecate(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    x_kid, _ = dw._first_plain_entity(user_db)

    def do(out):
        return db_editor.deprecate_entity(
            out, None, x_kid, deprecated_at_version=GVT,
            version=dw._ver(b), defer_commit=True)
    return do


def _edit_create_version_new_tag(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = dw._pick_nonfunction_row(user_db, null_trio=True)
    cols = dw._seed_source_row(user_db, kid, vf_tag)
    cols["signature"] = "void (rp new tag bump)"

    def do(out):
        return db_editor.create_version(
            out, None, kid, NEW_TAG, dict(cols),
            version=(NEW_TAG, 2000000), defer_commit=True)
    return do


# ==========================================================================
# CAPTURE-COMMIT-RESTORE BYTE-IDENTITY -- the load-bearing proof, every job shape.
# ==========================================================================
def _byte_identity_all_shapes(b):
    _capture_commit_restore_is_identical(b, _edit_reverify(b), "re-verify")
    _capture_commit_restore_is_identical(b, _edit_full_column(b), "full-column UPDATE")
    _capture_commit_restore_is_identical(b, _edit_create_entity(b), "create-entity")
    sup_setup, sup_edit = _supersede_setup_and_edit(b)
    _capture_commit_restore_is_identical(b, sup_edit, "supersede", setup=sup_setup)
    _capture_commit_restore_is_identical(b, _edit_deprecate(b), "deprecate")


# ==========================================================================
# THE NEW-TAG RESTORE -- the new game_versions row gone, the prior interval re-opened,
# the new av row gone, sqlite_sequence reset -> byte-identical.
# ==========================================================================
def _new_tag_restore(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = dw._pick_nonfunction_row(user_db, null_trio=True)

    # Whole-DB byte-identity (the load-bearing proof for the new-tag shape).
    _capture_commit_restore_is_identical(
        b, _edit_create_version_new_tag(b), "create-version-at-a-NEW-tag")

    # Plus an explicit structural assertion of the per-row undo (the new gv row gone,
    # the prior interval re-opened, the new av row gone) on a fresh run.
    out = dw._fresh_db(b)
    try:
        cols = dw._seed_source_row(user_db, kid, vf_tag)
        cols["signature"] = "void (rp new tag bump)"
        ret = db_editor.create_version(out, None, kid, NEW_TAG, dict(cols),
                                       version=(NEW_TAG, 2000000), defer_commit=True)
        handle = _as_handle(ret)
        commit(handle)
        restore(handle)

        for which in ("reference.sqlite", "reference-dev.sqlite"):
            con = sqlite3.connect(os.path.join(out, which))
            try:
                gv = {r[0] for r in con.execute("SELECT tag FROM game_versions")}
                assert NEW_TAG not in gv, (
                    f"[{which}] restore left the new game_versions row {NEW_TAG!r}")
                # exactly one OPEN row for the entity again (the prior interval re-opened)
                open_rows = con.execute(
                    "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ? "
                    "AND valid_through IS NULL", (kid,)).fetchone()[0]
                assert open_rows == 1, (
                    f"[{which}] prior interval NOT re-opened: {open_rows} open rows "
                    f"(expected 1)")
                # no av row at the (now-absent) new tag
                n_at_new = con.execute(
                    "SELECT COUNT(*) FROM address_versions av "
                    "JOIN game_versions g ON av.valid_from = g.id "
                    "WHERE av.kcdx_id = ? AND g.tag = ?",
                    (kid, NEW_TAG)).fetchone()[0]
                assert n_at_new == 0, (
                    f"[{which}] restore left an av row at the new tag")
            finally:
                con.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)


# ==========================================================================
# BOTH DBs -- a function-kind add (USER INSERTs the promoted row; DEV promotes an
# EXISTING bulk row IN PLACE -- the shape a seq-boundary-only restore would MISS).
# ==========================================================================
def _both_dbs_function_kind_add(b):
    rva = dw._a_bulk_function_rva()
    assert rva is not None, "no bulk function rva in the dump fixture"
    first = {"valid_from_version": GVT, "module": "WHGame.dll",
             "kind": "function", "rva": "0x%08X" % rva}

    def do(out):
        return db_editor.create_entity(
            out, None, "rp_oracle_fn", dict(first),
            version=dw._ver(b), defer_commit=True)

    _capture_commit_restore_is_identical(b, do, "function-kind add (both DBs)")


# ==========================================================================
# SCOPE IS BOUNDED -- the capture reads O(actions) rows, never a full-table snapshot.
# A structural assertion on the captured row counts: for a single-entity edit the
# capture holds only a handful of rows, NOT the table's hundreds.
# ==========================================================================
def _scope_is_bounded(b):
    out = dw._fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        # The table sizes the capture must NOT scale with (the mini-dump curated set).
        con = sqlite3.connect(user_db)
        try:
            n_av = con.execute("SELECT COUNT(*) FROM address_versions").fetchone()[0]
            n_names = con.execute("SELECT COUNT(*) FROM address_names").fetchone()[0]
        finally:
            con.close()
        assert n_av >= 100 and n_names >= 100, (
            f"fixture too small to prove scope (av={n_av} names={n_names}); the bound "
            f"assertion needs a table materially larger than an edit's touched set")

        kid, vf_tag = dw._pick_function_trio_row(user_db)
        edits = {"verified_by": "rp_scope", "verified_date": "2097-06-06",
                 "last_verified_at_version": GVT}
        handle = db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=dw._ver(b),
            defer_commit=True)

        rp = handle.restore_point
        assert rp is not None, "deferred path captured no restore-point"
        # The captured rows of the per-DB partition (av + survival + names; the tiny
        # game_versions dimension is whole, ~1-2 rows). A single-entity re-verify
        # touches ONE entity, so the captured av+names rows are a small constant, NOT
        # the table size -- the scoped-not-snapshot proof.
        for tag, cap in (("user", rp.user), ("dev", rp.dev)):
            n_cap_av = len(cap["rows"]["address_versions"])
            n_cap_names = len(cap["rows"]["address_names"])
            n_cap_gv = len(cap["rows"]["game_versions"])
            # Bound: the captured av/names rows are tiny (a single entity has 1..few
            # version rows + 1 names row) and FAR below the table size. The game_versions
            # capture is the whole tiny dimension; assert it stays tiny (a few rows).
            assert n_cap_av < 20, (
                f"[{tag}] captured {n_cap_av} av rows for a 1-entity edit -- not "
                f"scoped (table has {n_av}); a full-table snapshot is the rejected "
                f"mechanism (D21)")
            assert n_cap_names < 20, (
                f"[{tag}] captured {n_cap_names} names rows for a 1-entity edit (table "
                f"has {n_names}) -- not scoped")
            assert n_cap_gv < 20, (
                f"[{tag}] captured {n_cap_gv} game_versions rows -- the dimension is "
                f"meant to be tiny; a large count means it is not the small shared "
                f"dimension assumed")
            # Hard proof it is NOT a full-table snapshot: the capture is a small
            # FRACTION of the table.
            assert n_cap_av < n_av, (
                f"[{tag}] the capture is NOT smaller than the table ({n_cap_av} vs "
                f"{n_av}) -- it is a snapshot, not the scoped touched-set")
        rollback(handle)   # clean up the held txn (do not commit the scope probe)
    finally:
        shutil.rmtree(out, ignore_errors=True)


# ==========================================================================
# IDEMPOTENT -- a double restore lands the same byte-identical state.
# ==========================================================================
def _double_restore_is_idempotent(b):
    out = dw._fresh_db(b)
    try:
        before_fp = dw._db_fingerprint(out)
        before_seqs = _all_seqs(out)

        do = _edit_create_entity(b)
        handle = _as_handle(do(out))
        commit(handle)
        restore(handle)
        once_fp = dw._db_fingerprint(out)
        assert once_fp == before_fp, "single restore not byte-identical"

        restore(handle)               # second restore -- must be a no-op in effect
        twice_fp = dw._db_fingerprint(out)
        twice_seqs = _all_seqs(out)
        assert twice_fp == before_fp, (
            "double restore drifted from the byte-identical baseline; differing: "
            f"{sorted(k for k in before_fp if before_fp.get(k) != twice_fp.get(k))}")
        assert twice_seqs == before_seqs, (
            "double restore drifted the sqlite_sequence")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# ==========================================================================
# RESTORE ON A NON-DEFERRED/NO-RESTORE-POINT HANDLE -- a clear error, never silent.
# ==========================================================================
def _restore_without_restore_point_errors(b):
    # The immediate path captures no restore-point; a hand-built handle with
    # restore_point=None must error on restore (a deferred-path-only capability).
    h = imp.DeferredCommit(None, None, {}, b["out"], restore_point=None)
    raised = None
    try:
        restore(h)
    except RuntimeError as e:
        raised = e
    assert raised is not None, (
        "restore() on a handle with no restore-point did not error (it is a "
        "deferred-path-only capability)")


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = dw._get_baseline()
        yield b
        dw._cleanup_baseline()
except ImportError:   # pragma: no cover
    pytest = None


def test_byte_identity_all_shapes(baseline):  # noqa: F811
    _byte_identity_all_shapes(baseline)


def test_new_tag_restore(baseline):  # noqa: F811
    _new_tag_restore(baseline)


def test_both_dbs_function_kind_add(baseline):  # noqa: F811
    _both_dbs_function_kind_add(baseline)


def test_scope_is_bounded(baseline):  # noqa: F811
    _scope_is_bounded(baseline)


def test_double_restore_is_idempotent(baseline):  # noqa: F811
    _double_restore_is_idempotent(baseline)


def test_restore_without_restore_point_errors(baseline):  # noqa: F811
    _restore_without_restore_point_errors(baseline)


if __name__ == "__main__":
    try:
        b = dw._get_baseline()
        _byte_identity_all_shapes(b)
        print("PASS byte-identity: re-verify / full-column / create-entity / "
              "supersede / deprecate")
        _new_tag_restore(b)
        print("PASS new-tag restore (gv row gone, interval re-opened, byte-identical)")
        _both_dbs_function_kind_add(b)
        print("PASS both DBs: function-kind add (DEV in-place promote restored)")
        _scope_is_bounded(b)
        print("PASS scope is bounded (O(actions), not a full-table snapshot)")
        _double_restore_is_idempotent(b)
        print("PASS double restore is idempotent")
        _restore_without_restore_point_errors(b)
        print("PASS restore without a restore-point errors")
        print("\nall restore-point tests passed")
    finally:
        dw._cleanup_baseline()
