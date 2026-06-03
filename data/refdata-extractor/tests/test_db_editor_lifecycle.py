"""test_db_editor_lifecycle.py -- the db_editor lifecycle UPDATE entry points
(maintainer-tool Phase 1, step 5): supersede_entity (Job 4 / US-8) +
deprecate_entity (Job 5 / US-8).

WHAT THIS PROVES
----------------
db_editor.supersede_entity / deprecate_entity are the headless, in-process entry
points the GUI calls to land an entity-level lifecycle edit on an EXISTING
address_names row -- they drive the EXISTING validated atomic applier
(import_to_sqlite.apply_seeds, design D13) over a prospective seed they build by
exporting the current DB + folding the lifecycle cells into the matched names row.
This test exercises the REAL db_editor -> REAL apply -> REAL reference DBs on the
mini-dump fixture (no wrapper, no stubbed gate). Cases:

  1. VALID SUPERSEDE: set superseded_by + superseded_at_version TOGETHER on an
     existing entity X (its successor Y is a fresh data_slot entity minted via the
     landed create_entity path). Both lifecycle columns land in BOTH DBs; the
     edge resolves to Y's kcdx_id (the validator resolves the name -> id).

  2. VALID DEPRECATE: set is_deprecated + deprecated_at_version (+ optional
     deprecation_replacement) TOGETHER on an existing plain entity. is_deprecated
     + deprecated_at_version land in BOTH DBs; the optional replacement (a real
     successor name) resolves and lands.

  3. PARTIAL-PAIR ABORTS WITH NO WRITE -- one column of a both-or-neither pair set
     alone -> the shared validator rejects the prospective names seed (RuntimeError
     out of apply_seeds before any DB open) and BOTH DBs are byte-identical to the
     pre-action snapshot. Four sub-cases: supersede with only superseded_by,
     supersede with only superseded_at_version, deprecate with only is_deprecated,
     deprecate with only deprecated_at_version.

  4. SELF-SUPERSEDE ABORTS WITH NO WRITE: an entity superseded_by ITS OWN name ->
     the validator's no-self-supersede HARD ERROR; both DBs byte-identical.

  5. SUPERSESSION CYCLE ABORTS WITH NO WRITE: X -> Y and Y -> X (a 2-node cycle) ->
     the validator's check_supersession_acyclic HARD ERROR; both DBs byte-identical.
     (X's edge is set first and lands; the cycle-closing Y -> X edit is the one
     that must abort, leaving the DB unchanged from BEFORE the closing edit.)

  6. REPLACEMENT-WITHOUT-DEPRECATED ABORTS WITH NO WRITE: deprecation_replacement
     set while is_deprecated is NOT set -> the validator's
     replacement-requires-deprecated HARD ERROR; both DBs byte-identical.

WHY THE BRIDGE IS SOUND (no separate write/validate path -- D13)
----------------------------------------------------------------
supersede_entity / deprecate_entity write NOTHING under data/seeds/ -- each exports
the current DB to a TEMP seed dir, folds the lifecycle cells into the matched
address_names row there (seed_csv_edit.update_row_in_place keyed on id, diff-
preserved), and drives apply_seeds with the importer's seed-path constants pointed
at the temp dir (the round_trip.py / apply-oracle convention). The pair-integrity,
no-self-supersede, acyclicity, and replacement-requires-deprecated rules are the
shared validator's HARD ERRORs (resolve_and_check_name_refs +
check_supersession_acyclic, run over the FULL prospective names seed BEFORE any DB
open) -- this test asserts the post-state + the no-write-on-invalid, NEVER a
reimplemented rule. The entity identity (id, name) is never mutated.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the db_editor INSERT test harness wholesale (test_db_editor_insert.py): the
module-scoped baseline rebuild (built ONCE off the committed seeds + the mini-dump
excerpt), the per-test fresh DB copy, the _snapshot byte-identity fingerprint, the
_names_row / _all_names_ids readers, and _a_non_function_rva (a NON-function rva so
a minted successor entity does not trip the function-kind baseline gate -- that has
its own oracle). create_entity (landed step 4) mints the successor Y.

RUN
---
    python tests/test_db_editor_lifecycle.py
    pytest tests/test_db_editor_lifecycle.py
"""
import os
import shutil
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Reuse the INSERT test harness verbatim: seed-dir pointing, the module-scoped
# baseline, the fresh-DB copy, the snapshot/byte-identity fingerprint, the DB
# readers, the non-function rva picker, and the import of db_editor + GVT.
import test_db_editor_insert as base  # noqa: E402
from test_db_editor_insert import (  # noqa: E402,F401
    db_editor, DLL_PATH, GVT,
    _fresh_db, _snapshot, _names_row, _all_names_ids, _a_non_function_rva,
    _get_baseline, _cleanup_baseline,
)


# --------------------------------------------------------------------------
# Edit-target pickers from the BUILT DB (use whatever the fixture carries).
# --------------------------------------------------------------------------
def _first_plain_entity(db_path):
    """The (id, name) of the first address_names entity that is NOT already
    deprecated or superseded -- a clean entity to supersede/deprecate in the edit."""
    import sqlite3
    con = sqlite3.connect(db_path)
    try:
        for kid, name, sb, dep in con.execute(
                "SELECT id, name, superseded_by, is_deprecated FROM address_names "
                "ORDER BY id"):
            if sb is None and not dep:
                return kid, name
        return None
    finally:
        con.close()


def _names_id_for_name(db_path, name):
    import sqlite3
    con = sqlite3.connect(db_path)
    try:
        row = con.execute("SELECT id FROM address_names WHERE name = ?",
                          (name,)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


_Y_NAME = "lifecycle_oracle_successor"


def _mint_successor(out_dir, name=_Y_NAME):
    """Mint a fresh successor entity Y (a non-function data_slot at a non-function
    rva, so create_entity mints it without the function-kind baseline gate -- the
    same shape the apply deprecate/supersede oracle uses for its successor). Returns
    the assigned kcdx_id. Used so the supersede edge points at a REAL existing
    successor (an unresolvable name would be a different validator error)."""
    rva = _a_non_function_rva()
    ret = db_editor.create_entity(
        out_dir, DLL_PATH, name,
        first_version_columns={
            "valid_from_version": GVT,
            "module": "WHGame.dll",
            "kind": "data_slot",
            "rva": "0x%08X" % rva,
        })
    return ret["kcdx_id"]


# --------------------------------------------------------------------------
# Case 1: valid supersede sets both columns atomically (both DBs).
# --------------------------------------------------------------------------
def _valid_supersede(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        x_kid, _x_name = _first_plain_entity(user_db)
        assert x_kid is not None, "no plain entity to supersede in the fixture"

        # Mint the successor Y so superseded_by resolves to a real entity.
        y_kid = _mint_successor(out)

        ret = db_editor.supersede_entity(out, DLL_PATH, x_kid, _Y_NAME, GVT)
        assert ret["action"] == "supersede", \
            f"action != 'supersede': {ret['action']!r}"
        assert ret["kcdx_id"] == x_kid
        # NOT AP18-gated -- an UPDATE to an approved entity is not a new row.
        assert "ap18_new_row" not in ret, \
            "supersede (an UPDATE) wrongly carried the AP18 new-row flag"

        for label, dbp in (("user", user_db), ("dev", dev_db)):
            row = _names_row(dbp, x_kid)
            assert row is not None, f"[{label}] X names row vanished"
            # superseded_by stored as the successor's id; superseded_at_version as
            # the game_versions id -- both SET together (the pair landed).
            assert row["superseded_by"] == y_kid, (
                f"[{label}] superseded_by={row['superseded_by']!r} "
                f"!= successor id {y_kid}")
            assert row["superseded_at_version"] is not None, \
                f"[{label}] superseded_at_version not set with superseded_by"
            # Identity untouched.
            assert row["id"] == x_kid and row["name"] == _x_name, \
                f"[{label}] entity identity mutated: {row}"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: valid deprecate sets is_deprecated + deprecated_at_version (+ optional
# replacement) atomically (both DBs).
# --------------------------------------------------------------------------
def _valid_deprecate(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        x_kid, _x_name = _first_plain_entity(user_db)
        assert x_kid is not None, "no plain entity to deprecate in the fixture"

        # Mint a real replacement entity so deprecation_replacement (a NAME)
        # resolves -- the optional advisory pointer, allowed only when deprecated.
        repl_kid = _mint_successor(out, name="lifecycle_oracle_replacement")

        ret = db_editor.deprecate_entity(
            out, DLL_PATH, x_kid,
            deprecated_at_version=GVT,
            deprecation_replacement="lifecycle_oracle_replacement")
        assert ret["action"] == "deprecate", \
            f"action != 'deprecate': {ret['action']!r}"
        assert ret["kcdx_id"] == x_kid
        assert "ap18_new_row" not in ret, \
            "deprecate (an UPDATE) wrongly carried the AP18 new-row flag"

        for label, dbp in (("user", user_db), ("dev", dev_db)):
            row = _names_row(dbp, x_kid)
            assert row is not None, f"[{label}] X names row vanished"
            assert row["is_deprecated"], \
                f"[{label}] is_deprecated not set: {row['is_deprecated']!r}"
            assert row["deprecated_at_version"] is not None, \
                f"[{label}] deprecated_at_version not set with is_deprecated"
            assert row["deprecation_replacement"] == repl_kid, (
                f"[{label}] deprecation_replacement={row['deprecation_replacement']!r}"
                f" != replacement id {repl_kid}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: a partial both-or-neither pair aborts with NO write.
# --------------------------------------------------------------------------
def _partial_pair_aborts(b):
    # Each entry drives ONE column of a both-or-neither lifecycle pair, leaving the
    # other unset -> the shared validator's pair-integrity HARD ERROR (RuntimeError
    # out of apply_seeds, no DB open/write). The successor name for the lone
    # superseded_by sub-case is a REAL entity so the rejection is the PAIR rule, not
    # an unresolvable-name error.
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        x_kid, _ = _first_plain_entity(user_db)
        y_kid = _mint_successor(out)   # so superseded_by resolves (isolate the pair rule)

        cases = [
            # (label, callable building the edit on the SAME (out, x_kid))
            ("supersede with only superseded_by",
             lambda: db_editor.supersede_entity(out, DLL_PATH, x_kid, _Y_NAME, "")),
            ("supersede with only superseded_at_version",
             lambda: db_editor.supersede_entity(out, DLL_PATH, x_kid, "", GVT)),
            ("deprecate with only is_deprecated",
             lambda: db_editor.deprecate_entity(
                 out, DLL_PATH, x_kid, is_deprecated=True,
                 deprecated_at_version=None)),
            ("deprecate with only deprecated_at_version",
             lambda: db_editor.deprecate_entity(
                 out, DLL_PATH, x_kid, is_deprecated=False,
                 deprecated_at_version=GVT)),
        ]
        for label, do_edit in cases:
            snap = _snapshot(out)
            raised = None
            try:
                do_edit()
            except (RuntimeError, db_editor.DbEditError) as e:
                raised = e
            assert raised is not None, f"[{label}] did not abort"
            assert isinstance(raised, RuntimeError), (
                f"[{label}] raised {type(raised).__name__}, expected the "
                f"validator's RuntimeError: {raised}")
            assert _snapshot(out) == snap, (
                f"[{label}] a DB changed despite the partial-pair abort "
                f"(no write expected)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: a self-supersede aborts with NO write.
# --------------------------------------------------------------------------
def _self_supersede_aborts(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        x_kid, x_name = _first_plain_entity(user_db)

        snap = _snapshot(out)
        raised = None
        try:
            # X superseded_by ITS OWN name -> the validator's no-self-supersede
            # HARD ERROR (both columns set, so it is NOT a pair-integrity failure).
            db_editor.supersede_entity(out, DLL_PATH, x_kid, x_name, GVT)
        except (RuntimeError, db_editor.DbEditError) as e:
            raised = e
        assert raised is not None, "self-supersede did not abort"
        assert isinstance(raised, RuntimeError), (
            f"self-supersede raised {type(raised).__name__}, expected the "
            f"validator's RuntimeError: {raised}")
        assert _snapshot(out) == snap, \
            "a DB changed despite the self-supersede abort (no write expected)"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 5: a supersession cycle aborts with NO write.
# --------------------------------------------------------------------------
def _cycle_aborts(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        x_kid, x_name = _first_plain_entity(user_db)
        # Mint Y, then make X -> Y (this first edge is VALID and lands).
        y_kid = _mint_successor(out)
        db_editor.supersede_entity(out, DLL_PATH, x_kid, _Y_NAME, GVT)

        # Snapshot AFTER the valid X -> Y edge: the cycle-closing edit must leave the
        # DB byte-identical to THIS state (no write on the rejected closing edit).
        snap = _snapshot(out)
        raised = None
        try:
            # Y -> X closes the 2-node cycle -> check_supersession_acyclic HARD ERROR.
            db_editor.supersede_entity(out, DLL_PATH, y_kid, x_name, GVT)
        except (RuntimeError, db_editor.DbEditError) as e:
            raised = e
        assert raised is not None, "supersession cycle did not abort"
        assert isinstance(raised, RuntimeError), (
            f"cycle raised {type(raised).__name__}, expected the validator's "
            f"RuntimeError: {raised}")
        assert _snapshot(out) == snap, \
            "a DB changed despite the cycle abort (no write expected)"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 6: a deprecation_replacement set without is_deprecated aborts with NO write.
# --------------------------------------------------------------------------
def _replacement_without_deprecated_aborts(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        x_kid, _ = _first_plain_entity(user_db)
        # Mint a real replacement so the rejection is the requires-deprecated rule,
        # not an unresolvable-name error.
        _mint_successor(out, name="lifecycle_oracle_replacement")

        snap = _snapshot(out)
        raised = None
        try:
            # deprecation_replacement set while is_deprecated is FALSE ->
            # replacement-requires-deprecated HARD ERROR.
            db_editor.deprecate_entity(
                out, DLL_PATH, x_kid, is_deprecated=False,
                deprecation_replacement="lifecycle_oracle_replacement")
        except (RuntimeError, db_editor.DbEditError) as e:
            raised = e
        assert raised is not None, "replacement-without-deprecated did not abort"
        assert isinstance(raised, RuntimeError), (
            f"replacement-without-deprecated raised {type(raised).__name__}, "
            f"expected the validator's RuntimeError: {raised}")
        assert _snapshot(out) == snap, (
            "a DB changed despite the replacement-without-deprecated abort "
            "(no write expected)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points (reuse the insert module's `baseline` fixture).
# --------------------------------------------------------------------------
try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


def test_valid_supersede_sets_pair_atomically(baseline):  # noqa: F811
    _valid_supersede(baseline)


def test_valid_deprecate_sets_pair_atomically(baseline):  # noqa: F811
    _valid_deprecate(baseline)


def test_partial_pair_aborts_with_no_write(baseline):  # noqa: F811
    _partial_pair_aborts(baseline)


def test_self_supersede_aborts_with_no_write(baseline):  # noqa: F811
    _self_supersede_aborts(baseline)


def test_supersession_cycle_aborts_with_no_write(baseline):  # noqa: F811
    _cycle_aborts(baseline)


def test_replacement_without_deprecated_aborts_with_no_write(baseline):  # noqa: F811
    _replacement_without_deprecated_aborts(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _valid_supersede(b)
        print("PASS test_valid_supersede_sets_pair_atomically")
        _valid_deprecate(b)
        print("PASS test_valid_deprecate_sets_pair_atomically")
        _partial_pair_aborts(b)
        print("PASS test_partial_pair_aborts_with_no_write")
        _self_supersede_aborts(b)
        print("PASS test_self_supersede_aborts_with_no_write")
        _cycle_aborts(b)
        print("PASS test_supersession_cycle_aborts_with_no_write")
        _replacement_without_deprecated_aborts(b)
        print("PASS test_replacement_without_deprecated_aborts_with_no_write")
        print("\nall db_editor lifecycle oracle tests passed")
    finally:
        _cleanup_baseline()
