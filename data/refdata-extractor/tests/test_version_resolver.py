"""test_version_resolver.py -- the test for seeds_shared/version_resolver.py
(db-updator Phase 1; plan.md §7).

POSITIVE -- against the real linked DLL at third-party-ghidra/WHGame.dll:
  resolve_version(dll) == ("1.5.1164953", 1164953), AND the scan found >=2
  interns (the agreement-check path actually fired, not just the happy return).
  If the heavy DLL is absent (e.g. CI), the test SKIPS with a clear reason.

NEGATIVE -- pure-function `_decide` over synthetic match lists, so the count +
agreement rules are exercised fast and deterministically without touching (or
corrupting) the real binary:
  - []                       -> raises (found 0, < 2)
  - [one match]              -> raises (found 1, < 2)
  - [two agreeing matches]   -> returns ("1.5.1164953", 1164953)
  - [two disagreeing]        -> raises; message carries both VAs + both values

RUN
---
    python tests/test_version_resolver.py
    pytest tests/test_version_resolver.py
"""
import os
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)

from seeds_shared.version_resolver import (  # noqa: E402
    resolve_version,
    _scan_rdata_matches,
    _decide,
    VersionResolveError,
)


def _repo_root():
    """Walk up from this file to the repo root (the dir holding CLAUDE.md /
    third-party-ghidra), so the DLL path is not a hardcoded machine path."""
    d = HERE
    for _ in range(8):
        if os.path.isdir(os.path.join(d, "third-party-ghidra")):
            return d
        parent = os.path.dirname(d)
        if parent == d:
            break
        d = parent
    return None


def _live_dll_path():
    root = _repo_root()
    if root is None:
        return None
    p = os.path.join(root, "third-party-ghidra", "WHGame.dll")
    return p if os.path.isfile(p) else None


# ---------------------------------------------------------------------------
# POSITIVE -- real DLL (skips if the heavy binary is absent)
# ---------------------------------------------------------------------------

def test_resolve_live_dll():
    dll = _live_dll_path()
    if dll is None:
        _skip("third-party-ghidra/WHGame.dll not present "
              "(heavy binary, git-ignored); positive scan test skipped")
        return
    # >=2-intern path must actually fire: assert the scan found the agreeing
    # interns, not just that the happy return came back.
    matches = _scan_rdata_matches(dll)
    assert len(matches) >= 2, (
        f"expected >=2 interns in the live DLL's .rdata, found {len(matches)}: "
        f"{matches}")
    assert resolve_version(dll) == ("1.5.1164953", 1164953)


# ---------------------------------------------------------------------------
# NEGATIVE -- pure _decide over synthetic matches (no real PE touched)
# ---------------------------------------------------------------------------

_AGREE_A = ((1, 5, 1164953, 7490), 0x183c3edef)
_AGREE_B = ((1, 5, 1164953, 7490), 0x183dba258)
_DISAGREE = ((1, 6, 2000000, 1), 0x183dba258)


def test_decide_empty_raises():
    try:
        _decide([])
    except VersionResolveError as e:
        assert "found 0" in str(e), str(e)
    else:
        raise AssertionError("expected VersionResolveError for 0 matches")


def test_decide_single_raises():
    try:
        _decide([_AGREE_A])
    except VersionResolveError as e:
        assert "found 1" in str(e), str(e)
    else:
        raise AssertionError("expected VersionResolveError for 1 match")


def test_decide_two_agree_returns():
    assert _decide([_AGREE_A, _AGREE_B]) == ("1.5.1164953", 1164953)


def test_decide_disagree_raises_with_detail():
    try:
        _decide([_AGREE_A, _DISAGREE])
    except VersionResolveError as e:
        msg = str(e)
        # both VAs surfaced
        assert "0x183c3edef" in msg, msg
        assert "0x183dba258" in msg, msg
        # both decoded values surfaced
        assert "release_1_5_1164953_7490" in msg, msg
        assert "release_1_6_2000000_1" in msg, msg
    else:
        raise AssertionError("expected VersionResolveError for disagreeing matches")


# ---------------------------------------------------------------------------
# direct-run harness (no pytest required)
# ---------------------------------------------------------------------------

_skipped = []


def _skip(reason):
    _skipped.append(reason)
    print(f"  SKIP: {reason}")


def _main():
    tests = [
        ("test_decide_empty_raises", test_decide_empty_raises),
        ("test_decide_single_raises", test_decide_single_raises),
        ("test_decide_two_agree_returns", test_decide_two_agree_returns),
        ("test_decide_disagree_raises_with_detail",
         test_decide_disagree_raises_with_detail),
        ("test_resolve_live_dll", test_resolve_live_dll),
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
    print(f"\nALL PASSED ({len(tests) - len(_skipped)} ran, "
          f"{len(_skipped)} skipped)")


if __name__ == "__main__":
    _main()
