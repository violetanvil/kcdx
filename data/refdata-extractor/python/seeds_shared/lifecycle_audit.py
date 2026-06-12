"""seeds_shared.lifecycle_audit -- the entity-lifecycle COMPLETENESS audit (design D41).

WHAT THIS IS (design data/maintainer-tool/design.md D41 / policy.md S"Status is NOT
an authored column" / ui/screens/s09-needs-action.md S"Contents")
---------------------------------------------------------------------------------------
The data-core READS the curated DB and computes the NEEDS-ACTION set at the current
game version V -- the three version-relative incomplete-lifecycle kinds the write-time
HARD-ERROR checks (policy.md / validators.py: NULL ids, duplicate tuples, supersession
XOR, acyclic supersession, last_verified < valid_from, audit-trio all-or-nothing, FK
closure) cannot catch, because completeness AT A SPECIFIC VERSION V is a query-time
property the importer cannot know (D41 fact 1).

NO WRITE -- this module READS the DB (READ-ONLY, the read_api `_open_ro` seam) and
COMPUTES the needs-action set. The maintainer RESOLVES each gap via the canonical write
path (s02/s04/s05 -> _apply_one_db); the data-core is the sole writer (law 6 / D19) and
DETECTION is READ-ONLY. This module never opens a write connection.

THE THREE KINDS (D41 fact 1 / s09 S"Contents" rows 31-33 / plan-spec S"The detection
conditions" -- the conditions below MUST match those three authorities exactly):

  UNCOVERED-AT-V ORPHAN -- an address_names entity with NO address_versions interval
    covering V (`valid_from <= V <= valid_through` OR `valid_through IS NULL` -- an OPEN
    interval covers forward, src/refdb.cpp), AND `is_deprecated = 0`, AND
    `superseded_by IS NULL`. A deprecated/superseded entity is a COMPLETED lifecycle, NOT
    an orphan even when uncovered (policy.md rules 1-2 are the completed states; the
    orphan is the no-covering-interval subset of rule-4 UNVERIFIED where neither holds).
    Comparison is by game_versions.ordinal (resolve valid_from / valid_through FK ids ->
    ordinals; an entity is covered if ANY of its rows' interval contains V's ordinal).

  NEVER VERIFIED -- an address_versions row with `last_verified_at_version IS NULL`
    (authored but never signed off). One entry PER ROW (a row is the grain the maintainer
    verifies).

  BROKEN REFERENCE -- an entity whose `deprecation_replacement` OR `superseded_by` points
    at a target that is NONEXISTENT (no address_names row) OR ITSELF-INCOMPLETE (the
    target is itself in the uncovered-orphan set at V). See INTERPRETATION below for the
    precise "itself-incomplete" reading.

ORDERING (D41 / s09 S"Contents" + S"States"): Uncovered first (the most consequential --
an entity that resolves to nothing at V), then Never verified, then Broken references.

INTERPRETATION CALLS (surfaced -- spec items the build had to decide)
---------------------------------------------------------------------
1. "itself-incomplete" for a broken reference. The spec says a reference is broken when
   its target is "nonexistent or itself-incomplete." NONEXISTENT is unambiguous: no
   address_names row for the target id. For ITSELF-INCOMPLETE this module flags a target
   that is itself in the UNCOVERED-ORPHAN set at V (the spec's own floor: "for
   'itself-incomplete,' flag a target that is itself in the uncovered-orphan set"). A
   target that is merely never-verified is NOT treated as itself-incomplete here -- a
   replacement/successor that exists and is covered at V is a usable redirect even if a
   row lacks a sign-off; the orphan case (the target resolves to nothing at V) is the one
   that makes the redirect itself dead. This is the narrowest defensible reading that
   matches the spec floor and avoids over-flagging.

2. The reference COLUMNS are INTEGER FKs to address_names.id (schema.py: `superseded_by`,
   `deprecation_replacement` are `INTEGER` FKs), NOT name strings. The design prose
   ("pointing at a name") is the conceptual layer; the DB stores the id. The audit builds
   to the DB column (the id), per spec-conformance (build to the schema, not the prose).

3. V resolution. `current_version_tag=None` (the common path) defaults to the MAX-ordinal
   game_versions row -- "the current version" the importer wrote (read_api `_current_ordinal`
   shape). A passed-in tag resolves to that row's ordinal (an unknown tag raises).

THE RETURN SHAPE (the 2.1 backend endpoint -- a LATER step, not this one -- serves it)
-------------------------------------------------------------------------------------
    {
      "version": "<tag>",          # the V the incompleteness is measured at (s09 header)
      "version_ordinal": <int>,
      "uncovered":      [ {kcdx_id, name, gap}, ... ],   # gap: "closed -- no interval covers <V>"
      "never_verified": [ {kcdx_id, name, address_version_id, valid_from_version, gap}, ... ],
      "broken_refs":    [ {kcdx_id, name, field, target_kcdx_id, gap}, ... ],
    }
The three lists are ordered Uncovered / Never verified / Broken references. Each list
element carries the entity (kcdx_id + name) + the kind-specific gap detail the s09 row
renders. The total needs-action count the s01 badge shows is the sum of the three list
lengths.
"""
import os
import sqlite3

# The read-only DB-open + FK->ordinal/tag resolution the read surface already owns (law 6
# -- one place resolves a game_versions.id FK to its ordinal/tag). The audit reuses
# _open_ro (the same sqlite URI mode=ro the read endpoints use) + the FK maps rather than
# re-querying game_versions itself.
from .read_api import (
    _open_ro,
    _version_ordinals,
    _version_tags,
    DbReadError,
)
# The interval-containment-at-a-version primitive (D39). Reused here for the orphan check
# ("does kcdx_id have a row whose interval contains V?") -- the SAME containment query the
# close-intervals resolver uses, so the two paths cannot drift on what "covers V" means.
from .reverify_resolver import _interval_containing_row


class LifecycleAuditError(RuntimeError):
    """The audit could not resolve its inputs against the curated DB -- a structural
    mismatch the maintainer must see, NOT a silent skip (silent-success is the
    anti-pattern). Raised for an unknown `current_version_tag` (no game_versions row) or
    an empty game_versions table (no version to audit at)."""


# The address_names columns the audit reads -- the entity identity + the two lifecycle
# pairs + the two reference FKs the broken-ref check follows.
_NAMES_COLS = ("id", "name", "is_deprecated", "deprecated_at_version",
               "superseded_by", "superseded_at_version", "deprecation_replacement")
_NAMES_SELECT = ", ".join(_NAMES_COLS)


def _resolve_version(con, current_version_tag, gv_ordinals, gv_tags):
    """Resolve V to its (tag, ordinal). None -> the MAX-ordinal game_versions row (the
    current version the importer wrote). A passed tag -> that row's ordinal; an unknown
    tag raises (a typo the maintainer must see, never a silent skip)."""
    if not gv_ordinals:
        raise LifecycleAuditError(
            "game_versions is empty -- no current version to audit the lifecycle at "
            "(run a rebuild to create the baseline first)")
    if current_version_tag is None:
        # The MAX-ordinal row -- "the current version" (read_api _current_ordinal shape).
        max_id = max(gv_ordinals, key=lambda gid: gv_ordinals[gid])
        return gv_tags[max_id], gv_ordinals[max_id]
    # A passed tag -> its ordinal (reverse-lookup the gv_tags map).
    for gid, tag in gv_tags.items():
        if tag == current_version_tag:
            return tag, gv_ordinals[gid]
    raise LifecycleAuditError(
        f"current_version_tag={current_version_tag!r} names no game_versions row "
        f"(known tags: {sorted(gv_tags.values())})")


def _uncovered_orphans(con, names_rows, v_ordinal, gv_ordinals):
    """The uncovered-at-V orphan set: an entity with NO interval covering V, AND
    is_deprecated == 0, AND superseded_by IS NULL (D41 / s09 row 31). A deprecated or
    superseded entity is a COMPLETED lifecycle -- excluded even when uncovered.

    "Covers V" reuses _interval_containing_row (the close-intervals containment query):
    an entity is covered iff ANY of its rows' interval contains V's ordinal (an OPEN row
    covers forward). Returns a set of orphan kcdx_ids (for the broken-ref itself-
    incomplete check) PLUS the ordered entry list."""
    orphan_ids = set()
    entries = []
    for nrow in names_rows:
        # COMPLETED lifecycle -- a deprecated or superseded entity is not an orphan (the
        # two columns are the policy.md rule-1/rule-2 completed states; the orphan is the
        # rule-4 no-covering-interval subset where NEITHER holds). The presence of a
        # successor (superseded_by non-NULL) / the deprecation flag is what excludes it --
        # the version gate is irrelevant here (a deprecated/superseded entity is a closed
        # lifecycle regardless of when it activates; an uncovered-at-V entity that is ALSO
        # marked deprecated/superseded is being handled, not orphaned).
        if nrow["is_deprecated"]:
            continue
        if nrow["superseded_by"] is not None:
            continue
        # Covered? -- ANY interval of this entity contains V's ordinal.
        covering = _interval_containing_row(con, nrow["id"], v_ordinal, gv_ordinals)
        if covering is not None:
            continue
        orphan_ids.add(nrow["id"])
        entries.append({
            "kcdx_id": nrow["id"],
            "name": nrow["name"],
            "gap": "closed -- no interval covers the current version",
        })
    return orphan_ids, entries


def _never_verified(con):
    """The never-verified set: every address_versions row with last_verified_at_version
    IS NULL (D41 / s09 row 32) -- authored but never signed off. One entry PER ROW (the
    row is the verify grain). Joins to address_names for the entity name; orders by
    (kcdx_id, valid_from) deterministically."""
    entries = []
    for r in con.execute(
            "SELECT av.kcdx_id AS kcdx_id, av.id AS av_id, "
            "       av.valid_from AS valid_from, n.name AS name "
            "FROM address_versions av "
            "JOIN address_names n ON n.id = av.kcdx_id "
            "WHERE av.kcdx_id IS NOT NULL "
            "  AND av.last_verified_at_version IS NULL "
            "ORDER BY av.kcdx_id, av.valid_from, av.id"):
        entries.append({
            "kcdx_id": r["kcdx_id"],
            "name": r["name"],
            "address_version_id": r["av_id"],
            "valid_from_version": r["valid_from"],
            "gap": "never signed off",
        })
    return entries


def _broken_refs(names_rows, names_by_id, orphan_ids):
    """The broken-reference set: an entity whose deprecation_replacement OR superseded_by
    points at a target that is NONEXISTENT (no address_names row) OR ITSELF-INCOMPLETE
    (the target is itself an uncovered-orphan at V -- see module INTERPRETATION). One
    entry per (entity, field) broken edge; ordered by (kcdx_id, field). The reference
    columns are INTEGER FKs to address_names.id (schema.py), so the target is an id."""
    entries = []
    # The two reference fields, checked in a stable order so a row with BOTH broken edges
    # surfaces deprecation_replacement first, then superseded_by (deterministic).
    for nrow in names_rows:
        for field in ("deprecation_replacement", "superseded_by"):
            target = nrow[field]
            if target is None:
                continue
            if target not in names_by_id:
                gap = f"{field} -> {target} (no such entity)"
            elif target in orphan_ids:
                gap = f"{field} -> {target} (target is itself uncovered at the current version)"
            else:
                continue  # the target exists and is not an orphan -> a usable redirect.
            entries.append({
                "kcdx_id": nrow["id"],
                "name": nrow["name"],
                "field": field,
                "target_kcdx_id": target,
                "gap": gap,
            })
    return entries


def audit_lifecycle(out_dir, current_version_tag=None):
    """Compute the needs-action set at the current game version V (D41 fact 1).

    Args:
      out_dir: the dir holding the curated USER reference.sqlite (the read_api _open_ro
        seam). READ-ONLY -- the audit never writes.
      current_version_tag: the version tag V to measure incompleteness at, OR None (the
        common path) for the MAX-ordinal game_versions row (the current version).

    Returns the dict described in the module docstring: {version, version_ordinal,
    uncovered[], never_verified[], broken_refs[]} -- the three kinds ordered Uncovered /
    Never verified / Broken references, each a list of entity-keyed gap dicts.

    Raises LifecycleAuditError on an unknown tag / an empty game_versions; DbReadError
    (from _open_ro) when the curated DB is absent under out_dir."""
    con = _open_ro(out_dir)
    try:
        gv_ordinals = _version_ordinals(con)
        gv_tags = _version_tags(con)
        v_tag, v_ordinal = _resolve_version(
            con, current_version_tag, gv_ordinals, gv_tags)

        names_rows = list(con.execute(
            f"SELECT {_NAMES_SELECT} FROM address_names ORDER BY id"))
        names_by_id = {r["id"]: r for r in names_rows}

        orphan_ids, uncovered = _uncovered_orphans(
            con, names_rows, v_ordinal, gv_ordinals)
        never_verified = _never_verified(con)
        broken_refs = _broken_refs(names_rows, names_by_id, orphan_ids)

        return {
            "version": v_tag,
            "version_ordinal": v_ordinal,
            "uncovered": uncovered,
            "never_verified": never_verified,
            "broken_refs": broken_refs,
        }
    finally:
        con.close()
