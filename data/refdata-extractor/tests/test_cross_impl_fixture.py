"""test_cross_impl_fixture.py -- the per-step test for the cross-implementation
per-kind survival fixture (the TEST-OF-RECORD, TRD D27).

WHAT THIS PROVES
----------------
The fixture (seeds_shared/cross_impl_fixture.py) LOADS and its expected-verdict table
is WELL-FORMED: the byte-slices load as bytes, every row carries a declared verdict per
slice, every in-scope checkable kind (function, callsite, string_anchor,
instruction_anchor, data_slot, vtable_base) has at least one row with a declared
expected verdict, and the vtable_index row declares the CannotCheck expectation.

WHAT THIS DOES NOT PROVE (scope boundary)
-----------------------------------------
This step establishes the FIXTURE + the EXPECTED-VERDICT TABLE only. It does NOT
implement the per-kind survival CHECK (that is the Phase-1 Python reference checker)
and does NOT assert that a checker reproduces the verdicts (no checker exists yet). So
these tests assert the fixture's SHAPE -- that it loads + the verdict table is
well-formed -- NOT that the verdicts are computed correctly by some checker.

The Phase-2 (JS<->Python) and Phase-3 (JS<->C++) agreement tests, in later steps, build
the SAME documented byte-slices on their side and assert the SAME pinned verdict from
this fixture -- this test only guards the fixture itself.

RUN
---
    python tests/test_cross_impl_fixture.py
    pytest tests/test_cross_impl_fixture.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)

from seeds_shared import cross_impl_fixture as fx  # noqa: E402


# The in-scope checkable kinds the fixture MUST each cover with >=1 declared-verdict row.
EXPECTED_IN_SCOPE_KINDS = frozenset({
    "function", "callsite", "string_anchor",
    "instruction_anchor", "data_slot", "vtable_base",
})


def test_fixture_loads():
    """(1) The fixture loads -- load_fixture() returns a non-empty list of rows, each a
    dict with the documented keys, and FIXTURE_ROWS agrees with it."""
    rows = fx.load_fixture()
    assert isinstance(rows, list) and rows, "load_fixture() returned no rows"
    assert len(rows) == len(fx.FIXTURE_ROWS), "load_fixture() disagrees with FIXTURE_ROWS"
    for r in rows:
        for key in ("kcdx_id", "kind", "name", "datum", "source", "slices"):
            assert key in r, "row %r missing key %r" % (r.get("name"), key)
        assert r["slices"], "row %r has no slices" % (r["name"],)
        # Provenance is mandatory -- the trust axis (working-artifacts.md): every verdict
        # traces to a documented source (seed row / mini-dump / _research finding /
        # documented synthetic), never an undocumented guess.
        assert isinstance(r["source"], str) and r["source"].strip(), (
            "row %r has no documented source/provenance" % (r["name"],))


def test_slices_load_as_bytes():
    """(1) Every fixture byte-slice loads -- each slice's body is bytes (the on-disk
    bytes the kind's check operates on). An empty body is allowed ONLY for the
    CannotCheck case (vtable_index has no on-disk target)."""
    for r in fx.FIXTURE_ROWS:
        for s in r["slices"]:
            assert isinstance(s["body"], (bytes, bytearray)), (
                "slice %r/%r body is not bytes" % (r["name"], s["name"]))
            if not s["body"]:
                assert s["verdict"] == fx.VERDICT_CANNOT_CHECK, (
                    "empty slice body only legal for CannotCheck; %r/%r is %r"
                    % (r["name"], s["name"], s["verdict"]))


def test_verdict_table_well_formed():
    """(2) The expected-verdict table parses + is well-formed -- every slice carries a
    verdict drawn from the declared VERDICTS vocabulary plus a non-empty detail (the
    falsifiable claim the agreement pins)."""
    for r in fx.FIXTURE_ROWS:
        for s in r["slices"]:
            assert s["verdict"] in fx.VERDICTS, (
                "slice %r/%r has unknown verdict %r" % (r["name"], s["name"], s["verdict"]))
            assert isinstance(s["detail"], str) and s["detail"].strip(), (
                "slice %r/%r has no detail (the falsifiable claim)" % (r["name"], s["name"]))


def test_slice_names_unique_within_row():
    """(2) The verdict table is addressable -- slice names are unique within a row, so
    expected_verdict(kcdx_id, slice_name) resolves to exactly one verdict."""
    for r in fx.FIXTURE_ROWS:
        names = [s["name"] for s in r["slices"]]
        assert len(names) == len(set(names)), (
            "row %r has duplicate slice names %r" % (r["name"], names))


def test_expected_verdict_lookup_resolves():
    """(2) expected_verdict() returns each declared slice's verdict and raises for an
    unknown pair (the lookup the agreement tests use to pin a verdict)."""
    for r in fx.FIXTURE_ROWS:
        for s in r["slices"]:
            assert fx.expected_verdict(r["kcdx_id"], s["name"]) == s["verdict"]
    try:
        fx.expected_verdict(-1, "nope")
    except KeyError:
        pass
    else:
        raise AssertionError("expected_verdict did not raise on an unknown pair")


def test_every_in_scope_kind_has_a_declared_verdict_row():
    """(3) Every in-scope checkable kind has >=1 row with a declared expected verdict.
    function, callsite, string_anchor, instruction_anchor, data_slot, vtable_base each
    appear with at least one slice carrying a VERDICTS value."""
    assert frozenset(fx.IN_SCOPE_KINDS) == EXPECTED_IN_SCOPE_KINDS, (
        "fixture IN_SCOPE_KINDS %r != expected %r"
        % (set(fx.IN_SCOPE_KINDS), set(EXPECTED_IN_SCOPE_KINDS)))
    for kind in EXPECTED_IN_SCOPE_KINDS:
        rows = fx.rows_for_kind(kind)
        assert rows, "no fixture row for in-scope kind %r" % (kind,)
        has_declared = any(
            s["verdict"] in fx.VERDICTS for r in rows for s in r["slices"])
        assert has_declared, "kind %r has no declared-verdict slice" % (kind,)


def test_in_scope_kinds_carry_unchanged_and_changed():
    """(3) Each in-scope kind's row is FALSIFIABLE -- it carries both an Unchanged
    (match) and a Changed (mismatch) slice, so the pinned verdict can go either way (a
    fixture that only ever pins Unchanged proves nothing -- AP15 falsifiability)."""
    for kind in EXPECTED_IN_SCOPE_KINDS:
        verdicts = {s["verdict"] for r in fx.rows_for_kind(kind) for s in r["slices"]}
        assert fx.VERDICT_UNCHANGED in verdicts, (
            "kind %r has no Unchanged slice" % (kind,))
        assert fx.VERDICT_CHANGED in verdicts, (
            "kind %r has no Changed slice (not falsifiable)" % (kind,))


def test_vtable_index_declares_cannot_check():
    """(4) The vtable_index row declares the CannotCheck expectation -- it exists, is
    kind=vtable_index, and every one of its slices carries the CannotCheck verdict (the
    deferred-within-this-design answer all three implementations return)."""
    rows = fx.rows_for_kind("vtable_index")
    assert rows, "no vtable_index row in the fixture"
    for r in rows:
        assert r["slices"], "vtable_index row %r has no slices" % (r["name"],)
        for s in r["slices"]:
            assert s["verdict"] == fx.VERDICT_CANNOT_CHECK, (
                "vtable_index slice %r is %r, expected CannotCheck"
                % (s["name"], s["verdict"]))
    # vtable_index is NOT one of the in-scope checkable kinds (it's the deferred case).
    assert "vtable_index" not in fx.IN_SCOPE_KINDS, (
        "vtable_index must not be listed as an in-scope checkable kind")


# ---------------------------------------------------------------------------
# direct-run harness (no pytest required) -- mirrors the precedent agreement test.
# ---------------------------------------------------------------------------
def _main():
    tests = [
        ("test_fixture_loads", test_fixture_loads),
        ("test_slices_load_as_bytes", test_slices_load_as_bytes),
        ("test_verdict_table_well_formed", test_verdict_table_well_formed),
        ("test_slice_names_unique_within_row", test_slice_names_unique_within_row),
        ("test_expected_verdict_lookup_resolves", test_expected_verdict_lookup_resolves),
        ("test_every_in_scope_kind_has_a_declared_verdict_row",
         test_every_in_scope_kind_has_a_declared_verdict_row),
        ("test_in_scope_kinds_carry_unchanged_and_changed",
         test_in_scope_kinds_carry_unchanged_and_changed),
        ("test_vtable_index_declares_cannot_check", test_vtable_index_declares_cannot_check),
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
    print(f"\nALL PASSED ({len(tests)} ran)")


if __name__ == "__main__":
    _main()
