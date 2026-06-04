"""test_survival_table.py -- the rebuild SHAPE oracle for the FOLDED survival/re-find
columns on address_versions (D22 / design §11.2; schema-flatten-survival-fold Phase 3).

WHAT THIS PROVES
----------------
The former `survival` sibling table is folded onto `address_versions` and DELETED
(D22 / design §11.2): the six re-find cells (aob/anchor_string/rule/slot_count/
expect_unique/derives_from) are first-class nullable columns ON the curated av row,
and the body fingerprint (content_hash/length) stays on the av row as before. A
rebuild emits the per-kind folded datum correctly -- the same assertions the old
survival-table oracle carried, repointed to the av columns now that the sibling
table is gone:

  - The six folded columns exist on address_versions with the right SQL types, in
    BOTH the USER and DEV DBs.
  - The folded cells per kind are populated correctly (the per-kind dispatch
    survival_builder._KIND_TO_FORM decides which cells a kind uses):
      callsite / instruction_anchor -> aob (+ expect_unique on the verified-unique)
      string_anchor                 -> anchor_string (+ expect_unique)
      data_slot                     -> rule (+ derives_from)
      vtable_base                   -> slot_count
      vtable_index                  -> all folded cells EMPTY (deferred), but the
                                       row exists like any other curated row.
      function kinds                -> NO folded re-find cell; the body fingerprint
                                       (content_hash/length) stays on the av row.
  - Step 5.2 filled 14 rows: callsite (5-8) carry an aob + expect_unique=1;
    string_anchor (12) carries an anchor_string + expect_unique=1;
    instruction_anchor (9) carries an aob + derives_from + expect_unique=1; data_slot
    (10/11/132) carry a rule + derives_from; vtable_base (119/138/139/140) carry a
    slot_count. The vtable_index rows (19-24) stay EMPTY (deferred on the
    runtime-vtable path).
  - derives_from walks the DAG the handoff authored (132->11->10->9->12); exactly 4
    curated rows carry a derives_from (9, 10, 11, 132).
  - expect_unique is set (=1) on the search-locating kinds the handoff verified
    unique (callsite 5-8, instruction_anchor 9, string_anchor 12) and NULL elsewhere.
  - The 156/157 ICVar accessor rows carry their RE-verified vtable_slot/struct_offset
    in the structured columns (id156 slot 2/+0x10; id157 slot 4/+0x20).

The apply==rebuild equivalence for the folded columns is carried by
test_direct_write.py (the direct-write path writes the SAME folded av cells a rebuild
would) and the whole-DB rebuild oracle (test_rebuild_oracle.py, whose per-table hash
now covers the folded columns on address_versions; the `survival` table is gone).

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

# kind -> the folded re-find cell(s) a kind populates (the single shared dispatch
# survival_builder._KIND_TO_FORM decides). function kinds use the body fingerprint
# (content_hash/length on the av row), NOT a folded re-find cell.
_FUNCTION_KINDS = ("function", "function_no_sig", "function_variadic")

# The folded re-find payload columns a function-kind av row must NOT carry (the
# search/derivation cells; the function form uses only content_hash + length).
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
        out = tempfile.mkdtemp(prefix="folded_oracle_")
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


def _folded_by_kid(con):
    """{kcdx_id: folded-cell dict} for the OPEN curated av rows (the folded re-find
    columns + the body fingerprint, read straight from address_versions)."""
    out = {}
    for row in con.execute(
            "SELECT kcdx_id, kind, aob, anchor_string, rule, slot_count, "
            "expect_unique, derives_from, content_hash, length "
            "FROM address_versions "
            "WHERE kcdx_id IS NOT NULL AND valid_through IS NULL"):
        out[row[0]] = {
            "kind": row[1], "aob": row[2], "anchor_string": row[3],
            "rule": row[4], "slot_count": row[5], "expect_unique": row[6],
            "derives_from": row[7], "content_hash": row[8], "length": row[9]}
    return out


def _no_survival_table(out):
    """The `survival` sibling table is GONE in both DBs (D22 / design §11.2 -- folded
    onto address_versions and deleted)."""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            present = {r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table'")}
            assert "survival" not in present, (
                f"[{which}] the `survival` table still exists (the fold should have "
                f"deleted it)")
        finally:
            con.close()


def _folded_columns_present(out):
    """The six folded survival/re-find columns exist on address_versions with the
    right SQL types in BOTH DBs (aob/anchor_string/rule TEXT;
    slot_count/expect_unique/derives_from INTEGER -- the former `survival` SCHEMA
    entry's SQL types)."""
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
                    f"!= expected {ty!r}")
        finally:
            con.close()


def _function_rows_carry_hash_not_folded(out):
    """A function-kind curated av row carries the body fingerprint (content_hash +
    length) and NO folded re-find cell (aob/anchor_string/rule/slot_count NULL;
    expect_unique NULL). Mirrors the old survival-row invariant on the av side."""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            kind_by_av = _decoded_kind_by_av(con)
            fn_avs = [av for av, k in kind_by_av.items() if k in _FUNCTION_KINDS]
            assert fn_avs, f"[{which}] no function-kind curated rows in the fixture"
            ph = ",".join("?" * len(fn_avs))
            # No function row carries a folded search/derivation cell.
            for pc in _NON_FUNCTION_PAYLOAD:
                bad = con.execute(
                    f'SELECT COUNT(*) FROM address_versions WHERE id IN ({ph}) '
                    f'AND "{pc}" IS NOT NULL', fn_avs).fetchone()[0]
                assert bad == 0, (
                    f"[{which}] {bad} function-kind av row(s) carry a folded {pc} "
                    f"(function kinds use only the body fingerprint, not the folded "
                    f"re-find cells)")
            bad_eu = con.execute(
                f'SELECT COUNT(*) FROM address_versions WHERE id IN ({ph}) '
                f'AND expect_unique IS NOT NULL', fn_avs).fetchone()[0]
            assert bad_eu == 0, (
                f"[{which}] {bad_eu} function-kind av row(s) carry expect_unique")
            # At least SOME function rows actually carry a body fingerprint (the
            # reuse path is exercised, not vacuously all-NULL).
            with_hash = con.execute(
                f'SELECT COUNT(*) FROM address_versions WHERE id IN ({ph}) '
                f'AND content_hash IS NOT NULL', fn_avs).fetchone()[0]
            assert with_hash > 0, (
                f"[{which}] no function-kind av row carries a content_hash (the "
                f"fingerprint path looks dead)")
        finally:
            con.close()


def _filled_kinds_carry_values(out):
    """Step 5.2 value assertions, on the folded av columns: the 14 filled rows carry
    their seed datum; the vtable_index rows (19-24) stay EMPTY (deferred). NEVER
    assert a guessed value -- just non-empty for the filled kinds + empty for the
    deferred kind. Plus: no non-function row carries a function fingerprint."""
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            fc = _folded_by_kid(con)
            # callsite (aob form): aob present.
            for kid in (5, 6, 7, 8):
                assert fc[kid]["aob"], f"[{which}] callsite {kid} has empty aob"
            # instruction_anchor (aob form): aob present + derives_from set.
            assert fc[9]["aob"], f"[{which}] instruction_anchor 9 has empty aob"
            assert fc[9]["derives_from"] is not None, (
                f"[{which}] instruction_anchor 9 has no derives_from")
            # string_anchor (literal form): anchor_string present.
            assert fc[12]["anchor_string"], (
                f"[{which}] string_anchor 12 has empty anchor_string")
            # data_slot (derivation form): rule + derives_from set.
            for kid in (10, 11, 132):
                assert fc[kid]["rule"], f"[{which}] data_slot {kid} has empty rule"
                assert fc[kid]["derives_from"] is not None, (
                    f"[{which}] data_slot {kid} has no derives_from")
            # vtable_base (table_shape form): slot_count set.
            for kid in (119, 138, 139, 140):
                assert fc[kid]["slot_count"] is not None, (
                    f"[{which}] vtable_base {kid} has empty slot_count")
            # vtable_index (deferred): all folded cells EMPTY.
            for kid in (19, 20, 21, 22, 23, 24):
                r = fc[kid]
                assert (r["aob"] is None and r["anchor_string"] is None
                        and r["rule"] is None and r["slot_count"] is None
                        and r["expect_unique"] is None
                        and r["derives_from"] is None), (
                    f"[{which}] vtable_index {kid} has a non-empty folded cell "
                    f"(deferred kind must stay empty): {r}")
            # No non-function row carries a function fingerprint.
            kind_by_av = _decoded_kind_by_av(con)
            nonfn_avs = [av for av, k in kind_by_av.items()
                         if k not in _FUNCTION_KINDS]
            ph = ",".join("?" * len(nonfn_avs))
            fp_on_nonfn = con.execute(
                f'SELECT COUNT(*) FROM address_versions WHERE id IN ({ph}) AND '
                f'(content_hash IS NOT NULL OR length IS NOT NULL)',
                nonfn_avs).fetchone()[0]
            assert fp_on_nonfn == 0, (
                f"[{which}] {fp_on_nonfn} non-function av row(s) carry a function "
                f"fingerprint (content_hash/length is function-only)")
        finally:
            con.close()


def _expect_unique_set_on_unique_locators(out):
    """Step 5.2: expect_unique=1 on the search-locating kinds the handoff verified
    .text-unique (callsite 5-8, instruction_anchor 9, string_anchor 12), and NULL on
    every other curated av row."""
    unique_kids = {5, 6, 7, 8, 9, 12}
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            fc = _folded_by_kid(con)
            for kid in unique_kids:
                assert fc[kid]["expect_unique"] == 1, (
                    f"[{which}] kid {kid} expect_unique="
                    f"{fc[kid]['expect_unique']!r}, expected 1")
            for kid, r in fc.items():
                if kid not in unique_kids:
                    assert r["expect_unique"] is None, (
                        f"[{which}] kid {kid} ({r['kind']}) carries "
                        f"expect_unique={r['expect_unique']!r}, expected NULL")
            # Exactly 6 curated av rows carry expect_unique.
            n = con.execute(
                "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL "
                "AND expect_unique IS NOT NULL").fetchone()[0]
            assert n == 6, (
                f"[{which}] {n} curated av rows carry expect_unique, expected 6")
        finally:
            con.close()


def _derives_from_chain(out):
    """Step 5.2: derives_from walks the DAG the handoff authored -- data_slot
    132 -> 11 -> 10 -> instruction_anchor 9 -> string_anchor 12. Each av.derives_from
    FK resolves to the dependency entity's curated av row, which back-maps to the
    expected kcdx_id. Exactly 4 curated av rows carry a derives_from (9, 10, 11, 132)."""
    expected = {9: 12, 10: 9, 11: 10, 132: 11}
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            cols = [c[1] for c in con.execute(
                'PRAGMA table_info("address_versions")')]
            assert "derives_from" in cols, (
                f"[{which}] address_versions has no derives_from column")
            av_to_kid = {r[0]: r[1] for r in con.execute(
                "SELECT id, kcdx_id FROM address_versions "
                "WHERE kcdx_id IS NOT NULL")}
            fc = _folded_by_kid(con)
            for kid, dep_kid in expected.items():
                dfrom = fc[kid]["derives_from"]
                assert dfrom is not None, (
                    f"[{which}] kid {kid} has no derives_from (expected -> "
                    f"{dep_kid})")
                assert av_to_kid.get(dfrom) == dep_kid, (
                    f"[{which}] kid {kid} derives_from av {dfrom} -> kid "
                    f"{av_to_kid.get(dfrom)}, expected kid {dep_kid}")
            n_set = con.execute(
                "SELECT COUNT(*) FROM address_versions WHERE kcdx_id IS NOT NULL "
                "AND derives_from IS NOT NULL").fetchone()[0]
            assert n_set == len(expected), (
                f"[{which}] {n_set} curated av row(s) have derives_from set, "
                f"expected {len(expected)} (the 9/10/11/132 chain)")
        finally:
            con.close()


def _icvar_slot_offset_cells(out):
    """schema-flatten-survival-fold Phase 3: the 156/157 ICVar accessor rows carry
    their RE-verified vtable_slot/struct_offset in the now-first-class structured
    columns (the §11 convention: a resolvable fact lives in its column, not only the
    notes prose). id156 ICVar_GetIVal = vtable[2] / +0x10; id157 ICVar_GetFVal =
    vtable[4] / +0x20 (decimal: slot 2/4, struct_offset 16/32)."""
    expected = {156: (2, 16), 157: (4, 32)}
    for which, fn in (("user", "reference.sqlite"), ("dev", "reference-dev.sqlite")):
        con = sqlite3.connect(os.path.join(out, fn))
        try:
            for kid, (slot, soff) in expected.items():
                row = con.execute(
                    "SELECT vtable_slot, struct_offset FROM address_versions "
                    "WHERE kcdx_id = ? AND valid_through IS NULL", (kid,)).fetchone()
                assert row is not None, (
                    f"[{which}] no curated av row for kcdx_id={kid}")
                assert row[0] == slot, (
                    f"[{which}] kid {kid} vtable_slot={row[0]!r}, expected {slot}")
                assert row[1] == soff, (
                    f"[{which}] kid {kid} struct_offset={row[1]!r}, expected {soff}")
        finally:
            con.close()


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_survival_table_deleted(rebuilt):  # noqa: F811
    _no_survival_table(rebuilt)


def test_folded_columns_present(rebuilt):  # noqa: F811
    _folded_columns_present(rebuilt)


def test_function_rows_carry_hash_not_folded(rebuilt):  # noqa: F811
    _function_rows_carry_hash_not_folded(rebuilt)


def test_filled_kinds_carry_values(rebuilt):  # noqa: F811
    _filled_kinds_carry_values(rebuilt)


def test_expect_unique_set_on_unique_locators(rebuilt):  # noqa: F811
    _expect_unique_set_on_unique_locators(rebuilt)


def test_derives_from_chain(rebuilt):  # noqa: F811
    _derives_from_chain(rebuilt)


def test_icvar_slot_offset_cells(rebuilt):  # noqa: F811
    _icvar_slot_offset_cells(rebuilt)


if __name__ == "__main__":
    try:
        out = _get_rebuild()
        _no_survival_table(out)
        print("PASS test_survival_table_deleted")
        _folded_columns_present(out)
        print("PASS test_folded_columns_present")
        _function_rows_carry_hash_not_folded(out)
        print("PASS test_function_rows_carry_hash_not_folded")
        _filled_kinds_carry_values(out)
        print("PASS test_filled_kinds_carry_values")
        _expect_unique_set_on_unique_locators(out)
        print("PASS test_expect_unique_set_on_unique_locators")
        _derives_from_chain(out)
        print("PASS test_derives_from_chain")
        _icvar_slot_offset_cells(out)
        print("PASS test_icvar_slot_offset_cells")
        print("\nall folded survival-column oracle tests passed")
    finally:
        _cleanup_rebuild()
