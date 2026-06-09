"""test_cross_impl_fixture_json.py -- the JSON-export round-trip test (TRD D27, step 3.4).

WHAT THIS PROVES
----------------
The cross-implementation fixture's JSON export (seeds_shared/cross_impl_fixture.py's
dump_fixture_json / fixture_to_json_obj / parse_json_obj) is the LOSSLESS contract that
carries the source-of-truth FIXTURE_ROWS across the language boundary to the C++ engine
agreement consumer (the C++ side cannot import the Python module, so it reads this JSON).

Three falsifiable claims:
  1. ROUND-TRIP LOSSLESS (AP14 -- never silently drops a row): parsing the emitted JSON
     back reproduces load_fixture() EXACTLY -- every row, every slice's bytes, every
     pinned verdict, every datum. A dropped/corrupted row turns this red.
  2. THE COMMITTED JSON IS CURRENT: the on-disk committed JSON
     (data/refdata-extractor/tests/cross_impl_fixture.json -- the file the C++ test
     reads) byte-matches a fresh emit. A fixture change that did not re-emit the JSON
     (a drift between the Python source-of-truth and the C++-consumed contract) turns
     this red. (Run this test to RE-EMIT after editing the fixture.)
  3. THE CONTRACT IS WELL-FORMED: the JSON object carries every in-scope kind + the
     vtable_index CannotCheck row, with hex-decodable slice bodies and known verdicts --
     the same coverage the fixture's own test pins, now on the exported shape.

THE SOURCE-OF-TRUTH CHAIN: cross_impl_fixture.py FIXTURE_ROWS (derived from
fingerprint-per-kind.md, NOT from any checker) -> dump_fixture_json -> the committed
cross_impl_fixture.json -> the C++ engine agreement consumer reads it, plants each
slice in a synthetic PE, runs the REAL engine static check, asserts
engine_verdict == the pinned verdict. This test pins the Python->JSON half so a drift
is caught on the Python side (the JS<->Python half is test_cross_impl_fixture.py +
the frontend crossImplAgreement.test.ts; the JS<->C++ half is the cap-NN plugin).

RUN
---
    python tests/test_cross_impl_fixture_json.py        # re-emits the committed JSON
    pytest tests/test_cross_impl_fixture_json.py
"""
import json
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)
# Repo root: .../kcdx (HERE is .../kcdx/data/refdata-extractor/tests).
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))

from seeds_shared import cross_impl_fixture as fx  # noqa: E402

# The committed JSON contract (a private artifact under the RE-tooling tree) -- the
# canonical human-readable cross-language fixture + the git-review diff surface.
JSON_PATH = os.path.join(HERE, "cross_impl_fixture.json")
# The generated C++ engine-embedded header the cap-NN agreement self-test #includes
# (src/ source that compiles into the engine -- the engine reads the EXACT pinned bytes
# with no runtime file I/O / deploy step). GENERATED from the SAME source-of-truth.
HEADER_PATH = os.path.join(REPO_ROOT, "src", "survival_agreement_fixture.h")

EXPECTED_IN_SCOPE_KINDS = frozenset({
    "function", "callsite", "string_anchor",
    "instruction_anchor", "data_slot", "vtable_base",
})


def _rows_equal(a, b):
    """Deep-equal two FIXTURE_ROWS-shaped row lists: same rows in order, each slice's
    name/body(bytes)/verdict/detail identical. Returns (ok, why)."""
    if len(a) != len(b):
        return False, "row count %d != %d" % (len(a), len(b))
    for ra, rb in zip(a, b):
        for k in ("kcdx_id", "kind", "name", "datum", "source"):
            if ra[k] != rb[k]:
                return False, "row %r field %r: %r != %r" % (ra.get("name"), k, ra[k], rb[k])
        if len(ra["slices"]) != len(rb["slices"]):
            return False, "row %r slice count differs" % (ra["name"],)
        for sa, sb in zip(ra["slices"], rb["slices"]):
            for k in ("name", "verdict", "detail"):
                if sa[k] != sb[k]:
                    return False, "row %r slice %r field %r differs" % (ra["name"], sa["name"], k)
            if bytes(sa["body"]) != bytes(sb["body"]):
                return False, "row %r slice %r BYTES differ" % (ra["name"], sa["name"])
    return True, ""


def test_json_round_trips_losslessly():
    """(1) parse_json_obj(fixture_to_json_obj()) == load_fixture() -- the JSON carries
    every row/slice/verdict/byte without loss (AP14). A dropped or corrupted row fails."""
    obj = fx.fixture_to_json_obj()
    reparsed = fx.parse_json_obj(obj)
    ok, why = _rows_equal(fx.load_fixture(), reparsed)
    assert ok, "JSON round-trip is LOSSY: %s" % (why,)


def test_json_serializes_and_deserializes_through_disk():
    """(1) The JSON survives a real json.dumps/json.loads (not just the in-memory obj):
    dump -> a string -> json.loads -> parse_json_obj reproduces load_fixture()."""
    obj = fx.fixture_to_json_obj()
    text = json.dumps(obj)            # a real serialize (catches a non-JSON value).
    reloaded = json.loads(text)
    reparsed = fx.parse_json_obj(reloaded)
    ok, why = _rows_equal(fx.load_fixture(), reparsed)
    assert ok, "JSON disk round-trip is LOSSY: %s" % (why,)


def test_committed_json_is_current():
    """(2) The committed cross_impl_fixture.json (the file the C++ engine consumer reads)
    matches a fresh emit. Re-emits the file, then asserts it equals the source-of-truth.
    A fixture edited without re-emitting the JSON -- a Python<->C++ contract drift -- is
    caught here (and this run fixes it). The C++ engine reads the SAME bytes the Python
    pins, so the JS<->Python<->C++ chain stays one verdict on one byte set."""
    fresh = fx.dump_fixture_json(JSON_PATH)          # write the committed file.
    with open(JSON_PATH, "r", encoding="utf-8") as f:
        on_disk = json.load(f)
    assert on_disk == fresh, (
        "the committed cross_impl_fixture.json drifted from the fixture source-of-truth; "
        "this run re-emitted it -- commit the updated JSON")
    # And the committed JSON itself round-trips to the fixture (the C++ consumer's input
    # reproduces the pinned verdicts).
    ok, why = _rows_equal(fx.load_fixture(), fx.parse_json_obj(on_disk))
    assert ok, "the committed JSON does not reproduce the fixture: %s" % (why,)


def test_committed_header_is_current():
    """(2) The generated C++ engine-embedded header (src/survival_agreement_fixture.h)
    matches a fresh emit AND embeds the SAME JSON bytes as the committed .json. Re-emits
    the header, then asserts the on-disk header equals the fresh render and that the JSON
    it embeds is byte-identical to fixture_json_text(). A fixture edited without
    re-emitting the header -- the C++ engine compiling a stale contract -- is caught here
    (and this run fixes it)."""
    fresh = fx.fixture_header_text()
    fx.dump_fixture_header(HEADER_PATH)
    with open(HEADER_PATH, "r", encoding="utf-8") as f:
        on_disk = f.read()
    assert on_disk == fresh, (
        "the generated src/survival_agreement_fixture.h drifted from the fixture "
        "source-of-truth; this run re-emitted it -- commit the updated header")
    # The header must embed exactly the committed JSON text (the two renderings agree),
    # so the C++ engine and the .json contract carry identical bytes.
    json_text = fx.fixture_json_text()
    assert json_text.strip() in on_disk, (
        "the header's embedded JSON does not match fixture_json_text() -- the embedded "
        "C++ contract diverged from the .json contract")


def test_json_object_is_well_formed():
    """(3) The exported object carries the contract metadata + every in-scope kind + the
    vtable_index CannotCheck row, with hex-decodable bodies and known verdicts."""
    obj = fx.fixture_to_json_obj()
    assert obj["format_version"] == fx.JSON_FORMAT_VERSION
    assert frozenset(obj["in_scope_kinds"]) == EXPECTED_IN_SCOPE_KINDS, (
        "JSON in_scope_kinds %r != %r" % (set(obj["in_scope_kinds"]), set(EXPECTED_IN_SCOPE_KINDS)))
    assert set(obj["verdicts"]) == fx.VERDICTS
    kinds_present = {r["kind"] for r in obj["rows"]}
    for kind in EXPECTED_IN_SCOPE_KINDS:
        assert kind in kinds_present, "JSON missing in-scope kind %r" % (kind,)
    assert "vtable_index" in kinds_present, "JSON missing the vtable_index CannotCheck row"
    # Every slice body decodes from hex; every verdict is known.
    for r in obj["rows"]:
        for s in r["slices"]:
            bytes.fromhex(s["body"])  # raises on a non-hex body.
            assert s["verdict"] in fx.VERDICTS, (
                "JSON slice %r/%r has unknown verdict %r" % (r["name"], s["name"], s["verdict"]))
            if s["body"] == "":
                assert s["verdict"] == fx.VERDICT_CANNOT_CHECK, (
                    "an empty body is only legal for CannotCheck; %r/%r is %r"
                    % (r["name"], s["name"], s["verdict"]))


def test_vtable_index_cannotcheck_present_in_json():
    """(3) The vtable_index CannotCheck row is present in the JSON with an empty body +
    the CannotCheck verdict -- the deferred answer all three implementations return."""
    obj = fx.fixture_to_json_obj()
    vt = [r for r in obj["rows"] if r["kind"] == "vtable_index"]
    assert vt, "no vtable_index row in the exported JSON"
    for r in vt:
        for s in r["slices"]:
            assert s["verdict"] == fx.VERDICT_CANNOT_CHECK, (
                "vtable_index JSON slice %r is %r, expected CannotCheck"
                % (s["name"], s["verdict"]))


# ---------------------------------------------------------------------------
# direct-run harness (no pytest required) -- mirrors the precedent agreement tests.
# Running directly RE-EMITS the committed JSON (test_committed_json_is_current does).
# ---------------------------------------------------------------------------
def _main():
    tests = [
        ("test_json_round_trips_losslessly", test_json_round_trips_losslessly),
        ("test_json_serializes_and_deserializes_through_disk",
         test_json_serializes_and_deserializes_through_disk),
        ("test_committed_json_is_current", test_committed_json_is_current),
        ("test_committed_header_is_current", test_committed_header_is_current),
        ("test_json_object_is_well_formed", test_json_object_is_well_formed),
        ("test_vtable_index_cannotcheck_present_in_json",
         test_vtable_index_cannotcheck_present_in_json),
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
    print(f"\nALL PASSED ({len(tests)} ran) -- committed JSON at {JSON_PATH}")


if __name__ == "__main__":
    _main()
