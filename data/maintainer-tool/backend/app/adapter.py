"""app.adapter -- the version-tag -> data-core-params adapter (design S5 / D15 / D18).

THE PROBLEM (design S5, the step's load-bearing integration question)
---------------------------------------------------------------------
The data-core write functions (seeds_shared.db_editor.*) take `(out_dir, dll_path,
...)`. `dll_path` is a DESKTOP assumption: the data-core's apply path resolves the
target game version by pefile-scanning a linked WHGame.dll's `.rdata` interns
(import_to_sqlite.apply_seeds step 1: `tag, ordinal = resolve_version(dll_path)`,
then a gate that REFUSES unless `tag == GAME_VERSION_TAG`). The DLL is used for
NOTHING ELSE -- its sole product is the `(tag, ordinal)` pair.

The web backend has NO DLL server-side (D14/D18: the image carries only app code;
the client resolves a DLL in-browser and sends only the version TAG, D15). So the
adapter maps a maintainer-CHOSEN version TAG to the version-context params the
data-core needs in place of a DLL scan (S5: "a thin adapter maps a chosen version
tag -> the data-core's params -- no DLL server-side").

WHAT THIS ADAPTER OWNS (fully design-determined -- built here)
--------------------------------------------------------------
Resolving + validating a chosen version tag against the KNOWN versions the server
holds (the built DB's `game_versions` rows, with the data-core baseline constant
as the floor when no DB resolves), producing a `VersionContext(tag, ordinal)` --
the exact pair `resolve_version(dll_path)` would have produced from a DLL of that
version. This is the "resolved version another way" the step doc names: the tag is
validated to be a real game version, and resolved to its ordinal, with NO DLL read.

It holds NO data-core rule logic -- it does not validate seed content, run SQL, or
touch the write gate. It maps a tag to the version facts; the data-core's gate is
still the single validator on any write (D13/R3).

WHAT IS DELIBERATELY *NOT* WIRED HERE (a surfaced design fork -- steps 2-5)
--------------------------------------------------------------------------
HOW the resolved `VersionContext` reaches the data-core's WRITE call is an open
integration decision, because the data-core's write signature accepts only
`dll_path` and calls `resolve_version(dll_path)` ITSELF -- there is no tag-accepting
seam in the data-core today. The candidate mechanisms (extend the data-core to
accept a pre-resolved (tag, ordinal); have the adapter inject the resolved pair
past resolve_version at call time; or hold one canonical reference DLL server-side)
are a design choice the user owns (design-authority) and are NOT needed for THIS
step (s01 ships no read/save/commit endpoint). The adapter therefore resolves the
version context and STOPS at the data-core's param boundary; `data_core_dll_param`
raises a clear NotImplementedError naming the fork rather than fabricating a
dll_path. The decision is surfaced in the step report.
"""
import os
import sqlite3
from dataclasses import dataclass

from . import data_core


class VersionTagError(ValueError):
    """A chosen version tag is not a known game version (it matches no
    game_versions row the server holds). A caller-facing error -- the maintainer
    picked a tag the server has no baseline for; surfaced, never guessed past."""


@dataclass(frozen=True)
class VersionContext:
    """The version-context params the data-core derives from a DLL, supplied
    instead from a chosen tag. `(tag, ordinal)` is exactly what
    `resolve_version(dll_path)` returns for a DLL of this version -- the adapter's
    product, with no DLL read."""
    tag: str
    ordinal: int


def known_versions(config):
    """The version tags the server holds (design D10/US-10: the dropdown is
    populated from the known game versions). Source order:

      1. the built USER reference DB's `game_versions` rows, when the configured
         checkout resolves a DB (the authoritative live set -- the same rows the
         data-core's apply gate resolves a tag against);
      2. the data-core's baseline version constant, as the floor, when no DB
         resolves yet (the empty-checkout case still offers the one baseline tag
         the data-core's gate accepts).

    Returns {tag: ordinal}. Reads the DB read-only; never writes. Holds NO rule
    logic -- it reads the data-core's own version table / constant, it does not
    invent versions.
    """
    versions = {}
    db_path = config.user_db
    if os.path.isfile(db_path):
        con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
        try:
            has = con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "AND name='game_versions'").fetchone()
            if has:
                for tag, ordinal in con.execute(
                        "SELECT tag, ordinal FROM game_versions"):
                    versions[tag] = int(ordinal)
        finally:
            con.close()
    if not versions:
        # The data-core baseline constant is the floor (import_to_sqlite carries
        # GAME_VERSION_TAG / GAME_VERSION_ORDINAL). Read it from the data-core, not
        # a backend copy, so the one source of the baseline version is the
        # data-core's (D13/R3).
        import import_to_sqlite as imp
        versions[imp.GAME_VERSION_TAG] = int(imp.GAME_VERSION_ORDINAL)
    return versions


def resolve_tag(config, version_tag):
    """Map a maintainer-chosen version TAG to its `VersionContext(tag, ordinal)` --
    the version params the data-core needs in place of a DLL scan (design S5).
    Validates the tag against the known versions the server holds (`known_versions`);
    an unknown tag raises `VersionTagError` (surfaced, never guessed).

    This is the "resolved version another way" the step names: the equivalent of
    `resolve_version(dll_path)` for a DLL of `version_tag`, with NO DLL read.
    """
    versions = known_versions(config)
    if version_tag not in versions:
        raise VersionTagError(
            f"version tag {version_tag!r} is not a known game version "
            f"(known: {sorted(versions)}); the server holds no baseline for it")
    return VersionContext(tag=version_tag, ordinal=versions[version_tag])


def data_core_dll_param(version_context):
    """The data-core's write functions take a `dll_path`; the backend has no DLL.
    Threading the already-resolved `version_context` into a data-core WRITE call
    is an OPEN design fork (the data-core's write signature accepts only a DLL path
    and calls resolve_version itself -- there is no tag-accepting seam in the
    data-core today). That decision is the user's (design-authority) and is not
    needed for s01 (no write endpoint yet).

    Raising here -- rather than fabricating a dll_path -- keeps the adapter honest:
    it resolves the version context (its design-determined job) and refuses to
    invent the unsettled threading. The write steps (2-5) replace this once the
    user settles the mechanism (surfaced in the step report)."""
    raise NotImplementedError(
        "threading a resolved version tag into the data-core's dll_path-shaped "
        "write call is an unsettled integration decision (the data-core write "
        "signature accepts only dll_path and resolves the version itself); "
        "surfaced to the user, settled before steps 2-5 wire the write path")
