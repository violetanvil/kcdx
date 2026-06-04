"""seeds_shared.read_api -- the read-for-display surface of the data-core.

WHAT THIS IS (design D13, data/maintainer-tool/design.md S5 + S10 D13)
---------------------------------------------------------------------
The data-core is the SINGLE gate: the backend (and any future consumer) reads the
curated set / an entity's detail / its version rows -- and the DERIVED status --
by CALLING this module, never by re-querying the DB or re-deriving status itself.
The landed data-core was write+validate+export+round-trip with NO read-for-display
surface; this module adds it, and -- the load-bearing part -- gives the status
derivation a SINGLE home. The DEPRECATED/SUPERSEDED/VERIFIED/UNVERIFIED rule that
previously existed only as prose in data/seeds/policy.md S"Status is NOT an
authored column" is implemented ONCE here (derive_status); the backend reimplements
nothing, the frontend derives nothing in JS (both would be law-6 violations that
drift).

The DB is opened READ-ONLY (sqlite URI mode=ro): no write, no schema change. The
USER `reference.sqlite` (the curated set) is read -- the curated registry plus its
per-version resolve facts.

THE STATUS DERIVATION'S ORDINAL CONTRACT
----------------------------------------
The DB stores `*_at_version` / `valid_from` / `last_verified_at_version` as
`game_versions.id` FKs (schema.py), but the policy precedence compares ORDINALS
(policy.md S"Version comparison caveat": the engine's runtime derivation uses
ordinal compare). So derive_status takes ALREADY-RESOLVED integer ordinals -- the
read functions below resolve every FK->ordinal in ONE DB read (a
`game_versions.id -> ordinal` map) and hand derive_status plain ordinals. That
keeps derive_status a PURE policy function (no DB handle, oracle-testable with
plain integers) and keeps the id-vs-ordinal resolution in one place (the reader),
not smeared across the rule.
"""
import os
import sqlite3

# The four derived-status tokens (policy.md S"Status is NOT an authored column").
STATUS_DEPRECATED = "DEPRECATED"
STATUS_SUPERSEDED = "SUPERSEDED"
STATUS_VERIFIED = "VERIFIED"
STATUS_UNVERIFIED = "UNVERIFIED"


def derive_status(current_version_ordinal, version_row, entity):
    """The SINGLE implementation of policy.md's 4-rule status precedence -- the
    authority every consumer calls instead of re-deriving status.

    Returns exactly one of "DEPRECATED" / "SUPERSEDED" / "VERIFIED" / "UNVERIFIED".

    THE CONTRACT (ordinals in, never ids):
      current_version_ordinal -- the ordinal (int) of the game version status is
            derived AT. The read functions pass the DB's current/max
            game_versions.ordinal (the baseline the importer wrote).
      version_row             -- a mapping carrying the row's two version-window
            ordinals already resolved from their game_versions.id FKs:
              `valid_from_ordinal`               (int; the row's earliest-valid
                                                  version ordinal)
              `last_verified_ordinal`            (int or None; the latest version
                                                  ordinal the row was signed off
                                                  for -- None when never verified)
      entity                  -- a mapping carrying the entity-level lifecycle,
            with its version FKs already resolved to ordinals:
              `is_deprecated`                    (truthy 0/1)
              `deprecated_at_ordinal`            (int or None)
              `superseded_by`                    (truthy -- the successor id/name;
                                                  presence is what the rule tests)
              `superseded_at_ordinal`            (int or None)

    The precedence (read down -- policy.md S"Status is NOT an authored column",
    VERBATIM; rule N's else-arm is rule N+1):
      1. entity.is_deprecated AND current >= entity.deprecated_at -> DEPRECATED
      2. elif entity.superseded_by AND current >= entity.superseded_at -> SUPERSEDED
      3. elif row.last_verified >= current AND row.valid_from <= current -> VERIFIED
      4. else -> UNVERIFIED

    The `>=` / `<=` boundaries are inclusive (policy.md: the supersession/
    deprecation edge "applies >= this" version; VERIFIED holds when
    valid_from <= V <= last_verified). A new game version shipping (a higher
    current ordinal than a row's last_verified) flips that row VERIFIED->UNVERIFIED
    automatically by rule 3's left conjunct going false -- no row mutation.
    """
    # Rule 1 -- DEPRECATED: the entity is deprecated and the current version is at
    # or past where the deprecation took effect. A NULL deprecated_at with the flag
    # set cannot pass (the pair is both-or-neither per policy.md, but guard anyway:
    # None fails the >= compare cleanly).
    dep_at = entity.get("deprecated_at_ordinal")
    if entity.get("is_deprecated") and dep_at is not None \
            and current_version_ordinal >= dep_at:
        return STATUS_DEPRECATED

    # Rule 2 -- SUPERSEDED: the entity was renamed and the current version is at or
    # past where the supersession edge activates. `superseded_by` presence is the
    # left conjunct (the entity points at a successor); the version gate is the
    # right.
    sup_at = entity.get("superseded_at_ordinal")
    if entity.get("superseded_by") and sup_at is not None \
            and current_version_ordinal >= sup_at:
        return STATUS_SUPERSEDED

    # Rule 3 -- VERIFIED: the row's trusted window includes the current version --
    # last_verified >= current (the row was signed off for at least this version)
    # AND valid_from <= current (the row is authoritative from this version
    # forward). last_verified None (never verified) fails the first conjunct.
    last_verified = version_row.get("last_verified_ordinal")
    valid_from = version_row.get("valid_from_ordinal")
    if last_verified is not None and valid_from is not None \
            and last_verified >= current_version_ordinal \
            and valid_from <= current_version_ordinal:
        return STATUS_VERIFIED

    # Rule 4 -- UNVERIFIED: none of the above held.
    return STATUS_UNVERIFIED


# ---------------------------------------------------------------------------
# Read-only DB access + the FK->ordinal resolution the readers share.
# ---------------------------------------------------------------------------
def _open_ro(out_dir):
    """Open the USER reference.sqlite under `out_dir` READ-ONLY (sqlite URI
    mode=ro). Raises DbReadError if the DB file is absent (the consumer maps a
    missing DB to the s01 "no DB resolved" empty state). Row factory set to
    sqlite3.Row so columns are addressable by name."""
    db_path = os.path.join(out_dir, "reference.sqlite")
    if not os.path.isfile(db_path):
        raise DbReadError(
            f"no reference.sqlite under {out_dir!r}; the read surface needs the "
            f"curated USER DB (run a rebuild to create the baseline first)")
    # mode=ro: the file must exist (it does -- checked above) and is opened strictly
    # read-only, so a read-for-display call can never mutate the curated DB.
    con = sqlite3.connect(f"file:{db_path}?mode=ro", uri=True)
    con.row_factory = sqlite3.Row
    return con


class DbReadError(RuntimeError):
    """A read-surface error: the curated USER DB is absent under out_dir. A caller
    (the backend, step 2b) maps it to the s01 "no reference DB found" empty state."""


def _version_ordinals(con):
    """The `game_versions.id -> ordinal` map -- the single FK->ordinal lookup the
    readers use to resolve every *_at_version / valid_from / last_verified_at_version
    column to its comparable ordinal (policy.md S"ordinal compare")."""
    return {r["id"]: r["ordinal"]
            for r in con.execute("SELECT id, ordinal FROM game_versions")}


def _current_ordinal(con):
    """The current (max) game_versions.ordinal in the DB -- the baseline the
    importer wrote, the version status is derived at (step doc: do NOT hardcode
    GAME_VERSION_TAG -- read it). Reuses the same MAX(ordinal) read shape the
    importer's db_latest_ordinal helper uses, against the already-open connection."""
    row = con.execute("SELECT MAX(ordinal) FROM game_versions").fetchone()
    return row[0] if row else None


def _dict_decode(con, table, col):
    """The `id -> value` map for a dict-encoded column (the importer stores
    low-cardinality TEXT -- e.g. address_versions.kind / evidence_kind -- as a
    small INTEGER id in a `_dict_<table>_<col>` lookup table). Used to render the
    `kind` display string. Returns {} if the dict table is absent (a column the
    DB never populated has no dict table)."""
    has = con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        (f"_dict_{table}_{col}",)).fetchone()
    if not has:
        return {}
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _entity_lifecycle_ordinals(names_row, gv_ordinals):
    """Build derive_status's `entity` arg from an address_names row dict, resolving
    the deprecated_at_version / superseded_at_version game_versions.id FKs to their
    ordinals via `gv_ordinals`. Presence-of-successor (superseded_by) and the
    is_deprecated flag pass through as-is (the rule tests their presence/truthiness)."""
    return {
        "is_deprecated": names_row["is_deprecated"],
        "deprecated_at_ordinal": gv_ordinals.get(names_row["deprecated_at_version"]),
        "superseded_by": names_row["superseded_by"],
        "superseded_at_ordinal": gv_ordinals.get(names_row["superseded_at_version"]),
    }


def _version_window_ordinals(av_row, gv_ordinals):
    """Build derive_status's `version_row` arg from an address_versions row dict,
    resolving valid_from / last_verified_at_version game_versions.id FKs to ordinals."""
    return {
        "valid_from_ordinal": gv_ordinals.get(av_row["valid_from"]),
        "last_verified_ordinal": gv_ordinals.get(av_row["last_verified_at_version"]),
    }


def _current_av_row(con, kcdx_id):
    """The CURRENT address_versions row for an entity -- the open-interval row
    (valid_through IS NULL), the one the engine resolves at the current version. An
    entity ships with exactly one current curated row (schema.py: partial UNIQUE on
    (kcdx_id) WHERE valid_through IS NULL). Returns the sqlite3.Row or None."""
    return con.execute(
        "SELECT * FROM address_versions "
        "WHERE kcdx_id = ? AND valid_through IS NULL",
        (kcdx_id,)).fetchone()


# ---------------------------------------------------------------------------
# The three read-for-display entry points.
# ---------------------------------------------------------------------------
def read_curated_set(out_dir):
    """The curated entity list for s01 (the navigator): one dict per curated entity

        {"kcdx_id": int, "name": str, "status": str, "kind": str}

    `status` is derived via derive_status at the DB's current/max ordinal; `kind`
    is the entity's CURRENT-row kind (s01 S"Contents": the entity's current-row
    kind, decoded from the dict-encoded integer to its display string). Joins the
    USER address_names registry to its current address_versions row. Reads the DB
    READ-ONLY."""
    con = _open_ro(out_dir)
    try:
        gv_ordinals = _version_ordinals(con)
        current = _current_ordinal(con)
        kind_decode = _dict_decode(con, "address_versions", "kind")

        out = []
        for nrow in con.execute(
                "SELECT id, name, superseded_by, superseded_at_version, "
                "is_deprecated, deprecated_at_version "
                "FROM address_names ORDER BY id"):
            av = _current_av_row(con, nrow["id"])
            # The entity-level lifecycle (deprecation/supersession) drives rules 1-2
            # regardless of the version row; the version window drives rule 3. A
            # curated USER entity always has a current av row (every shipped entity
            # has >=1 baseline row), but guard with an empty window so an entity
            # missing one derives UNVERIFIED rather than raising.
            entity = _entity_lifecycle_ordinals(nrow, gv_ordinals)
            window = (_version_window_ordinals(av, gv_ordinals) if av is not None
                      else {"valid_from_ordinal": None,
                            "last_verified_ordinal": None})
            status = derive_status(current, window, entity)
            kind = (kind_decode.get(av["kind"]) if av is not None else None)
            out.append({
                "kcdx_id": nrow["id"],
                "name": nrow["name"],
                "status": status,
                "kind": kind,
            })
        return out
    finally:
        con.close()


def read_entity_detail(out_dir, kcdx_id):
    """The entity-level identity + lifecycle fields s02 renders, as a dict:

        {"kcdx_id", "name", "superseded_by", "superseded_at_version",
         "is_deprecated", "deprecated_at_version", "deprecation_replacement",
         "notes"}

    Returns None for an unknown kcdx_id -- the consumer (step 2b) maps None to a
    404 (the standard no-matching-row idiom; distinct from read_version_rows'
    empty-list, which means "entity has no rows" not "entity not found"). The
    version FK columns are returned as their game_versions.id values (the s02
    surface renders/resolves them); identity is returned verbatim. Reads READ-ONLY."""
    con = _open_ro(out_dir)
    try:
        row = con.execute(
            "SELECT id, name, superseded_by, superseded_at_version, "
            "is_deprecated, deprecated_at_version, deprecation_replacement, notes "
            "FROM address_names WHERE id = ?", (kcdx_id,)).fetchone()
        if row is None:
            return None
        return {
            "kcdx_id": row["id"],
            "name": row["name"],
            "superseded_by": row["superseded_by"],
            "superseded_at_version": row["superseded_at_version"],
            "is_deprecated": row["is_deprecated"],
            "deprecated_at_version": row["deprecated_at_version"],
            "deprecation_replacement": row["deprecation_replacement"],
            "notes": row["notes"],
        }
    finally:
        con.close()


# The READ CONTRACT for read_version_rows -- the EXPLICIT display/editable columns
# the maintainer sees on s02 (version table) + s03 (full-record compare), per design
# US-5 (the editable-columns spec) + policy.md. This is an allowlist: only these
# cross the wire. Three classes of address_versions column are DELIBERATELY EXCLUDED
# and never returned --
#   * content_hash -- the engine-computed BLAKE3 body fingerprint (a BLOB). DERIVED,
#     not maintainer-authored (policy.md S"function kinds need no survival authoring":
#     a function's survival datum is content_hash+length, reused by the importer,
#     never hand-authored); never shown on s02/s03. Dropping it is the point of this
#     contract -- the engine fingerprint stays out of the display surface.
#   * auto_name / decompile_quality -- schema.py marks both DEV-ONLY (bulk-discovery
#     scaffolding, not a curated display column).
#   * id -- the internal autoincrement PRIMARY KEY row handle, never a display column.
# The six folded survival columns (aob/anchor_string/rule/slot_count/expect_unique/
# derives_from -- D22/S11.2, the former `survival` sibling folded onto address_versions)
# ARE in the allowlist: they are the survival re-find data the maintainer authors and
# sees on s02/s03 (design US-5 "the six survival columns" + S11.2), curated display
# columns like offset/vtable_slot/struct_offset -- distinct from the DROPPED internal
# columns above.
# Order mirrors the schema/display grouping; valid_from + valid_through are REQUIRED
# (the newest-first sort + status derivation key on valid_from; valid_through marks
# the current/closed interval). The per-row derived `status` is ADDED on top (below);
# kind/evidence_kind are dict-decoded to their display strings (caller_arg_agreement
# stays a raw id -- today's surface does not decode it).
_VERSION_DISPLAY_COLUMNS = (
    "kcdx_id",                      # identity (read-only)
    "kind",                         # dict-decoded below
    "module_id",                    # module FK; surface resolves it (raw, as today)
    "rva", "length",
    "value",                        # authored per-kind datum
    "signature",
    "observed_arg_slots", "caller_reg_arg_count", "caller_arg_agreement",  # survival
    "offset", "vtable_slot", "struct_offset",   # authored consumer/vtable/struct cols
    "aob", "anchor_string", "rule", "slot_count", "expect_unique", "derives_from",  # folded survival cols (D22/S11.2): the survival re-find data the maintainer authors/sees on s02/s03 (US-5), now first-class av columns
    "last_verified_at_version", "verified_by", "verified_date", "evidence_kind",  # audit trio (+ek dict-decoded)
    "valid_from", "valid_through",  # identity / interval window (sort + status key on valid_from)
)


def read_version_rows(out_dir, kcdx_id):
    """The entity's address_versions rows, NEWEST-first (s02 S"Contents": newest
    first), each as a display-column dict carrying its derived `status` (via
    derive_status). Only the design DISPLAY/EDITABLE columns are returned (the
    _VERSION_DISPLAY_COLUMNS allowlist above -- s02 version table + s03 history/
    compare; the engine-computed content_hash and the DEV-ONLY columns never cross
    the wire); the dict-encoded `kind` / `evidence_kind` cells are decoded to their
    display strings, and an extra "status" key carries the per-row derived status at
    the DB's current ordinal.

    NEWEST-first is ordered by the row's valid_from ORDINAL descending (the version
    a row is valid from -- not the internal autoincrement id), so the current
    (highest valid_from) row leads. Returns [] for an unknown kcdx_id (no rows),
    distinguishable from None by read_entity_detail. Reads READ-ONLY."""
    con = _open_ro(out_dir)
    try:
        gv_ordinals = _version_ordinals(con)
        current = _current_ordinal(con)
        kind_decode = _dict_decode(con, "address_versions", "kind")
        ek_decode = _dict_decode(con, "address_versions", "evidence_kind")

        # The entity-level lifecycle (drives rules 1-2 for EVERY version row of this
        # entity -- a deprecated/superseded entity's rows all derive DEPRECATED/
        # SUPERSEDED regardless of their own verification window).
        nrow = con.execute(
            "SELECT superseded_by, superseded_at_version, is_deprecated, "
            "deprecated_at_version FROM address_names WHERE id = ?",
            (kcdx_id,)).fetchone()
        if nrow is None:
            return []
        entity = _entity_lifecycle_ordinals(nrow, gv_ordinals)

        cols = _VERSION_DISPLAY_COLUMNS
        rows = con.execute(
            f'SELECT {",".join(chr(34) + c + chr(34) for c in cols)} '
            f'FROM address_versions WHERE kcdx_id = ?', (kcdx_id,)).fetchall()

        out = []
        for r in rows:
            d = {c: r[c] for c in cols}
            window = _version_window_ordinals(d, gv_ordinals)
            d["status"] = derive_status(current, window, entity)
            # Decode the dict-encoded display columns to their string values (the
            # version table renders the kind/evidence_kind text, not the dict id).
            if d.get("kind") is not None:
                d["kind"] = kind_decode.get(d["kind"], d["kind"])
            if d.get("evidence_kind") is not None:
                d["evidence_kind"] = ek_decode.get(d["evidence_kind"],
                                                   d["evidence_kind"])
            out.append(d)

        # NEWEST-first: sort by the row's valid_from ORDINAL descending. A NULL
        # valid_from (should not occur on a curated row) sorts last.
        out.sort(
            key=lambda d: gv_ordinals.get(d.get("valid_from"), -1),
            reverse=True)
        return out
    finally:
        con.close()


def read_modules(out_dir):
    """The module registry for the s04 field editor's `module` Select: one dict per
    curated module

        {"id": int, "name": str, "path": str}

    Reads the `modules` table from the USER reference.sqlite (schema.py: id (the
    seed-assigned canonical module id, not autoincrement), name, path -- the
    install-relative module path). id-ascending (a stable registry order; modules.id
    comes from the seed, not a version ordinal). The backend SURFACES this as
    GET /modules -- it derives nothing (D13/law 6); this seam does the DB read. Reads
    READ-ONLY; raises DbReadError when no curated DB resolves under out_dir (the same
    no-DB signal the other read functions raise, so GET /modules surfaces the s01
    empty state, not a crash)."""
    con = _open_ro(out_dir)
    try:
        return [{"id": r["id"], "name": r["name"], "path": r["path"]}
                for r in con.execute(
                    "SELECT id, name, path FROM modules ORDER BY id")]
    finally:
        con.close()
