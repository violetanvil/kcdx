"""test_reference_checker.py -- the per-step test for the Python per-kind survival
REFERENCE checker (seeds_shared/survival_checker.py; the test-of-record, TRD D27).

WHAT THIS PROVES (the load-bearing claim)
-----------------------------------------
The reference checker REPRODUCES the Phase-0 cross-impl fixture's pinned verdicts. For
EVERY fixture slice (seeds_shared/cross_impl_fixture.py, step 0.5), running the checker
over that slice's bytes yields the fixture's DECLARED expected verdict -- per kind:

  - function          -- the body BLAKE3 == the recorded content_hash -> Unchanged; a
                         one-byte-flipped body -> Changed.
  - callsite          -- a unique AOB hit -> Unchanged; zero hits -> Changed; TWO hits
                         -> Ambiguous (the multiple-hit case the step calls out).
  - string_anchor     -- the literal present -> Unchanged; absent -> Changed.
  - instruction_anchor-- the anchor shape matches + the disp32-follow lands on the
                         recorded target -> Unchanged; a wrong-shape opcode -> Changed.
  - data_slot         -- the disp32 derivation reaches the recorded slot -> Unchanged; a
                         displaced disp32 -> Changed.
  - vtable_base       -- N qwords each a .text pointer -> Unchanged; a non-pointer slot
                         -> Changed.
  - vtable_index      -- CannotCheck (deferred -- no static target).

PLUS the transitive-CannotCheck DAG: a row that derives from a Changed dependency is
CannotCheck-with-reason, not a silent pass.

THE BLAKE3 DEPENDENCY. stdlib hashlib has NO blake3; the function body-hash kind needs
the canonical BLAKE3 the production content_hash was computed with. The test injects a
BLAKE3 hasher via the checker's `default_body_hasher()` (the `blake3` PyPI package, v1.0.8,
Apache-2.0 -- recorded in data/refdata-extractor/README.md). If `blake3` is not installed,
the function body-hash assertions SKIP-with-reason (the test still RUNS + asserts every
other kind) AND the no-hasher path is asserted to yield CannotCheck -- the checker never
silently passes a body-hash it cannot compute.

RUN
---
    python tests/test_reference_checker.py
    pytest tests/test_reference_checker.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)

from seeds_shared import cross_impl_fixture as fx          # noqa: E402
from seeds_shared import survival_checker as sc            # noqa: E402


# ---------------------------------------------------------------------------
# BLAKE3 hasher -- injected into the function body-hash check. None if `blake3` is
# not installed (the function-kind assertions then SKIP-with-reason; every other
# kind still runs).
# ---------------------------------------------------------------------------
try:
    _BODY_HASHER = sc.default_body_hasher()
    _BLAKE3_AVAILABLE = True
except RuntimeError:
    _BODY_HASHER = None
    _BLAKE3_AVAILABLE = False


def _row(kcdx_id):
    """The fixture row for a kcdx_id."""
    for r in fx.FIXTURE_ROWS:
        if r["kcdx_id"] == kcdx_id:
            return r
    raise KeyError(kcdx_id)


def _slice(row, name):
    for s in row["slices"]:
        if s["name"] == name:
            return s
    raise KeyError((row["kcdx_id"], name))


def _check_slice(kcdx_id, slice_name, **kw):
    """Run the checker over a fixture slice and return (got_verdict, expected_verdict).

    The slice carries the kind's bytes (cross_impl_fixture.py header: "the check sees
    exactly these bytes"); the row's `datum` carries the recorded survival datum the
    check compares against. We pass datum + the slice body straight into check_row.
    """
    row = _row(kcdx_id)
    s = _slice(row, slice_name)
    got = sc.check_row(row["kind"], row["datum"], s["body"], **kw)
    return got.verdict, s["verdict"], got


# ===========================================================================
# function -- body hash (BLAKE3). Unchanged on a match, Changed on a byte flip.
# Skips (with reason) the two body-hash assertions when blake3 is absent, but
# ALWAYS asserts the no-hasher path is CannotCheck (never a silent pass).
# ===========================================================================
def test_function_body_hash_reproduces_fixture_verdicts():
    # 999001: unchanged (BLAKE3(body) == content_hash) + changed_byte_flip.
    if _BLAKE3_AVAILABLE:
        got, exp, res = _check_slice(999001, "unchanged", body_hasher=_BODY_HASHER)
        assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
        got, exp, res = _check_slice(999001, "changed_byte_flip", body_hasher=_BODY_HASHER)
        assert got == exp == fx.VERDICT_CHANGED, (res, exp)
    else:
        print("SKIP function body-hash (blake3 not installed) -- asserting no-hasher CannotCheck only")
    # The checker NEVER silently passes a body-hash it cannot compute: no hasher ->
    # CannotCheck-with-reason (runs regardless of blake3 availability).
    row = _row(999001)
    s = _slice(row, "unchanged")
    res = sc.check_row(row["kind"], row["datum"], s["body"], body_hasher=None)
    assert res.verdict == fx.VERDICT_CANNOT_CHECK, res


# ===========================================================================
# callsite -- AOB re-match. unique -> Unchanged, zero -> Changed, TWO -> Ambiguous.
# ===========================================================================
def test_callsite_reproduces_fixture_verdicts():
    got, exp, res = _check_slice(7, "unchanged_unique_hit")
    assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
    got, exp, res = _check_slice(7, "changed_zero_hits")
    assert got == exp == fx.VERDICT_CHANGED, (res, exp)
    # The multiple-hit -> Ambiguous case the step calls out explicitly.
    got, exp, res = _check_slice(7, "ambiguous_two_hits")
    assert got == exp == fx.VERDICT_AMBIGUOUS, (res, exp)


# ===========================================================================
# string_anchor -- literal presence. present -> Unchanged, absent -> Changed.
# ===========================================================================
def test_string_anchor_reproduces_fixture_verdicts():
    got, exp, res = _check_slice(12, "unchanged_present")
    assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
    got, exp, res = _check_slice(12, "changed_absent")
    assert got == exp == fx.VERDICT_CHANGED, (res, exp)


# ===========================================================================
# instruction_anchor -- derivation chain. shape match + disp32-follow lands on the
# recorded target -> Unchanged; a wrong-shape opcode -> Changed.
# ===========================================================================
def test_instruction_anchor_reproduces_fixture_verdicts():
    got, exp, res = _check_slice(9, "unchanged_lands_on_target")
    assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
    got, exp, res = _check_slice(9, "changed_wrong_shape")
    assert got == exp == fx.VERDICT_CHANGED, (res, exp)


# ===========================================================================
# data_slot -- derivation (no content hash). disp32 derivation reaches the recorded
# slot -> Unchanged; a displaced disp32 -> Changed.
# ===========================================================================
def test_data_slot_reproduces_fixture_verdicts():
    got, exp, res = _check_slice(10, "unchanged_derivation_reaches_slot")
    assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
    got, exp, res = _check_slice(10, "changed_derivation_lands_wrong")
    assert got == exp == fx.VERDICT_CHANGED, (res, exp)


# ===========================================================================
# vtable_base -- table-shape. N .text pointers -> Unchanged; a non-pointer slot -> Changed.
# The fixture's `text_range` is the real WHGame.dll .text window [0x1000, 0x3A01E1A);
# all three id-138 good-table RVAs (0x0071A5A4, 0x00667B24, 0x03993898) resolve into
# it, so the Unchanged slice pins normally.
# ===========================================================================
def test_vtable_base_reproduces_fixture_verdicts():
    got, exp, res = _check_slice(138, "unchanged_n_valid_pointers")
    assert got == exp == fx.VERDICT_UNCHANGED, (res, exp)
    # The Changed slice (a non-pointer slot) is unambiguous and always asserted.
    got, exp, res = _check_slice(138, "changed_non_pointer_slot")
    assert got == exp == fx.VERDICT_CHANGED, (res, exp)


# ===========================================================================
# vtable_index -- CannotCheck (deferred -- no static target).
# ===========================================================================
def test_vtable_index_reproduces_cannot_check():
    got, exp, res = _check_slice(19, "cannot_check_no_static_target")
    assert got == exp == fx.VERDICT_CANNOT_CHECK, (res, exp)


# ===========================================================================
# Every fixture slice is reproduced -- the comprehensive pin: iterate the WHOLE
# fixture and assert checker(slice) == the slice's declared verdict.
# ===========================================================================
def test_every_fixture_slice_verdict_is_reproduced():
    for r in fx.FIXTURE_ROWS:
        for s in r["slices"]:
            # function kinds need the hasher; skip those slices when blake3 is absent
            # (the dedicated function test already asserts the no-hasher CannotCheck).
            is_function = r["kind"] in ("function", "function_no_sig", "function_variadic")
            if is_function and not _BLAKE3_AVAILABLE:
                continue
            kw = {"body_hasher": _BODY_HASHER} if is_function else {}
            got = sc.check_row(r["kind"], r["datum"], s["body"], **kw)
            assert got.verdict == s["verdict"], (
                "kind=%s row=%s slice=%s: checker said %s, fixture pins %s (%s)"
                % (r["kind"], r["name"], s["name"], got.verdict, s["verdict"], got.reason))


# ===========================================================================
# The transitive-CannotCheck DAG -- a row that derives from a Changed dependency is
# CannotCheck-with-reason, NOT a silent pass (fingerprint-per-kind.md "The anchor
# dependency"). Uses the data_slot row (derives from instruction_anchor id 9): even
# the slice that WOULD be Unchanged becomes CannotCheck when its dependency is Changed.
# ===========================================================================
def test_transitive_cannot_check_when_dependency_changed():
    row = _row(10)  # data_slot, derives from instruction_anchor id 9
    s = _slice(row, "unchanged_derivation_reaches_slot")  # would be Unchanged standalone
    dep_changed = sc.CheckResult(fx.VERDICT_CHANGED, "the anchor it derives from changed")
    res = sc.check_row(row["kind"], row["datum"], s["body"], dependency_result=dep_changed)
    assert res.verdict == fx.VERDICT_CANNOT_CHECK, (
        "a data_slot deriving from a Changed anchor must be transitively CannotCheck, "
        "got %s" % res)
    assert "transitively suspect" in res.reason, res
    # Control: the SAME row with an Unchanged dependency runs its own check normally.
    dep_ok = sc.CheckResult(fx.VERDICT_UNCHANGED, "")
    res2 = sc.check_row(row["kind"], row["datum"], s["body"], dependency_result=dep_ok)
    assert res2.verdict == fx.VERDICT_UNCHANGED, res2


# ===========================================================================
# An unknown kind is RAISED, never guessed (the "do NOT invent" floor).
# ===========================================================================
def test_unknown_kind_raises():
    try:
        sc.check_row("not_a_real_kind", {}, b"")
    except ValueError:
        pass
    else:
        raise AssertionError("check_row did not raise on an unknown kind")


# ---------------------------------------------------------------------------
# direct-run harness (no pytest required) -- mirrors the fixture/agreement tests.
# ---------------------------------------------------------------------------
def _main():
    tests = [
        ("test_function_body_hash_reproduces_fixture_verdicts",
         test_function_body_hash_reproduces_fixture_verdicts),
        ("test_callsite_reproduces_fixture_verdicts", test_callsite_reproduces_fixture_verdicts),
        ("test_string_anchor_reproduces_fixture_verdicts", test_string_anchor_reproduces_fixture_verdicts),
        ("test_instruction_anchor_reproduces_fixture_verdicts",
         test_instruction_anchor_reproduces_fixture_verdicts),
        ("test_data_slot_reproduces_fixture_verdicts", test_data_slot_reproduces_fixture_verdicts),
        ("test_vtable_base_reproduces_fixture_verdicts", test_vtable_base_reproduces_fixture_verdicts),
        ("test_vtable_index_reproduces_cannot_check", test_vtable_index_reproduces_cannot_check),
        ("test_every_fixture_slice_verdict_is_reproduced", test_every_fixture_slice_verdict_is_reproduced),
        ("test_transitive_cannot_check_when_dependency_changed",
         test_transitive_cannot_check_when_dependency_changed),
        ("test_unknown_kind_raises", test_unknown_kind_raises),
    ]
    failed = []
    for name, fn in tests:
        try:
            fn()
            print(f"PASS {name}")
        except AssertionError as e:
            failed.append((name, e))
            print(f"FAIL {name}: {e}")
    if failed:
        print(f"\n{len(failed)} FAILED")
        sys.exit(1)
    print(f"\nALL PASSED ({len(tests)} ran){' [blake3 present]' if _BLAKE3_AVAILABLE else ' [blake3 ABSENT -- function body-hash skipped]'}")


if __name__ == "__main__":
    _main()
