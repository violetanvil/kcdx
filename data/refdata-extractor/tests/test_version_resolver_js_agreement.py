"""test_version_resolver_js_agreement.py -- the Python half of the cross-implementation
agreement test (design D15).

The maintainer-tool frontend ships a JS port of seeds_shared/version_resolver.py (the
client-side `.rdata` resolver). The two implementations MUST resolve the SAME (tag, ordinal)
on the SAME bytes. The JS half lives in the frontend
(src/dll-resolver/versionResolver.test.ts, the "cross-implementation agreement" describe);
this file is the Python half.

THE SHARED FIXTURE. Both halves read the SAME documented input: the version string
`release_1_5_1164953_841` interned >=2 times in a `.rdata` section. The JS builds it with
makeFakePE.makeAgreeingPE(); this Python test builds the SAME intern bytes (the regex matches
raw ASCII in `.rdata`, so the bytes that matter are exactly the intern ASCII -- the PE header
scaffolding around them does not change what the regex sees). Both resolve to the SAME PINNED
value:

    AGREE_EXPECTED = ("1.5.1164953", 1164953)   # tag = M.N.BUILD (SUB 841 dropped); ordinal = BUILD

so a change to EITHER resolver that breaks agreement -- a different regex, a different
agreement rule, or the SUB leaking into the tag -- fails a test on its own side. This test
exercises the REAL version_resolver path (its _VERSION_RE over the intern bytes, then its
_decide), not a reimplementation.

RUN
---
    python tests/test_version_resolver_js_agreement.py
    pytest tests/test_version_resolver_js_agreement.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)

from seeds_shared import version_resolver  # noqa: E402
from seeds_shared.version_resolver import _decide, VersionResolveError  # noqa: E402


# The SHARED, documented fixture input -- identical to the JS makeFakePE AGREE_INTERN.
AGREE_INTERN = b"release_1_5_1164953_841"
# The SHARED, documented disagreeing input -- identical to the JS makeDisagreeingPE.
DISAGREE_INTERN = b"release_1_6_2000000_1"

# The single PINNED expected value BOTH implementations must produce on the agreeing fixture.
# (Mirrors makeFakePE AGREE_EXPECTED on the JS side.)
AGREE_EXPECTED = ("1.5.1164953", 1164953)


def _scan_intern_bytes(data: bytes):
    """Run version_resolver's REAL regex over a `.rdata`-style byte block and return the
    decoded match tuples in version_resolver's `((M,N,BUILD,SUB), va)` shape -- the same shape
    _scan_rdata_matches yields, but over an in-memory byte block rather than a pefile section.

    This exercises version_resolver._VERSION_RE (the authority for the regex the JS mirrors) on
    the SAME intern bytes the JS fixture interns into `.rdata`. The va is the in-block offset
    (the PE scaffolding's image-base/section-VA offset is irrelevant to the agreement -- the
    resolved (tag, ordinal) does not depend on the VA, only the disagreement MESSAGE does)."""
    return [
        ((int(mobj.group(1)), int(mobj.group(2)),
          int(mobj.group(3)), int(mobj.group(4))), mobj.start())
        for mobj in version_resolver._VERSION_RE.finditer(data)
    ]


def _rdata_block(interns):
    """Compose a `.rdata`-style byte block: a NUL lead + each intern NUL-separated, mirroring
    how distinct interned strings sit in a real `.rdata`. The regex sees the intern ASCII
    regardless of the surrounding NULs/filler."""
    block = b"\x00" * 8
    for intern in interns:
        block += intern + b"\x00ABC\x00"
    return block


def test_agreeing_fixture_resolves_to_pinned_value():
    """The Python resolves the agreeing fixture (>=2 `release_1_5_1164953_841` interns) to the
    SAME pinned value the JS asserts -- the cross-impl agreement on the agreeing path."""
    matches = _scan_intern_bytes(_rdata_block([AGREE_INTERN, AGREE_INTERN]))
    # The agreement path must actually fire (>=2 interns found), not vacuously pass.
    assert len(matches) >= 2, f"expected >=2 interns, found {len(matches)}: {matches}"
    assert _decide(matches) == AGREE_EXPECTED, (
        f"Python resolved {_decide(matches)!r}, expected {AGREE_EXPECTED!r} -- the JS port "
        f"asserts the SAME value on the SAME fixture bytes (D15 agreement broken)")


def test_disagreeing_fixture_raises_both_sides():
    """The disagreeing fixture (two different release strings) raises on the Python side too --
    the same advisory failure the JS makeDisagreeingPE produces."""
    matches = _scan_intern_bytes(_rdata_block([AGREE_INTERN, DISAGREE_INTERN]))
    assert len(matches) == 2, f"expected 2 interns, found {len(matches)}"
    try:
        _decide(matches)
    except VersionResolveError as e:
        msg = str(e)
        assert "release_1_5_1164953_841" in msg, msg
        assert "release_1_6_2000000_1" in msg, msg
    else:
        raise AssertionError("expected VersionResolveError for disagreeing interns")


def test_single_intern_fixture_raises_both_sides():
    """The <2 fixture (one intern) raises ('found 1') on the Python side too -- the same
    advisory failure the JS makeSingleInternPE produces."""
    matches = _scan_intern_bytes(_rdata_block([AGREE_INTERN]))
    assert len(matches) == 1
    try:
        _decide(matches)
    except VersionResolveError as e:
        assert "found 1" in str(e), str(e)
    else:
        raise AssertionError("expected VersionResolveError for a single intern")


# ---------------------------------------------------------------------------
# direct-run harness (no pytest required)
# ---------------------------------------------------------------------------

def _main():
    tests = [
        ("test_agreeing_fixture_resolves_to_pinned_value",
         test_agreeing_fixture_resolves_to_pinned_value),
        ("test_disagreeing_fixture_raises_both_sides",
         test_disagreeing_fixture_raises_both_sides),
        ("test_single_intern_fixture_raises_both_sides",
         test_single_intern_fixture_raises_both_sides),
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
