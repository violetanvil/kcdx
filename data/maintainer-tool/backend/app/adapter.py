"""app.adapter -- maps a chosen game-version tag to the params the data-core needs
(no DLL is read server-side).

THE PROBLEM (the load-bearing integration question)
---------------------------------------------------
The data-core write functions (seeds_shared.db_editor.*) take `(out_dir, dll_path,
...)`. `dll_path` is a DESKTOP assumption: the data-core's apply path resolves the
target game version by pefile-scanning a linked WHGame.dll's `.rdata` interns
(import_to_sqlite.apply_seeds first resolves `tag, ordinal = resolve_version(dll_path)`,
then a gate that REFUSES unless `tag == GAME_VERSION_TAG`). The DLL is used for
NOTHING ELSE -- its sole product is the `(tag, ordinal)` pair.

The web backend has NO DLL server-side: the image carries only app code; the client
resolves a DLL in-browser and sends only the version TAG. So the adapter maps a
maintainer-CHOSEN version TAG to the version-context params the data-core needs in
place of a DLL scan -- a thin adapter, no DLL server-side.

WHAT THIS ADAPTER OWNS
----------------------
Resolving + validating a chosen version tag against the KNOWN versions the server
holds (the built DB's `game_versions` rows, with the data-core baseline constant
as the floor when no DB resolves), producing a `VersionContext(tag, ordinal)` --
the exact pair `resolve_version(dll_path)` would have produced from a DLL of that
version. This is the "resolved version another way": the tag is validated to be a
real game version, and resolved to its ordinal, with NO DLL read.

It holds NO data-core rule logic -- it does not validate seed content, run SQL, or
touch the write gate. It maps a tag to the version facts; the data-core's gate is
still the single validator on any write.

HOW THE RESOLVED VersionContext REACHES THE DATA-CORE
-----------------------------------------------------
How a chosen tag threads into a data-core call whose signature accepted only
`dll_path` is settled. The data-core's apply_seeds + the five db_editor functions
take an OPTIONAL pre-resolved `version=(tag, ordinal)` param: supply `version` and
the data-core skips the DLL `.rdata` scan entirely (no DLL server-side). So a save
endpoint passes `version=(ctx.tag, ctx.ordinal)` + `dll_path=None` -- the adapter's
`VersionContext` IS the data-core's param. There is no `dll_path` to fabricate; the
adapter resolves the context, the endpoint passes it. This adapter therefore owns
ONLY the tag -> `VersionContext` resolution; how it is consumed (the `version=`
keyword on a data-core call -- a Save's dry validate `validate_only=True`, a
Confirm's write) is the endpoint's (app.routes_save / the confirm path), not a
translation step here.
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
    """The version tags the server holds (the dropdown is populated from the known
    game versions). Source order:

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
        # data-core's.
        import import_to_sqlite as imp
        versions[imp.GAME_VERSION_TAG] = int(imp.GAME_VERSION_ORDINAL)
    return versions


def resolve_tag(config, version_tag):
    """Map a maintainer-chosen version TAG to its `VersionContext(tag, ordinal)` --
    the version params the data-core needs in place of a DLL scan. Validates the tag
    against the known versions the server holds (`known_versions`); an unknown tag
    raises `VersionTagError` (surfaced, never guessed).

    This is the "resolved version another way": the equivalent of
    `resolve_version(dll_path)` for a DLL of `version_tag`, with NO DLL read.
    """
    versions = known_versions(config)
    if version_tag not in versions:
        raise VersionTagError(
            f"version tag {version_tag!r} is not a known game version "
            f"(known: {sorted(versions)}); the server holds no baseline for it")
    return VersionContext(tag=version_tag, ordinal=versions[version_tag])
