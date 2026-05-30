"""seeds_shared.version_resolver -- resolve a game version from a linked DLL.

The per-module version mechanism: scan every `.rdata` section of a linked module
DLL for the interned `release_M_N_BUILD_SUB` string, apply a hard
intern-agreement check, and return `(tag, ordinal)` for the agreed version.

This is an ALTERNATIVE to the importer's `whdlversions.json` path
(import_to_sqlite.py `read_game_version`), NOT a replacement -- the JSON path
stays valid. The incremental `apply` path resolves the target version from the
linked DLL via this module (db-updator Phase 1; plan.md §7). Wiring it into
`apply` is a later step -- this module is the resolver only.

The hard intern-agreement check is what makes a binary scan trustworthy: the
version string is interned MORE THAN ONCE in `.rdata` (two copies verified in
1.5.1164953, at va=0x183c3edef and va=0x183dba258). Requiring >=2 matches that
all decode to the same (M, N, BUILD, SUB) catches a hand-patched or corrupt
binary, where a single edited copy would otherwise be taken at face value. A
disagreement names each intern's VA + decoded value so a maintainer can find the
mismatched copy.
"""
import re

# release_<major>_<minor>_<build>_<sub>, interned as raw ASCII in .rdata.
# Groups: (M, N, BUILD, SUB). BUILD is the game's monotonic counter (the ordinal).
_VERSION_RE = re.compile(rb"release_(\d{1,3})_(\d{1,3})_(\d{4,8})_(\d{1,4})")


class VersionResolveError(Exception):
    """The linked DLL's .rdata did not yield a single agreed-upon version:
    fewer than 2 interned version strings, or interns that disagree."""


def _scan_rdata_matches(dll_path):
    """Open `dll_path` with pefile and return every `release_*` intern found in
    any `.rdata` section as a list of `((M, N, BUILD, SUB), va)` pairs.

    VA computation: `va = image_base + section.VirtualAddress + offset`, where
    `offset` is the match's byte offset within that section's raw data (the bytes
    pefile hands back from `section.get_data()`). For the live WHGame.dll this
    yields the two known interns near va=0x183c3edef and va=0x183dba258.

    There may be more than one `.rdata` section; all are scanned.
    """
    import pefile  # local import: pefile is only needed for the real scan path,
                   # not for the _decide unit tests.

    pe = pefile.PE(dll_path, fast_load=True)
    try:
        image_base = pe.OPTIONAL_HEADER.ImageBase
        matches = []
        for section in pe.sections:
            name = section.Name.rstrip(b"\x00").decode("latin-1")
            if name != ".rdata":
                continue
            data = section.get_data()
            section_va = image_base + section.VirtualAddress
            for m in _VERSION_RE.finditer(data):
                tup = (int(m.group(1)), int(m.group(2)),
                       int(m.group(3)), int(m.group(4)))
                va = section_va + m.start()
                matches.append((tup, va))
        return matches
    finally:
        pe.close()


def _fmt_match(match):
    """`((M,N,BUILD,SUB), va)` -> 'va=0x... -> M.N.BUILD.SUB' for messages."""
    (mj, mn, build, sub), va = match
    return f"va=0x{va:x} -> release_{mj}_{mn}_{build}_{sub}"


def _decide(matches):
    """Apply the count + agreement rules to a collected match list and return
    `(tag, ordinal)` or raise `VersionResolveError`. Pure -- no PE, no I/O --
    so the count/agreement logic is unit-testable on synthetic matches.

    Rules (plan.md §7):
      4. < 2 matches            -> raise (found N)
      5. matches disagree       -> raise, surfacing each distinct VA + value
      6. >= 2 matches, all agree -> return (f"{M}.{N}.{BUILD}", int(BUILD))
    """
    if len(matches) < 2:
        raise VersionResolveError(
            f"expected >=2 interned version strings in .rdata; found "
            f"{len(matches)}")

    distinct = {tup for tup, _va in matches}
    if len(distinct) != 1:
        # Surface EVERY match's VA + decoded value -- a maintainer needs to see
        # which interned copy disagrees to investigate a hand-patched binary.
        detail = "; ".join(_fmt_match(m) for m in matches)
        raise VersionResolveError(
            f"interned version strings in .rdata disagree "
            f"({len(distinct)} distinct values across {len(matches)} interns): "
            f"{detail}")

    (major, minor, build, _sub) = next(iter(distinct))
    tag = f"{major}.{minor}.{build}"
    ordinal = int(build)
    return (tag, ordinal)


def resolve_version(dll_path):
    """Resolve `dll_path`'s game version from its `.rdata` interns.

    Returns `(tag, ordinal)` -- e.g. `("1.5.1164953", 1164953)`. Raises
    `VersionResolveError` if fewer than 2 interns are found or they disagree.
    """
    return _decide(_scan_rdata_matches(dll_path))
