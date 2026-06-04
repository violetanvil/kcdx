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
    an EMPTY payload when not. Step 5.2 filled 14 rows: callsite (5-8) carry an
    aob + expect_unique=1; string_anchor (12) carries an anchor_string +
    expect_unique=1; instruction_anchor (9) carries an aob + derives_from +
    expect_unique=1; data_slot (10/11/132) carry a rule + derives_from;
    vtable_base (119/138/139/140) carry a slot_count. The vtable_index rows
    (19-24) stay EMPTY (deferred on the runtime-vtable path). This step asserts
    the filled kinds are non-empty and the deferred kind is still empty.
  - derives_from is set where the kind has a dependency AND the seed column is
    filled: the data_slot -> instruction_anchor -> string_anchor chain (10->9,
    11->10, 132->11, 9->12). vtable_index's base ref is still unfilled, so its
    derives_from stays empty.
  - expect_unique is set (=1) on the search-locating kinds the handoff verified
    unique (callsite 5-8, instruction_anchor 9, string_anchor 12) and NULL on
    every other kind.

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
# Runs against the small committed REAL dump excerpt (tests/fixtures/mini-dump/,
# built by make_mini_dump.py) for a fast rebuild; full-dump fidelity is covered
# by test_rebuild_oracle.py.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))

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


def _sv_by_kid(con):
    """{kcdx_id: survival-row dict} for the OPEN curated rows, joining survival to
    its address_versions row."""
    out = {}
    for row in con.execute(
            "SELECT a.kcdx_id, s.kind_form, s.aob, s.anchor_string, s.rule, "
            "s.slot_count, s.expect_unique, s.derives_from, s.content_hash, "
            "s.length FROM survival s JOIN address_versions a "
            "ON s.address_version_id = a.id "
            "WHERE a.kcdx_id IS NOT NULL AND a.valid_through IS NULL"):
        out[row[0]] = {
            "kind_form": row[1], "aob": row[2], "anchor_string": row[3],
            "rule": row[4], "slot_count": row[5], "expect_unique": row[6],
            "derives_from": row[7], "content_hash": row[8], "length": row[9]}
    return out


def _filled_kinds_carry_values(out):
    """Step 5.2 value assertions: the 14 filled rows carry their seed datum, and
    NO non-function row carries a function fingerprint. The vtable_index rows
    (19-24) stay EMPTY (deferred). NEVER assert a guessed value -- just non-empty
    for the filled kinds + empty for the deferred kind."""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            sv = _sv_by_kid(con)
            # callsite (aob form): aob present.
            for kid in (5, 6, 7, 8):
                assert sv[kid]["aob"], f"[{which}] callsite {kid} has empty aob"
            # instruction_anchor (aob form): aob present + derives_from set.
            assert sv[9]["aob"], f"[{which}] instruction_anchor 9 has empty aob"
            assert sv[9]["derives_from"] is not None, (
                f"[{which}] instruction_anchor 9 has no derives_from")
            # string_anchor (literal form): anchor_string present.
            assert sv[12]["anchor_string"], (
                f"[{which}] string_anchor 12 has empty anchor_string")
            # data_slot (derivation form): rule + derives_from set.
            for kid in (10, 11, 132):
                assert sv[kid]["rule"], f"[{which}] data_slot {kid} has empty rule"
                assert sv[kid]["derives_from"] is not None, (
                    f"[{which}] data_slot {kid} has no derives_from")
            # vtable_base (table_shape form): slot_count set.
            for kid in (119, 138, 139, 140):
                assert sv[kid]["slot_count"] is not None, (
                    f"[{which}] vtable_base {kid} has empty slot_count")
            # vtable_index (slot_target form): still fully EMPTY (deferred).
            for kid in (19, 20, 21, 22, 23, 24):
                r = sv[kid]
                assert r["kind_form"] == "slot_target", (
                    f"[{which}] vtable_index {kid} kind_form={r['kind_form']!r}")
                assert (r["aob"] is None and r["anchor_string"] is None
                        and r["rule"] is None and r["slot_count"] is None
                        and r["expect_unique"] is None
                        and r["derives_from"] is None), (
                    f"[{which}] vtable_index {kid} has a non-empty payload "
                    f"(deferred kind must stay empty): {r}")
            # No non-function row carries a function fingerprint.
            fp_on_nonfn = con.execute(
                "SELECT COUNT(*) FROM survival WHERE kind_form != 'function_hash' "
                "AND (content_hash IS NOT NULL OR length IS NOT NULL)"
            ).fetchone()[0]
            assert fp_on_nonfn == 0, (
                f"[{which}] {fp_on_nonfn} non-function survival row(s) carry a "
                f"function fingerprint (content_hash/length is function_hash-only)")
        finally:
            con.close()


def _expect_unique_set_on_unique_locators(out):
    """Step 5.2: expect_unique=1 on the search-locating kinds the handoff verified
    .text-unique (callsite 5-8, instruction_anchor 9, string_anchor 12), and NULL
    on every other survival row."""
    unique_kids = {5, 6, 7, 8, 9, 12}
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            sv = _sv_by_kid(con)
            for kid in unique_kids:
                assert sv[kid]["expect_unique"] == 1, (
                    f"[{which}] kid {kid} expect_unique="
                    f"{sv[kid]['expect_unique']!r}, expected 1")
            for kid, r in sv.items():
                if kid not in unique_kids:
                    assert r["expect_unique"] is None, (
                        f"[{which}] kid {kid} ({r['kind_form']}) carries "
                        f"expect_unique={r['expect_unique']!r}, expected NULL")
            # Exactly 6 survival rows carry expect_unique.
            n = con.execute(
                "SELECT COUNT(*) FROM survival WHERE expect_unique IS NOT NULL"
            ).fetchone()[0]
            assert n == 6, (
                f"[{which}] {n} survival rows carry expect_unique, expected 6")
        finally:
            con.close()


def _av_folded_columns_present(out):
    """schema-flatten-survival-fold Phase 1 step 1: the six folded survival
    columns exist on address_versions with the right SQL types after a rebuild,
    in BOTH DBs (they ship to USER + DEV). ADDITIVE first move -- this step adds
    the columns and asserts they are NULL on every row (no populate logic yet;
    step 2 fills them). The SQL types match the former `survival` SCHEMA entry's
    same-named columns (aob/anchor_string/rule TEXT; slot_count/expect_unique/
    derives_from INTEGER)."""
    expected_types = {
        "aob": "TEXT", "anchor_string": "TEXT", "rule": "TEXT",
        "slot_count": "INTEGER", "expect_unique": "INTEGER",
        "derives_from": "INTEGER",
    }
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            info = {c[1]: c[2] for c in con.execute(
                'PRAGMA table_info("address_versions")')}
            for col, ty in expected_types.items():
                assert col in info, (
                    f"[{which}] address_versions has no {col!r} column "
                    f"(the fold did not add it)")
                assert info[col] == ty, (
                    f"[{which}] address_versions.{col} type={info[col]!r} "
                    f"!= expected {ty!r} (must match survival's SQL type)")
            # Additive invariant for THIS step: every folded cell is NULL (no
            # populate logic yet -- step 2). A non-NULL cell here means a
            # populate path leaked in early.
            for col in expected_types:
                n = con.execute(
                    f'SELECT COUNT(*) FROM address_versions '
                    f'WHERE "{col}" IS NOT NULL').fetchone()[0]
                assert n == 0, (
                    f"[{which}] {n} address_versions row(s) have a non-NULL "
                    f"{col} (this additive step populates none)")
        finally:
            con.close()


def _derives_from_chain(out):
    """Step 5.2: derives_from walks the survival DAG the handoff authored --
    data_slot 132 -> 11 -> 10 -> instruction_anchor 9 -> string_anchor 12. Each
    survival.derives_from FK resolves to the dependency entity's curated
    address_versions row, which back-maps to the expected kcdx_id. Exactly 4
    survival rows carry a derives_from (9, 10, 11, 132)."""
    expected = {9: 12, 10: 9, 11: 10, 132: 11}
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            cols = [c[1] for c in con.execute('PRAGMA table_info("survival")')]
            assert "derives_from" in cols, (
                f"[{which}] survival table has no derives_from column")
            av_to_kid = {r[0]: r[1] for r in con.execute(
                "SELECT id, kcdx_id FROM address_versions "
                "WHERE kcdx_id IS NOT NULL")}
            sv = _sv_by_kid(con)
            for kid, dep_kid in expected.items():
                dfrom = sv[kid]["derives_from"]
                assert dfrom is not None, (
                    f"[{which}] kid {kid} has no derives_from (expected -> "
                    f"{dep_kid})")
                assert av_to_kid.get(dfrom) == dep_kid, (
                    f"[{which}] kid {kid} derives_from av {dfrom} -> kid "
                    f"{av_to_kid.get(dfrom)}, expected kid {dep_kid}")
            n_set = con.execute(
                "SELECT COUNT(*) FROM survival WHERE derives_from IS NOT NULL"
            ).fetchone()[0]
            assert n_set == len(expected), (
                f"[{which}] {n_set} survival row(s) have derives_from set, "
                f"expected {len(expected)} (the 9/10/11/132 chain)")
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


def test_survival_filled_kinds_carry_values(rebuilt):  # noqa: F811
    _filled_kinds_carry_values(rebuilt)


def test_survival_expect_unique_set_on_unique_locators(rebuilt):  # noqa: F811
    _expect_unique_set_on_unique_locators(rebuilt)


def test_survival_derives_from_chain(rebuilt):  # noqa: F811
    _derives_from_chain(rebuilt)


def test_av_folded_columns_present(rebuilt):  # noqa: F811
    _av_folded_columns_present(rebuilt)


if __name__ == "__main__":
    try:
        out = _get_rebuild()
        _one_to_one_with_curated(out)
        print("PASS test_survival_one_to_one_with_curated")
        _kind_form_correct(out)
        print("PASS test_survival_kind_form_correct")
        _function_rows_carry_hash(out)
        print("PASS test_survival_function_rows_carry_hash")
        _filled_kinds_carry_values(out)
        print("PASS test_survival_filled_kinds_carry_values")
        _expect_unique_set_on_unique_locators(out)
        print("PASS test_survival_expect_unique_set_on_unique_locators")
        _derives_from_chain(out)
        print("PASS test_survival_derives_from_chain")
        _av_folded_columns_present(out)
        print("PASS test_av_folded_columns_present")
        print("\nall survival-table oracle tests passed")
    finally:
        _cleanup_rebuild()
