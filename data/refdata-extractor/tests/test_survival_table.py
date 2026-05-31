"""test_survival_table.py -- the rebuild SHAPE oracle for the `survival` table
(db-updator Phase 1, step 5.1).

WHAT THIS PROVES
----------------
A rebuild emits the per-kind survival datum correctly:

  - ONE survival row per CURATED address_versions row (1:1), in BOTH the USER and
    DEV DBs (the survival table ships to both -- curated-entity data the engine
    consumer reads at the user tier). No bulk uncurated function gets a survival
    row.
  - The kind_form is correct per address kind:
      function / function_no_sig / function_variadic -> 'function_hash'
      callsite / instruction_anchor                  -> 'aob'
      string_anchor                                  -> 'literal'
      data_slot                                      -> 'derivation'
      vtable_base                                    -> 'table_shape'
      vtable_index                                   -> 'slot_target'
  - A function survival row carries the body fingerprint REUSED from its
    address_versions row: content_hash + length equal the av row's (NULL only
    when the av row itself carries no fingerprint -- a function with no bulk
    baseline; the survival row NEVER forges a hash the av row lacks).
  - A non-function survival row carries its seed survival datum when present and
    an EMPTY payload when not. Today step 5.2 has not filled any seed survival
    column, so EVERY non-function payload (aob / anchor_string / rule /
    slot_count) is empty -- the SHAPE assertion this step locks. As 5.2 fills the
    columns these tighten to value assertions.
  - derives_from is set where the kind has a dependency AND the seed column is
    filled. Today NO seed survival_derives_from is filled, so derives_from is
    empty everywhere -- assert the SHAPE (the column exists, is currently empty).

This is the rebuild-side counterpart of the apply==rebuild survival assertions in
test_apply_add_entity.py (which prove apply writes a survival row byte-identical
to a rebuild's). The apply-equals-rebuild oracle for the survival table at large
is carried by test_rebuild_oracle.py (the per-table hash now includes `survival`).

RUN
---
    python tests/test_survival_table.py
    pytest tests/test_survival_table.py

Needs the local refdata-1.5.1164953 dump; one rebuild takes tens of seconds.
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
import seeds_shared as ss       # noqa: E402,F401

# kind -> expected survival kind_form (the single shared mapping under test).
_EXPECTED_FORM = {
    "function":           "function_hash",
    "function_no_sig":    "function_hash",
    "function_variadic":  "function_hash",
    "callsite":           "aob",
    "instruction_anchor": "aob",
    "string_anchor":      "literal",
    "data_slot":          "derivation",
    "vtable_base":        "table_shape",
    "vtable_index":       "slot_target",
}

# The payload columns NOT used by function_hash (must be NULL on a function row).
_NON_FUNCTION_PAYLOAD = ("aob", "anchor_string", "rule", "slot_count")


# --------------------------------------------------------------------------
# Module-scoped rebuild fixture: one rebuild into a temp dir, reused by every
# assertion (a rebuild is ~tens of seconds; do it once).
# --------------------------------------------------------------------------
_REBUILD = {}


def _get_rebuild():
    if "out" not in _REBUILD:
        if not os.path.isdir(DUMP_DIR):
            raise SystemExit(f"dump dir not found: {DUMP_DIR}")
        out = tempfile.mkdtemp(prefix="survival_oracle_")
        _REBUILD["out"] = out
        imp.run_rebuild(DUMP_DIR, out)
    return _REBUILD["out"]


def _cleanup_rebuild():
    out = _REBUILD.get("out")
    if out:
        import shutil
        shutil.rmtree(out, ignore_errors=True)
        _REBUILD.clear()


try:
    import pytest

    @pytest.fixture(scope="module")
    def rebuilt():
        out = _get_rebuild()
        yield out
        _cleanup_rebuild()
except ImportError:   # pragma: no cover
    pytest = None


# --------------------------------------------------------------------------
# Assertions (each takes the rebuilt out_dir).
# --------------------------------------------------------------------------
def _decoded_kind_by_av(con):
    """{address_versions.id: kind STRING} for the curated rows, decoding the
    dict-encoded kind column via _dict_address_versions_kind."""
    kdict = {r[0]: r[1] for r in con.execute(
        'SELECT id, val FROM "_dict_address_versions_kind"')}
    return {r[0]: kdict.get(r[1]) for r in con.execute(
        "SELECT id, kind FROM address_versions WHERE kcdx_id IS NOT NULL")}


def _one_to_one_with_curated(out):
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            n_sv = con.execute("SELECT COUNT(*) FROM survival").fetchone()[0]
            n_cur = con.execute(
                "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL"
            ).fetchone()[0]
            assert n_sv == n_cur, (
                f"[{which}] survival rows {n_sv} != curated address_versions "
                f"{n_cur} (must be 1:1)")
            # Every survival row points at a CURATED av row (none at a bulk row).
            bad = con.execute(
                "SELECT COUNT(*) FROM survival s JOIN address_versions a "
                "ON s.address_version_id = a.id WHERE a.kcdx_id IS NULL"
            ).fetchone()[0]
            assert bad == 0, (
                f"[{which}] {bad} survival row(s) point at a bulk (uncurated) "
                f"address_versions row")
            # Every curated av row has exactly one survival row.
            uncovered = con.execute(
                "SELECT COUNT(*) FROM address_versions a WHERE a.kcdx_id IS NOT NULL "
                "AND NOT EXISTS (SELECT 1 FROM survival s "
                "WHERE s.address_version_id = a.id)"
            ).fetchone()[0]
            assert uncovered == 0, (
                f"[{which}] {uncovered} curated av row(s) have no survival row")
        finally:
            con.close()


def _kind_form_correct(out):
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            kind_by_av = _decoded_kind_by_av(con)
            rows = con.execute(
                "SELECT address_version_id, kind_form FROM survival").fetchall()
            for av_id, form in rows:
                kind = kind_by_av.get(av_id)
                assert kind is not None, (
                    f"[{which}] survival av_id={av_id} maps to no curated kind")
                exp = _EXPECTED_FORM.get(kind)
                assert exp is not None, (
                    f"[{which}] address kind {kind!r} has no expected survival "
                    f"form in the test map (a new kind needs one)")
                assert form == exp, (
                    f"[{which}] av_id={av_id} kind={kind!r}: kind_form={form!r} "
                    f"!= expected {exp!r}")
        finally:
            con.close()


def _function_rows_carry_hash(out):
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            # A function survival row's content_hash + length equal its av row's;
            # NULL only when the av row itself is NULL (never forged).
            forged = con.execute(
                "SELECT COUNT(*) FROM survival s JOIN address_versions a "
                "ON s.address_version_id = a.id WHERE s.kind_form='function_hash' "
                "AND s.content_hash IS NULL AND a.content_hash IS NOT NULL"
            ).fetchone()[0]
            assert forged == 0, (
                f"[{which}] {forged} function survival row(s) dropped a hash the "
                f"av row carries")
            mism = con.execute(
                "SELECT COUNT(*) FROM survival s JOIN address_versions a "
                "ON s.address_version_id = a.id WHERE s.kind_form='function_hash' "
                "AND s.content_hash IS NOT NULL AND s.content_hash != a.content_hash"
            ).fetchone()[0]
            assert mism == 0, (
                f"[{which}] {mism} function survival hash != av hash")
            lmism = con.execute(
                "SELECT COUNT(*) FROM survival s JOIN address_versions a "
                "ON s.address_version_id = a.id WHERE s.kind_form='function_hash' "
                "AND IFNULL(s.length,-1) != IFNULL(a.length,-1)"
            ).fetchone()[0]
            assert lmism == 0, (
                f"[{which}] {lmism} function survival length != av length")
            # At least SOME function rows actually carry a hash (the reuse path is
            # exercised, not vacuously all-NULL).
            with_hash = con.execute(
                "SELECT COUNT(*) FROM survival WHERE kind_form='function_hash' "
                "AND content_hash IS NOT NULL").fetchone()[0]
            assert with_hash > 0, (
                f"[{which}] no function survival row carries a content_hash "
                f"(the fingerprint-reuse path looks dead)")
            # A function row uses ONLY content_hash + length -- the other payload
            # columns stay NULL.
            for pc in _NON_FUNCTION_PAYLOAD:
                bad = con.execute(
                    f'SELECT COUNT(*) FROM survival WHERE kind_form=\'function_hash\' '
                    f'AND "{pc}" IS NOT NULL').fetchone()[0]
                assert bad == 0, (
                    f"[{which}] {bad} function survival row(s) have non-empty "
                    f"{pc} (function_hash uses only content_hash+length)")
        finally:
            con.close()


def _nonfunction_payload_empty_today(out):
    """Step 5.1 SHAPE assertion: with step 5.2 unfilled, EVERY non-function
    survival payload is empty, and NO survival row carries a function fingerprint
    on a non-function form. (This tightens to value assertions as 5.2 fills.)"""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            nonempty = con.execute(
                "SELECT COUNT(*) FROM survival WHERE kind_form != 'function_hash' "
                "AND (aob IS NOT NULL OR anchor_string IS NOT NULL "
                "OR rule IS NOT NULL OR slot_count IS NOT NULL)"
            ).fetchone()[0]
            assert nonempty == 0, (
                f"[{which}] {nonempty} non-function survival row(s) have a "
                f"non-empty payload, but step 5.2 has filled no seed survival "
                f"column -- a value appeared from nowhere (guessed or notes-parsed?)")
            fp_on_nonfn = con.execute(
                "SELECT COUNT(*) FROM survival WHERE kind_form != 'function_hash' "
                "AND (content_hash IS NOT NULL OR length IS NOT NULL)"
            ).fetchone()[0]
            assert fp_on_nonfn == 0, (
                f"[{which}] {fp_on_nonfn} non-function survival row(s) carry a "
                f"function fingerprint (content_hash/length is function_hash-only)")
        finally:
            con.close()


def _derives_from_shape(out):
    """Step 5.1 SHAPE assertion: derives_from is set where the kind has a
    dependency AND the seed column is filled. Today NO survival_derives_from is
    filled, so derives_from is empty everywhere -- assert the column exists and is
    currently all-NULL (the SHAPE; it carries a real av-id once 5.2 fills the
    seed)."""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            cols = [c[1] for c in con.execute('PRAGMA table_info("survival")')]
            assert "derives_from" in cols, (
                f"[{which}] survival table has no derives_from column")
            n_set = con.execute(
                "SELECT COUNT(*) FROM survival WHERE derives_from IS NOT NULL"
            ).fetchone()[0]
            assert n_set == 0, (
                f"[{which}] {n_set} survival row(s) have derives_from set, but no "
                f"seed survival_derives_from is filled yet (step 5.2)")
        finally:
            con.close()


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_survival_one_to_one_with_curated(rebuilt):  # noqa: F811
    _one_to_one_with_curated(rebuilt)


def test_survival_kind_form_correct(rebuilt):  # noqa: F811
    _kind_form_correct(rebuilt)


def test_survival_function_rows_carry_hash(rebuilt):  # noqa: F811
    _function_rows_carry_hash(rebuilt)


def test_survival_nonfunction_payload_empty_today(rebuilt):  # noqa: F811
    _nonfunction_payload_empty_today(rebuilt)


def test_survival_derives_from_shape(rebuilt):  # noqa: F811
    _derives_from_shape(rebuilt)


if __name__ == "__main__":
    try:
        out = _get_rebuild()
        _one_to_one_with_curated(out)
        print("PASS test_survival_one_to_one_with_curated")
        _kind_form_correct(out)
        print("PASS test_survival_kind_form_correct")
        _function_rows_carry_hash(out)
        print("PASS test_survival_function_rows_carry_hash")
        _nonfunction_payload_empty_today(out)
        print("PASS test_survival_nonfunction_payload_empty_today")
        _derives_from_shape(out)
        print("PASS test_survival_derives_from_shape")
        print("\nall survival-table oracle tests passed")
    finally:
        _cleanup_rebuild()
