"""import_to_sqlite.py -- the maintainer-side import: CSV-per-table dump dirs +
the curated seed TRIPLE (module_seed.csv + address_names_seed.csv +
address_versions_seed.csv) -> two encoded SQLite reference DBs on the
FLATTENED address-name/address-version schema.

PRODUCES TWO artifacts from one full-dump dir + the curated seed triple:
  - USER DB  (<out>/reference.sqlite, ships in every kcdx release): the curated
    set only. Tables: modules, game_versions, address_names (the curated names),
    address_versions (filtered to curated kcdx_ids; the per-version resolve facts
    -- rva, length, content_hash, signature, kind, etc.), meta. Powers the per-
    launch cross-version survival check + the ABI a hook needs at install.
  - DEV DB   (<out>/reference-dev.sqlite, on-demand author download): the bulk
    superset -- address_versions for every binary function (the 321K), plus
    statements + referenced_vars + call_edges, plus the dev-only columns
    (auto_name, decompile_quality, source, notes). The discovery surface for
    `kcdx.find`.

TWO MODES:
  - UPDATE (default): the per-version incremental path. Reads the most-recent
    version already in the DB, reads the game's on-disk version, and -- if the
    on-disk version is newer -- runs the version-update import (append a new
    game_versions row + the matcher's close/open of intervals). The DB is
    append-only / updated in place (NOT rebuilt). The version-update APPEND itself
    needs the cross-version matcher, which is separate, not-yet-built work; until
    it lands, update mode resolves the version comparison and reports either
    "already current" or "newer version -> matcher required (not yet implemented)".
  - REBUILD (--rebuild): the from-scratch baseline build from a dump dir. Builds
    the v1.5 baseline; also the path to use if the schema itself changes.

THE ON-DISK VERSION SOURCE (verified): the game DLL carries NO PE version
resource. The version is in <game>/whdlversions.json, which holds PER-CONFIGURATION
build ids; the SHIPPED config is MasterMasterPGO (the live game runs from
Bin/Win64MasterMasterSteamPGO/). The detector reads that config's versionId, whose
build number (e.g. 1164953) is the game's own monotonic counter -> the ordinal
(backfill-safe); tag = branch (release_1_5) + build -> "1.5.1164953".

SCHEMA (FLATTENED 2026-05-28; 5 user tables + 3 DEV-only + _dict_* lookups):
  USER: modules, game_versions, address_names, address_versions, meta.
  DEV adds: statements, referenced_vars, call_edges.

  address_names  (id PK, name, is_deprecated, superseded_by, source [DEV],
                  notes [DEV]) -- the curated NAME registry. `id` IS the kcdx_id
                  (the stable cross-version handle plugins reference); one row
                  per entity. superseded_by -> another address_names.id (entity-
                  level deprecation: "this entity is replaced by that one").
  address_versions (id PK, kcdx_id non-unique, kind, module_id, rva, length,
                    content_hash, value, signature, observed_arg_slots,
                    caller_reg_arg_count, caller_arg_agreement, offset,
                    vtable_slot, status, auto_name [DEV], decompile_quality [DEV],
                    valid_from, valid_through) -- per-(entity, version-interval)
                    resolve facts. Partial UNIQUE (kcdx_id) WHERE valid_through
                    IS NULL enforces "at most one current form per entity."

  Every table has an autoincrement INTEGER PK `id`. kcdx_id is the stable
  cross-version handle; it lives on address_versions (and is referenced by
  address_names + the DEV statements/referenced_vars/call_edges).

ID AUTHORITY:
  Every functions/ row is assigned a kcdx_id 1..N in ascending-rva order. That
  rva->kcdx_id dict is THE authority: every other table maps its
  function_rva/caller_rva/callee_rva through it. The 6 curated vtable-index seed
  rows (ids 3000-3005, empty rva) get NEW kcdx_ids appended after the bulk.

ENCODING (lossless):
  - content_hash 64-hex TEXT  -> 32-byte BLOB
  - low-cardinality TEXT (kind, storage_kind, data_type, source, status,
    agreement, decompile_quality, ...) -> INTEGER FK into a per-(table,col)
    `_dict_*` lookup table
  - hex/decimal address + count columns -> INTEGER
  Everything else stays TEXT. PRAGMA journal_mode=OFF / synchronous=OFF /
  page_size=4096; batched executemany; VACUUM at the end.

Run:
  python import_to_sqlite.py <dump_dir> <out_dir>
    -> <out_dir>/reference.sqlite       (USER)
       <out_dir>/reference-dev.sqlite   (DEV / full)
"""
import csv
import glob
import os
import re
import sqlite3
import sys
import time

# Allow very large quoted CSV fields (seed notes can be long).
csv.field_size_limit(1 << 24)

# ---------------------------------------------------------------------------
# Constants for this baseline import (single game version).
# ---------------------------------------------------------------------------
GAME_VERSION_TAG = "1.5.1164953"
GAME_VERSION_ORDINAL = 1164953
GAME_VERSION_ID = 1            # valid_from / valid_through FK target for baseline
SCHEMA_VERSION = 1
ABI_CONFIDENCE = "count+width+caller_reg"

# Three seed files (split 2026-05-28). Shape mirrors the DB tables 1:1 -- a
# new game version appends rows to the versions seed; the names seed is left
# alone unless an entity is renamed (supersession) or deprecated. All three
# files use CANONICAL maintainer-supplied ids; the importer fails loud on null
# or duplicate id, on an unknown FK, and on duplicate (kcdx_id, game_version)
# in the versions seed.
#
#   module_seed.csv             -- (id, name, path) registry of every module
#                                  the curated address-set can reference.
#                                  Today: one row (WHGame.dll).
#
#   address_names_seed.csv      -- per-entity, stable forever (the things that
#                                  NEVER change between game versions):
#                                    id, name,
#                                    superseded_by, superseded_at_version,
#                                    is_deprecated, deprecated_at_version,
#                                    deprecation_replacement,
#                                    source, notes
#                                  Each row's `id` becomes address_names.id
#                                  (== kcdx_id, the stable cross-version handle
#                                  plugins reference).
#
#   address_versions_seed.csv   -- per-(kcdx_id, game_version) (the things that
#                                  CAN change between game versions):
#                                    kcdx_id, game_version, module, rva,
#                                    status, signature
#                                  When KCD2 ships a new build, this file gets
#                                  new rows; the names file is unchanged. Every
#                                  kcdx_id must resolve to an address_names_seed
#                                  row; (kcdx_id, game_version) is unique.
HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))  # data/refdata-extractor/python -> repo
SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")
MODULE_SEED_CSV           = os.path.join(SEED_DIR, "module_seed.csv")
ADDRESS_NAMES_SEED_CSV    = os.path.join(SEED_DIR, "address_names_seed.csv")
ADDRESS_VERSIONS_SEED_CSV = os.path.join(SEED_DIR, "address_versions_seed.csv")

# Dict-encoded columns, keyed by the SCHEMA table+column name (post-transform).
# `address_names.source` REMOVED 2026-05-28 (carried no information; "verified"
# duplicated status, which is now derived). `address_versions.status` REMOVED
# 2026-05-28 (status is derived from verification audit columns + entity
# flags). `address_versions.evidence_kind` ADDED 2026-05-28.
DICT_COLS = {
    "address_versions":      ["kind", "caller_arg_agreement",
                              "decompile_quality", "evidence_kind"],
    "statements":            ["kind"],
    "referenced_vars":       ["storage_kind", "data_type"],
}

# USER db column allowlists (per table). Tables NOT listed here are DEV-only.
# A column omitted from the list is dropped from the USER CREATE TABLE + insert.
# Under the FLATTENED schema (2026-05-28) there is no entities/entity_versions
# split: address_names is the (kcdx_id, name) registry, address_versions carries
# every per-version resolve fact for any curated address (the kcdx_id is the
# stable handle plugins reference). For the USER (production) DB, only address-
# names whose kcdx_id has at least one row in address_versions ship; bulk
# discovery functions live in DEV-only tables under their own bulk id-space.
USER_COLUMNS = {
    "modules":          ["id", "name", "path"],
    "game_versions":    ["id", "tag", "ordinal", "released"],
    "address_names":    ["id", "name",
                         "superseded_by", "superseded_at_version",
                         "is_deprecated", "deprecated_at_version",
                         "deprecation_replacement", "notes"],
    # `id` IS the kcdx_id (the stable cross-version handle); no separate kcdx_id
    # column. address_versions.kcdx_id references address_names.id.
    # excludes notes (DEV-only).
    # The verification audit columns ship to USER -- the engine needs them to
    # derive status at resolve time (a plugin author resolving target="X" at
    # current game_version V needs to see "is this row verified at V?", which
    # is exactly the (valid_from <= V <= last_verified_at_version) test).
    "address_versions": ["id", "kcdx_id", "kind", "module_id", "rva", "length",
                         "content_hash", "value", "signature",
                         "observed_arg_slots", "caller_reg_arg_count",
                         "caller_arg_agreement", "offset", "vtable_slot",
                         "last_verified_at_version", "verified_by",
                         "verified_date", "evidence_kind",
                         "valid_from", "valid_through"],
    # excludes auto_name, decompile_quality (DEV-only discovery labels)
    "meta":             ["id", "schema_version", "abi_confidence"],
}

# Full schema (DEV): every table, every column, with its SQL column type.
# BLOB = content_hash; INTEGER = dict/int/fk; TEXT = the rest.
SCHEMA = {
    # modules.id and address_names.id are NOT autoincrement -- both come from
    # the seed files (module_seed.csv.id, address_names_seed.csv.id). The importer
    # fails loud on null or duplicate id in either file. address_versions.id
    # stays autoincrement (internal row id; hypothetically could change between
    # rebuilds and that's fine as long as resolution still walks correctly).
    "modules": [
        ("id", "INTEGER PRIMARY KEY"),            # from module_seed.csv.id
        ("name", "TEXT"),
        ("path", "TEXT"),                         # install-relative path
    ],
    "game_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("tag", "TEXT"),
        ("ordinal", "INTEGER"),
        ("released", "TEXT"),
    ],
    # address_names: ONE ROW PER CURATED ENTITY. `id` is the canonical kcdx_id
    # supplied by address_names_seed.csv (NOT autoincrement; the importer
    # fails loud on null or duplicate id in either seed file). The id is
    # stable across rebuilds for the same address_names_seed.csv row.
    #
    # Two distinct entity-level events, each with its own version anchor:
    #
    # (1) SUPERSESSION (cosmetic rename, version-gated, engine AUTO-FOLLOWS):
    #     `superseded_by` -> address_names.id of the direct successor (NOT the
    #     terminal -- the engine walks the chain at query time, applying the
    #     version filter at each hop). `superseded_at_version` -> game_versions.id;
    #     the supersession edge becomes active at that version inclusive. Both
    #     paired (both NULL or both NOT NULL). Resolution: at any game version V,
    #     resolve(name) walks the chain, following each edge IFF
    #     edge.superseded_at_version.ordinal <= V; stops on the first row whose
    #     active edge is NULL.
    #
    # (2) DEPRECATION (entity behavior changed; engine WARNS but does not
    #     redirect): `is_deprecated` 0/1 paired with `deprecated_at_version`.
    #     The optional `deprecation_replacement` -> address_names.id is an
    #     advisory pointer surfaced in the warning ("X is deprecated; consider
    #     switching to Y") -- the engine does NOT auto-follow it. The two flags
    #     are orthogonal: a rename (1) shares an address with its successor; a
    #     deprecation (2) does not, and the replacement (when given) is a
    #     DIFFERENT entity with different functionality.
    "address_names": [
        ("id", "INTEGER PRIMARY KEY"),                 # IS the kcdx_id; from address_names_seed.csv.id
        ("name", "TEXT"),
        # supersession pair (cosmetic rename; engine auto-follows, version-gated)
        ("superseded_by", "INTEGER"),                  # FK to address_names.id (direct successor)
        ("superseded_at_version", "INTEGER"),          # FK to game_versions.id; supersession applies >= this
        # deprecation pair (behavior changed; engine warns, does not redirect)
        ("is_deprecated", "INTEGER"),                  # 0/1
        ("deprecated_at_version", "INTEGER"),          # FK to game_versions.id; deprecation applies >= this
        ("deprecation_replacement", "INTEGER"),        # nullable FK to address_names.id; "consider switching to" advisory
        # prose (DEV only)
        ("notes", "TEXT"),                             # DEV-ONLY (entity-level prose)
    ],
    # address_versions: per-(entity, version-interval) resolve facts. kcdx_id
    # is NULLABLE -- set when the row is a curated entity (FK to address_names.id),
    # NULL when the row is a bulk-only DEV function (no curated name). Partial
    # UNIQUE (kcdx_id) WHERE kcdx_id IS NOT NULL AND valid_through IS NULL
    # enforces "at most one current form per CURATED entity." (Bulk rows are
    # currently 1:1 with their function but not enforced as such.)
    "address_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),                # NULLABLE FK to address_names.id (curated only)
        ("kind", "INTEGER"),                   # dict: function | callsite | vtable_base | etc.
        ("module_id", "INTEGER"),              # FK to modules.id
        ("rva", "INTEGER"),
        ("length", "INTEGER"),
        ("content_hash", "BLOB"),
        ("value", "INTEGER"),                  # slot int for vtable_index, offset for data_slot
        ("signature", "TEXT"),                 # verified ABI; for un-curated bulk this is the abi_walker floor
        ("observed_arg_slots", "INTEGER"),
        ("caller_reg_arg_count", "INTEGER"),
        ("caller_arg_agreement", "INTEGER"),   # dict
        ("offset", "INTEGER"),                 # callsite consumer offset
        ("vtable_slot", "INTEGER"),            # vtable_index slot integer (mirrors value for that kind)
        # Verification audit trail (added 2026-05-28). The `status` column was
        # REMOVED -- status is derived from
        #   (current_version, valid_from, last_verified_at_version) PLUS
        # entity-level supersession + deprecation flags on address_names.
        # See policy.md.
        ("last_verified_at_version", "INTEGER"), # nullable FK to game_versions.id; NULL = never verified
        ("verified_by", "TEXT"),                 # nullable person identifier
        ("verified_date", "TEXT"),               # nullable ISO YYYY-MM-DD
        ("evidence_kind", "INTEGER"),            # nullable dict (EVIDENCE_KIND_ENUM)
        ("auto_name", "TEXT"),                 # DEV-ONLY (FUN_<rva> for bulk discovery)
        ("decompile_quality", "INTEGER"),      # dict, DEV-ONLY
        ("valid_from", "INTEGER"),             # FK to game_versions.id (== valid_from_version)
        ("valid_through", "INTEGER"),          # NULL = current (open interval)
    ],
    "meta": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("schema_version", "INTEGER"),
        ("abi_confidence", "TEXT"),
    ],
    # DEV-only tables. Each row carries TWO FK columns to its owning function:
    #   address_version_id -- FK to address_versions.id; ALWAYS SET. The
    #     universal "which function row" pointer (works for both curated +
    #     bulk; what kcdx.find walks).
    #   kcdx_id            -- FK to address_names.id; NULLABLE, non-unique.
    #     Set only when the owning function is curated. Ergonomic shortcut for
    #     curated-subset joins; redundant with address_version_id otherwise.
    "statements": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("address_version_id", "INTEGER"),     # always set; -> address_versions.id
        ("kcdx_id", "INTEGER"),                # nullable, non-unique; -> address_names.id when curated
        ("idx", "INTEGER"),
        ("kind", "INTEGER"),                    # dict
        ("pseudo_text", "TEXT"),
        ("byte_range_start", "INTEGER"),
        ("byte_range_len", "INTEGER"),
        ("content_hash", "BLOB"),
        ("callee", "TEXT"),
        ("string_ref", "TEXT"),
    ],
    "referenced_vars": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("address_version_id", "INTEGER"),     # always set; -> address_versions.id
        ("kcdx_id", "INTEGER"),                # nullable, non-unique
        ("statement_idx", "INTEGER"),
        ("var_name", "TEXT"),
        ("storage_kind", "INTEGER"),            # dict
        ("storage_detail", "TEXT"),
        ("size_bytes", "INTEGER"),
        ("data_type", "INTEGER"),               # dict
    ],
    "call_edges": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("caller_address_version_id", "INTEGER"),   # always set
        ("callee_address_version_id", "INTEGER"),   # always set (call must resolve to land here)
        ("caller_kcdx_id", "INTEGER"),              # nullable, non-unique; curated caller only
        ("callee_kcdx_id", "INTEGER"),              # nullable, non-unique; curated callee only
        ("callsite_rva", "INTEGER"),
    ],
}

# Table sets per db.
DEV_TABLES = ["modules", "game_versions", "address_names", "address_versions",
              "meta", "statements", "referenced_vars", "call_edges"]
USER_TABLES = ["modules", "game_versions", "address_names", "address_versions",
               "meta"]

# The 9 kinds for address_versions.kind (covering every curated row type seen
# in the address seeds + the bulk function default).
ADDRESS_KINDS = ("function", "function_variadic", "function_no_sig", "callsite",
                 "vtable_index", "vtable_base", "data_slot", "string_anchor",
                 "instruction_anchor")

# Pairing trigger: NO LONGER NEEDED.
# Under the prior schema (entities + entity_versions + kcdx_overlay +
# kcdx_overlay_versions) a trigger had to mirror entity_versions inserts onto
# overlay_versions to keep two parallel version tables in sync across game-
# version updates. The flattened schema has ONLY address_versions -- one place
# to write per version. The maintainer-side update path inserts an
# address_versions row directly; no second table to keep paired. The trigger
# dropped here lived solely to bridge the two tables.


# ---------------------------------------------------------------------------
# Helpers (reused encoding policy from the prior cut).
# ---------------------------------------------------------------------------
def parse_int(v):
    if v is None or v == "":
        return None
    try:
        return int(v, 16) if v.startswith("0x") else int(v)
    except (ValueError, AttributeError):
        return None


def hash_blob(v):
    """64-hex TEXT -> 32-byte BLOB; '' -> None."""
    if isinstance(v, str) and len(v) == 64:
        try:
            return bytes.fromhex(v)
        except ValueError:
            return None
    return None


class Dicts:
    """Per-(table, col) value -> small INTEGER id, materialized at the end."""
    def __init__(self):
        self._d = {}   # (table, col) -> { value(str): int_id }

    def encode(self, table, col, value):
        if value is None or value == "":
            return None
        d = self._d.setdefault((table, col), {})
        return d.setdefault(value, len(d) + 1)   # 1-based ids

    def ensure(self, table, col, value):
        """Pre-register a value (so the trigger can look it up) and return id."""
        return self.encode(table, col, value)

    def materialize(self, con):
        n = 0
        for (t, c), d in self._d.items():
            con.execute(f'CREATE TABLE "_dict_{t}_{c}" (id INTEGER PRIMARY KEY, val TEXT)')
            con.executemany(f'INSERT INTO "_dict_{t}_{c}" VALUES (?,?)',
                            [(i, v) for v, i in d.items()])
            n += len(d)
        return n


# ---------------------------------------------------------------------------
# Dump readers.
# ---------------------------------------------------------------------------
def shard_paths(dump_dir, table):
    return sorted(glob.glob(os.path.join(dump_dir, table, f"{table}_*.csv")))


def iter_table(dump_dir, table):
    """Yield dict rows across all shards of a dump table."""
    shards = shard_paths(dump_dir, table)
    for shard in shards:
        with open(shard, newline="", encoding="utf-8", errors="replace") as f:
            rd = csv.DictReader(f)
            for row in rd:
                yield row


def _read_canonical_seed(path, kind):
    """Read a seed CSV with a canonical maintainer-supplied `id` column. Skips
    '#'-prefixed comment lines. Fails LOUD on:
      - any row whose `id` is empty or non-integer (no autoincrement; the id
        must be supplied by the maintainer)
      - any duplicate id within the file

    Returns list[dict] of rows. `kind` is "address" or "module" -- only used in
    error messages.
    """
    rows = []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rd = csv.DictReader(lines)
    seen_ids = {}
    for lineno, r in enumerate(rd, start=2):     # header is line 1
        rid_raw = (r.get("id") or "").strip()
        if not rid_raw:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: empty id (canonical ids are "
                f"maintainer-supplied; no autoincrement)")
        try:
            rid = int(rid_raw)
        except ValueError:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: id={rid_raw!r} is not an integer")
        if rid in seen_ids:
            raise RuntimeError(
                f"{kind}_seed.csv:{lineno}: duplicate id={rid} (first seen "
                f"at line {seen_ids[rid]})")
        seen_ids[rid] = lineno
        rows.append(r)
    return rows


def read_module_seed(path):
    """Read module_seed.csv. Schema: id, name, path. The id is the canonical
    modules.id; name + path are the maintainer-supplied module identity. Both
    name and path are required (empty = HARD ERROR)."""
    rows = _read_canonical_seed(path, "module")
    for r in rows:
        name = (r.get("name") or "").strip()
        modpath = (r.get("path") or "").strip()
        if not name:
            raise RuntimeError(
                f"module_seed.csv: row id={r['id']} has empty `name`")
        if not modpath:
            raise RuntimeError(
                f"module_seed.csv: row id={r['id']} has empty `path`")
    return rows


def read_address_names_seed(path):
    """Read address_names_seed.csv. Schema:
      id, name,
      superseded_by, superseded_at_version,
      is_deprecated, deprecated_at_version, deprecation_replacement,
      notes

    The id is the canonical address_names.id (== kcdx_id, the stable cross-
    version handle plugins reference). `name` is required (empty = HARD ERROR).
    Supersession + deprecation pair integrity is resolved later (after the
    versions seed is read; the post-loop pass in build_rows runs the cross-row
    validation).

    The legacy `source` column was DROPPED 2026-05-28 -- it carried no
    information ("verified" duplicating status, which is now derived from
    last_verified_at_version + the entity-level supersession/deprecation
    flags, not an authored column)."""
    rows = _read_canonical_seed(path, "address_names")
    for r in rows:
        name = (r.get("name") or "").strip()
        if not name:
            raise RuntimeError(
                f"address_names_seed.csv: row id={r['id']} has empty `name`")
    return rows


# Legal evidence_kind enum values. Order is the quality ranking (live-tier
# strongest; pattern-scan-only weakest). The importer fails loud on any other
# value when last_verified_at_version is set.
EVIDENCE_KIND_ENUM = (
    "live_production",
    "live_test_plugin",
    "maintainer_ghidra",
    "predecessor_sig",
    "pattern_scan",
)
_VERIFIED_DATE_RE = re.compile(r"^\d{4}-\d{2}-\d{2}$")


def read_address_versions_seed(path):
    """Read address_versions_seed.csv. Schema:
      kcdx_id, valid_from_version, module, rva, signature,
      last_verified_at_version, verified_by, verified_date, evidence_kind

    No `id` column -- row identity is the (kcdx_id, valid_from_version) tuple
    (a 1.5 row + a 1.6 row for the same entity is two rows, each with its own
    valid_from_version). `kcdx_id`, `valid_from_version`, `module` are REQUIRED
    (empty = HARD ERROR). `rva`/`signature` MAY be empty (vtable_index kind has
    no RVA; un-verified rows may lack a signature).

    Verification audit pair:
      - When last_verified_at_version is set, verified_by + verified_date +
        evidence_kind ALL three must be set. (Else "verified by what / when /
        how?" is unanswerable.)
      - When last_verified_at_version is NULL/empty, all three of verified_by
        / verified_date / evidence_kind MUST be empty.
      - last_verified_at_version >= valid_from_version (you can't verify a
        row for a version older than the version the row claims to start at).
      - verified_date format: YYYY-MM-DD.
      - evidence_kind: must be in EVIDENCE_KIND_ENUM when set.

    The `status` column was REMOVED 2026-05-28 -- status is derived at query
    time from (current_version, valid_from_version, last_verified_at_version)
    plus the entity-level supersession/deprecation flags on address_names. See
    policy.md for the derivation rule.

    Fails LOUD on:
      - missing required column
      - duplicate (kcdx_id, valid_from_version) tuple
      - audit-pair integrity violation
      - last_verified_at_version < valid_from_version
      - malformed verified_date
      - unknown evidence_kind

    Returns list[dict] in file order."""
    rows = []
    with open(path, newline="", encoding="utf-8", errors="replace") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rd = csv.DictReader(lines)
    seen = {}    # (kcdx_id:int, valid_from_version:str) -> lineno
    for lineno, r in enumerate(rd, start=2):
        kid_raw = (r.get("kcdx_id") or "").strip()
        vfv     = (r.get("valid_from_version") or "").strip()
        module  = (r.get("module") or "").strip()
        lvv     = (r.get("last_verified_at_version") or "").strip()
        vby     = (r.get("verified_by") or "").strip()
        vdate   = (r.get("verified_date") or "").strip()
        ekind   = (r.get("evidence_kind") or "").strip()

        if not kid_raw:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty kcdx_id")
        try:
            kid = int(kid_raw)
        except ValueError:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: kcdx_id={kid_raw!r} "
                f"is not an integer")
        if not vfv:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty valid_from_version "
                f"(kcdx_id={kid})")
        if not module:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: empty module "
                f"(kcdx_id={kid}, valid_from_version={vfv!r}); reference a "
                f"module_seed.csv row by id or name")
        key = (kid, vfv)
        if key in seen:
            raise RuntimeError(
                f"address_versions_seed.csv:{lineno}: duplicate "
                f"(kcdx_id={kid}, valid_from_version={vfv!r}) -- first at "
                f"line {seen[key]}")
        seen[key] = lineno

        # Audit-pair integrity: last_verified_at_version IS NULL <=> all three
        # audit columns NULL.
        audit_present_count = sum(1 for x in (vby, vdate, ekind) if x)
        if lvv:
            if audit_present_count != 3:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version={lvv!r} is set but the audit "
                    f"trio (verified_by, verified_date, evidence_kind) is "
                    f"incomplete -- got verified_by={vby!r}, "
                    f"verified_date={vdate!r}, evidence_kind={ekind!r}")
            # last_verified >= valid_from (string compare is fine for the
            # release_M_N.BUILD tag format; lexicographic order = real order).
            if lvv < vfv:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version={lvv!r} < "
                    f"valid_from_version={vfv!r} (you can't verify a row at a "
                    f"version older than the version the row starts at)")
            # Date format check.
            if not _VERIFIED_DATE_RE.match(vdate):
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"verified_date={vdate!r} must be YYYY-MM-DD")
            # Evidence kind enum check.
            if ekind not in EVIDENCE_KIND_ENUM:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"evidence_kind={ekind!r} is not in the enum "
                    f"{EVIDENCE_KIND_ENUM}")
        else:
            if audit_present_count != 0:
                raise RuntimeError(
                    f"address_versions_seed.csv:{lineno}: "
                    f"last_verified_at_version is empty but at least one of "
                    f"the audit trio is set -- got verified_by={vby!r}, "
                    f"verified_date={vdate!r}, evidence_kind={ekind!r}")

        rows.append(r)
    return rows


# ---------------------------------------------------------------------------
# On-disk game version detection (update mode).
# ---------------------------------------------------------------------------
# The shipped game config, as it appears in whdlversions.json versionId strings
# (the live game runs from Bin/Win64MasterMasterSteamPGO/). The DLL has no PE
# version resource; this JSON is the source of truth.
SHIPPED_CONFIG_TOKEN = "MasterMasterPGO"


def read_game_version(game_dir):
    """Parse <game_dir>/whdlversions.json -> (build_ordinal:int, tag:str).

    The JSON's Configurations[] carry PER-CONFIG versionId strings like
    'kcd2\\release_1_5\\PC\\MasterMasterPGO\\kcd2_release_1_5_PC_MasterMasterPGO_1164953_7490'.
    We select the MasterMasterPGO config (the shipped build), extract its branch
    (release_1_5) and build number (1164953). Returns (1164953, '1.5.1164953').

    Raises a clear error if the file/config/build is absent (do NOT guess a
    version -- a wrong version would corrupt the append).
    """
    import json
    import re

    path = os.path.join(game_dir, "whdlversions.json")
    if not os.path.isfile(path):
        raise RuntimeError("whdlversions.json not found in %s (the game version "
                           "source; the DLL has no version resource)" % game_dir)
    with open(path, encoding="utf-8") as f:
        data = json.load(f)

    configs = data.get("Configurations") or []
    chosen = None
    for c in configs:
        vid = (((c.get("SelectedVersion") or {}).get("versionId")) or "")
        if SHIPPED_CONFIG_TOKEN in vid:
            chosen = vid
            break
    if chosen is None:
        raise RuntimeError("no %s configuration in whdlversions.json (cannot "
                           "determine the shipped game build)" % SHIPPED_CONFIG_TOKEN)

    # versionId tail: ..._<MasterMasterPGO>_<build>_<sub>. Pull branch + build.
    branch = None
    pb = data.get("Preset") or {}
    branch = ((pb.get("Branch") or {}).get("Name")) or None  # e.g. 'release_1_5'
    m = re.search(SHIPPED_CONFIG_TOKEN + r"_(\d+)_\d+\b", chosen)
    if not m:
        m = re.search(SHIPPED_CONFIG_TOKEN + r"_(\d+)", chosen)
    if not m:
        raise RuntimeError("could not parse a build number from versionId %r" % chosen)
    build = int(m.group(1))

    # tag = dotted branch version + build, e.g. release_1_5 -> '1.5' -> '1.5.1164953'.
    tag_prefix = None
    if branch:
        bm = re.search(r"(\d+)_(\d+)", branch)
        if bm:
            tag_prefix = "%s.%s" % (bm.group(1), bm.group(2))
    tag = ("%s.%d" % (tag_prefix, build)) if tag_prefix else str(build)
    return build, tag


def db_latest_ordinal(db_path):
    """Return the max game_versions.ordinal in an existing DB, or None if the DB
    or table is absent."""
    if not os.path.isfile(db_path):
        return None
    con = sqlite3.connect(db_path)
    try:
        has = con.execute("SELECT name FROM sqlite_master WHERE type='table' "
                          "AND name='game_versions'").fetchone()
        if not has:
            return None
        row = con.execute("SELECT MAX(ordinal) FROM game_versions").fetchone()
        return row[0] if row else None
    finally:
        con.close()


# ---------------------------------------------------------------------------
# Overlay kind inference (heuristic; covers the 9 values, never crashes).
# ---------------------------------------------------------------------------
def infer_kind(seed_row):
    rva = (seed_row.get("rva") or "").strip()
    sig = (seed_row.get("signature") or "").strip()
    name = (seed_row.get("name") or "").lower()
    notes = (seed_row.get("notes") or "").lower()

    if not rva:
        return "vtable_index"   # ids 3000-3005, empty rva
    if "callsite" in name or "callsite" in notes or "not a function entry" in notes:
        return "callsite"
    if "vtable base" in notes or "vtable base address" in notes or "concrete-class" in notes:
        return "vtable_base"
    if ".data pointer" in notes or "static .data" in notes or "data slot" in notes \
            or "static pointer slot" in notes:
        return "data_slot"
    if ".rdata string" in notes or "string literal" in notes:
        return "string_anchor"
    if "instruction" in notes or "mov rcx" in notes:
        return "instruction_anchor"
    if "..." in name or "variadic" in name or "variadic" in notes:
        return "function_variadic"
    if sig:
        return "function"
    return "function_no_sig"   # real entry, empty signature


def kind_offset_and_slot(kind, notes):
    """offset stays NULL for the unblock. vtable_slot: parse a trailing slot int
    from vtable_index notes only when trivially present, else NULL."""
    offset = None
    vtable_slot = None
    if kind == "vtable_index" and notes:
        # notes like "...vtable index = 13 (0-indexed)..." or "slot 4 (0-indexed)".
        import re
        m = re.search(r"index\s*=\s*(\d+)", notes)
        if not m:
            m = re.search(r"slot\s+(\d+)\s*\(0-indexed\)", notes)
        if m:
            vtable_slot = int(m.group(1))
    return offset, vtable_slot


# ---------------------------------------------------------------------------
# The full transform: dump + seed -> in-memory row sets for every table.
# Returns a dict table -> list[dict-of-column->value], plus the Dicts encoder.
#
# Flattened schema (2026-05-28): no entities / entity_versions split. Every
# function (curated + bulk) gets ONE address_versions row keyed by its kcdx_id;
# every curated NAME (from address_names_seed.csv) gets ONE address_names row pointing at the
# same kcdx_id. The USER projection filters out address_versions rows whose
# kcdx_id is not referenced by any address_names row -- so the bulk 321K stay
# in DEV only, the curated ~140 ship to USER.
# ---------------------------------------------------------------------------
def build_rows(dump_dir, dicts):
    rows = {t: [] for t in DEV_TABLES}

    # --- modules from module_seed.csv ---
    # Read the maintainer-supplied module registry. Each row's id is canonical
    # (fail-loud on null/duplicate). Build two lookup maps for the address_seed
    # `module` column resolver below: by-int and by-name.
    module_rows = read_module_seed(MODULE_SEED_CSV)
    modules_by_id   = {}
    modules_by_name = {}
    for m in module_rows:
        mid = int(m["id"])
        name = m["name"].strip()
        modpath = m["path"].strip()
        rows["modules"].append({"id": mid, "name": name, "path": modpath})
        modules_by_id[mid]     = mid
        modules_by_name[name]  = mid
    print(f"  module_seed.csv: {len(module_rows)} module(s)", flush=True)

    # The bulk-functions extractor only enumerates one module today. Hardcode
    # which module_seed row that maps to (this is a baseline constant for the
    # 1.5.1164953 dump; when the extractor learns to dump multiple modules, it
    # will emit a `module` column per function and this lookup goes away).
    BULK_MODULE_NAME = "WHGame.dll"
    bulk_module_id = modules_by_name.get(BULK_MODULE_NAME)
    if bulk_module_id is None:
        raise RuntimeError(
            f"module_seed.csv has no row for the bulk-dump module "
            f"({BULK_MODULE_NAME!r}); add it before importing")

    rows["game_versions"].append({"id": GAME_VERSION_ID, "tag": GAME_VERSION_TAG,
                                  "ordinal": GAME_VERSION_ORDINAL, "released": None})
    rows["meta"].append({"id": 1, "schema_version": SCHEMA_VERSION,
                         "abi_confidence": ABI_CONFIDENCE})

    # --- 1. read functions/, sort by rva, assign each function a stable
    #         address_versions.id (av_id) 1..N. av_id is the universal "which
    #         function row" handle -- statements/edges/etc. point at it. NOT
    #         the same as kcdx_id (which is curated-only, autoincrement on
    #         address_names, set later).
    print("  reading functions/ ...", flush=True)
    functions = []
    for r in iter_table(dump_dir, "functions"):
        functions.append(r)
    # sort by integer rva ascending (empty/None rva functions sort last)
    def fn_rva(r):
        v = parse_int(r.get("rva", ""))
        return (v is None, v if v is not None else 0)
    functions.sort(key=fn_rva)
    rva_to_av_id = {}            # function rva -> address_versions.id
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        if rv is not None:
            rva_to_av_id[rv] = i
    n_functions = len(functions)
    print(f"  functions: {n_functions} rows -> address_versions.id 1..{n_functions}",
          flush=True)

    # --- merge signatures/ + caller_reg_args/ onto functions BY RVA ---
    print("  reading signatures/ + caller_reg_args/ ...", flush=True)
    sig_by_rva = {}
    for r in iter_table(dump_dir, "signatures"):
        rv = parse_int(r.get("rva", ""))
        if rv is not None:
            sig_by_rva[rv] = r
    cra_by_rva = {}
    for r in iter_table(dump_dir, "caller_reg_args"):
        rv = parse_int(r.get("rva", ""))
        if rv is not None:
            cra_by_rva[rv] = r

    # --- 2. address_versions for each bulk function. kcdx_id is NULL (these
    #        are uncurated; only the seed pass below promotes some to curated
    #        and sets their kcdx_id).
    versions_by_av_id = {}   # av_id -> row dict (so seed pass can amend)
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        sig = sig_by_rva.get(rv)
        cra = cra_by_rva.get(rv)
        versions_by_av_id[i] = {
            "id": i,                       # explicit; matches rva_to_av_id
            "kcdx_id": None,               # NULL = uncurated bulk; seed pass sets when curated
            "kind": dicts.encode("address_versions", "kind", "function"),
            "module_id": bulk_module_id,
            "rva": rv,
            "length": parse_int(r.get("length", "")),
            "content_hash": hash_blob(r.get("content_hash", "")),
            "value": None,
            "signature": (sig.get("signature") if sig else None) or None,
            "observed_arg_slots": parse_int(sig.get("observed_arg_slots", "")) if sig else None,
            "caller_reg_arg_count": parse_int(cra.get("caller_reg_arg_count", "")) if cra else None,
            "caller_arg_agreement": dicts.encode("address_versions", "caller_arg_agreement",
                                                 cra.get("agreement", "")) if cra else None,
            "offset": None,
            "vtable_slot": None,
            # Bulk rows are uncurated -- no maintainer ever signed off on them.
            # All four audit columns NULL; status is derived (always "unverified"
            # at any current_version for a NULL last_verified_at_version).
            "last_verified_at_version": None,
            "verified_by": None,
            "verified_date": None,
            "evidence_kind": None,
            "auto_name": (r.get("auto_name") or None),
            "decompile_quality": dicts.encode("address_versions", "decompile_quality",
                                              r.get("decompile_quality", "")),
            "valid_from": GAME_VERSION_ID,
            "valid_through": None,
        }

    # --- 3a. read address_names_seed.csv -> address_names table. One row per
    #         curated entity, ever. address_names.id == address_names_seed.id ==
    #         kcdx_id (the stable cross-version handle).
    names_seed = read_address_names_seed(ADDRESS_NAMES_SEED_CSV)
    print(f"  address_names_seed.csv: {len(names_seed)} curated entities",
          flush=True)
    valid_kcdx_ids = set()
    for ns in names_seed:
        nid = int(ns["id"])
        valid_kcdx_ids.add(nid)
        sb_name = (ns.get("superseded_by") or "").strip() or None
        sb_ver  = (ns.get("superseded_at_version") or "").strip() or None
        dep     = (ns.get("is_deprecated") or "").strip()
        dep_ver = (ns.get("deprecated_at_version") or "").strip() or None
        dep_repl= (ns.get("deprecation_replacement") or "").strip() or None
        rows["address_names"].append({
            "id": nid,                          # canonical; IS the kcdx_id
            "name": ns["name"].strip(),
            # Pre-resolution: these hold STRINGS (names + tags) from the seed.
            # The post-loop pass below resolves them to ids once the name -> id
            # map is complete.
            "superseded_by":          sb_name,
            "superseded_at_version":  sb_ver,
            "is_deprecated":          1 if dep in ("1", "true", "yes") else 0,
            "deprecated_at_version":  dep_ver,
            "deprecation_replacement": dep_repl,
            "notes": (ns.get("notes") or None),
        })

    # --- 3b. read address_versions_seed.csv -> curated address_versions rows.
    #         For each row, resolve (kcdx_id, valid_from_version, module);
    #         either amend an existing bulk address_versions row (when the
    #         seed's RVA matched a bulk-dump function for the current import
    #         version) or mint a fresh address_versions row. Every kcdx_id must
    #         resolve to an address_names_seed row.
    versions_seed = read_address_versions_seed(ADDRESS_VERSIONS_SEED_CSV)
    print(f"  address_versions_seed.csv: {len(versions_seed)} curated facts",
          flush=True)

    def _resolve_module(raw, where):
        """Per-row module resolution. Tries int (id) first; falls back to name.
        `where` is a short context string for the error message."""
        try:
            mid = int(raw)
            if mid in modules_by_id:
                return mid
            raise RuntimeError(
                f"{where}: module={raw!r} parses as int but no module_seed.csv "
                f"row has id={mid}")
        except ValueError:
            pass
        mid = modules_by_name.get(raw)
        if mid is None:
            raise RuntimeError(
                f"{where}: module={raw!r} matches no module_seed.csv row by name")
        return mid

    def _resolve_version_tag(tag, where):
        """Resolve a game_versions.tag string to game_versions.id. Fails LOUD
        on unknown -- the baseline import only knows about GAME_VERSION_TAG."""
        if not tag:
            return None
        for gv in rows["game_versions"]:
            if gv["tag"] == tag:
                return gv["id"]
        raise RuntimeError(
            f"{where}: version tag {tag!r} matches no game_versions row "
            f"(this baseline import only knows {GAME_VERSION_TAG!r}; future "
            f"versions need their own game_versions row in the meta seed first)")

    # Build (kcdx_id, name) lookup for kind inference: address_names carries the
    # name; the versions-seed kind heuristic still wants to see it.
    id_to_name = {int(ns["id"]): ns["name"].strip() for ns in names_seed}
    notes_by_kid = {int(ns["id"]): (ns.get("notes") or "") for ns in names_seed}

    next_av_id = n_functions + 1
    n_seed_mapped = 0
    n_seed_minted_addr = 0
    n_seed_minted_noaddr = 0

    for vs in versions_seed:
        kid = int(vs["kcdx_id"])
        vfv_tag = vs["valid_from_version"].strip()
        if kid not in valid_kcdx_ids:
            raise RuntimeError(
                f"address_versions_seed.csv: kcdx_id={kid} "
                f"(valid_from_version={vfv_tag!r}) has no row in "
                f"address_names_seed.csv (every versions row must reference "
                f"an existing entity)")
        # Only rows for the baseline import version are materialized today.
        # Future-version rows live in the seed but get materialized when the
        # importer is re-run with that version's dump.
        if vfv_tag != GAME_VERSION_TAG:
            continue

        where = (f"address_versions_seed.csv (kcdx_id={kid}, "
                 f"valid_from_version={vfv_tag!r})")
        module_id = _resolve_module(vs["module"].strip(), where)
        nm = id_to_name.get(kid, "")
        notes_for_kind = notes_by_kid.get(kid, "")

        srva = (vs.get("rva") or "").strip()
        sig  = (vs.get("signature") or "").strip()
        lvv  = (vs.get("last_verified_at_version") or "").strip()
        vby  = (vs.get("verified_by") or "").strip()
        vdt  = (vs.get("verified_date") or "").strip()
        ekn  = (vs.get("evidence_kind") or "").strip()

        # Resolve the verification version anchor (read_address_versions_seed
        # already enforced the audit-trio integrity rules; here we just FK it).
        lvv_id = _resolve_version_tag(lvv, where) if lvv else None
        ekn_id = dicts.encode("address_versions", "evidence_kind", ekn) if ekn else None

        kind_cue = {"rva": srva, "signature": sig, "name": nm, "notes": notes_for_kind}

        if srva:
            rv = parse_int(srva)
            av_id = rva_to_av_id.get(rv)
            if av_id is None:
                av_id = next_av_id
                next_av_id += 1
                versions_by_av_id[av_id] = {
                    "id": av_id,
                    "kcdx_id": kid,
                    "kind": None,
                    "module_id": module_id,
                    "rva": rv,
                    "length": None,
                    "content_hash": None,
                    "value": None,
                    "signature": sig or None,
                    "observed_arg_slots": None,
                    "caller_reg_arg_count": None,
                    "caller_arg_agreement": None,
                    "offset": None,
                    "vtable_slot": None,
                    "last_verified_at_version": lvv_id,
                    "verified_by": vby or None,
                    "verified_date": vdt or None,
                    "evidence_kind": ekn_id,
                    "auto_name": None,
                    "decompile_quality": None,
                    "valid_from": GAME_VERSION_ID,
                    "valid_through": None,
                }
                n_seed_minted_addr += 1
            else:
                n_seed_mapped += 1
        else:
            av_id = next_av_id
            next_av_id += 1
            versions_by_av_id[av_id] = {
                "id": av_id,
                "kcdx_id": kid,
                "kind": None,
                "module_id": module_id,
                "rva": None,
                "length": None,
                "content_hash": None,
                "value": None,
                "signature": sig or None,
                "observed_arg_slots": None,
                "caller_reg_arg_count": None,
                "caller_arg_agreement": None,
                "offset": None,
                "vtable_slot": None,
                "last_verified_at_version": lvv_id,
                "verified_by": vby or None,
                "verified_date": vdt or None,
                "evidence_kind": ekn_id,
                "auto_name": None,
                "decompile_quality": None,
                "valid_from": GAME_VERSION_ID,
                "valid_through": None,
            }
            n_seed_minted_noaddr += 1

        # Promote to curated: set kcdx_id (already), kind, audit columns, off/vslot.
        kind = infer_kind(kind_cue)
        offset, vslot = kind_offset_and_slot(kind, notes_for_kind.lower())
        v = versions_by_av_id[av_id]
        v["kcdx_id"] = kid
        v["kind"] = dicts.encode("address_versions", "kind", kind)
        v["last_verified_at_version"] = lvv_id
        v["verified_by"] = vby or None
        v["verified_date"] = vdt or None
        v["evidence_kind"] = ekn_id
        if sig:
            v["signature"] = sig
        if offset is not None:
            v["offset"] = offset
        if vslot is not None:
            v["vtable_slot"] = vslot
            v["value"] = vslot

    # Every kcdx_id in address_names_seed.csv must have a matching row in
    # address_versions_seed.csv for the baseline import version -- else the
    # entity has a name but no resolve facts.
    covered_kids = {v["kcdx_id"] for v in versions_by_av_id.values()
                    if v["kcdx_id"] is not None}
    uncovered = valid_kcdx_ids - covered_kids
    if uncovered:
        sample = sorted(uncovered)[:5]
        raise RuntimeError(
            f"address_names_seed.csv has {len(uncovered)} kcdx_id(s) with no "
            f"address_versions_seed.csv row for "
            f"valid_from_version={GAME_VERSION_TAG!r} "
            f"(first 5: {sample}); every named entity needs at least one "
            f"resolve fact for the baseline version")

    # rva -> kcdx_id (curated only) for the DEV-only tables' kcdx_id column.
    # rva -> av_id (universal) for the DEV-only tables' address_version_id column.
    rva_to_kcdx_id = {v["rva"]: v["kcdx_id"]
                      for v in versions_by_av_id.values()
                      if v["rva"] is not None and v["kcdx_id"] is not None}

    # --- 4. RESOLVE supersession / deprecation references + integrity checks.
    #
    # address_names_seed.csv records the DIRECT supersession edge by NAME ("at this version,
    # b supersedes a") + the version it became active. The engine walks the
    # chain at query time, applying the version filter at each hop -- no
    # compaction. deprecation_replacement is an advisory pointer (engine surfaces
    # it in the warning, does NOT auto-follow).
    #
    # All validation runs here, fail-loud: unknown names, unknown version tags,
    # paired-fields integrity (XOR), cycle in superseded_by graph.
    name_to_id = {r["name"]: r["id"] for r in rows["address_names"] if r["name"]}
    tag_to_id  = {gv["tag"]: gv["id"] for gv in rows["game_versions"]}

    def _resolve_name(rid, rname, col, val):
        nid = name_to_id.get(val)
        if nid is None:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col}={val!r} but no "
                f"seed row has that name")
        if nid == rid and col == "superseded_by":
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col} points at itself")
        return nid

    def _resolve_tag(rid, rname, col, val):
        gvid = tag_to_id.get(val)
        if gvid is None:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: {col}={val!r} but no "
                f"game_versions row has that tag")
        return gvid

    for r in rows["address_names"]:
        rid, rname = r["id"], r["name"]

        # Supersession pair integrity: both-or-neither.
        sb, sbv = r["superseded_by"], r["superseded_at_version"]
        if (sb is None) != (sbv is None):
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: superseded_by and "
                f"superseded_at_version must be set together "
                f"(got superseded_by={sb!r}, superseded_at_version={sbv!r})")
        if sb is not None:
            r["superseded_by"]         = _resolve_name(rid, rname, "superseded_by", sb)
            r["superseded_at_version"] = _resolve_tag(rid, rname, "superseded_at_version", sbv)

        # Deprecation pair integrity: is_deprecated=1 <=> deprecated_at_version set.
        dep, depv = r["is_deprecated"], r["deprecated_at_version"]
        if bool(dep) != (depv is not None):
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: is_deprecated and "
                f"deprecated_at_version must be set together "
                f"(got is_deprecated={dep}, deprecated_at_version={depv!r})")
        if depv is not None:
            r["deprecated_at_version"] = _resolve_tag(rid, rname, "deprecated_at_version", depv)

        # deprecation_replacement only valid when is_deprecated=1.
        dr = r["deprecation_replacement"]
        if dr is not None and not dep:
            raise RuntimeError(
                f"address_names_seed.csv row id={rid} name={rname!r}: deprecation_replacement="
                f"{dr!r} requires is_deprecated=1")
        if dr is not None:
            r["deprecation_replacement"] = _resolve_name(rid, rname, "deprecation_replacement", dr)

    # Cycle detection on the supersession graph -- a cycle is wrong regardless
    # of which versions gate which edges (the version-ignorant graph reachable
    # via superseded_by must be acyclic). Walk each chain to a terminal; abort
    # if we revisit any node.
    n_superseded = sum(1 for r in rows["address_names"] if r["superseded_by"] is not None)
    if n_superseded:
        print(f"  supersession: {n_superseded} edge(s); validating acyclicity...",
              flush=True)
        direct = {r["id"]: r["superseded_by"] for r in rows["address_names"]}
        id_to_name = {r["id"]: r["name"] for r in rows["address_names"]}
        for r in rows["address_names"]:
            seen = [r["id"]]
            cur = direct.get(r["id"])
            while cur is not None:
                if cur in seen:
                    seen.append(cur)
                    chain = " -> ".join(id_to_name.get(i, f"#{i}") for i in seen)
                    raise RuntimeError(f"address_names_seed.csv supersession cycle: {chain}")
                seen.append(cur)
                cur = direct.get(cur)

    print(f"  address_names: {len(rows['address_names'])} rows "
          f"(seed: bulk-matched={n_seed_mapped}, "
          f"minted-with-rva={n_seed_minted_addr}, minted-no-rva={n_seed_minted_noaddr})",
          flush=True)

    # Emit the address_versions rows in id order.
    for av_id in sorted(versions_by_av_id.keys()):
        rows["address_versions"].append(versions_by_av_id[av_id])
    n_addr_versions = len(rows["address_versions"])
    n_curated_kcdx_ids = len(rows["address_names"])
    print(f"  address_versions: {n_addr_versions} rows "
          f"(curated: {n_curated_kcdx_ids}, "
          f"bulk uncurated: {n_addr_versions - n_curated_kcdx_ids})", flush=True)

    # --- 8. statements (DEV) ---
    # Each statement points at its owning function by address_version_id
    # (always set; universal handle to the function row) + kcdx_id (nullable;
    # set only when the function is curated).
    print("  reading statements/ ...", flush=True)
    n_st = 0
    for r in iter_table(dump_dir, "statements"):
        fn_rva = parse_int(r.get("function_rva", ""))
        av_id = rva_to_av_id.get(fn_rva)
        if av_id is None:
            continue   # statement of a function not in the dump (shouldn't happen)
        callee = r.get("callee") or ""
        callee_rva = parse_int(r.get("callee_rva", ""))
        # NULL the callee when it is the redundant auto-name of callee_rva.
        if callee_rva is not None and callee == ("FUN_%x" % callee_rva):
            callee = None
        elif callee_rva is not None and callee == ("FUN_%08x" % callee_rva):
            callee = None
        else:
            callee = callee or None
        rows["statements"].append({
            "address_version_id": av_id,
            "kcdx_id": rva_to_kcdx_id.get(fn_rva),   # NULL for uncurated
            "idx": parse_int(r.get("idx", "")),
            "kind": dicts.encode("statements", "kind", r.get("kind", "")),
            "pseudo_text": (r.get("pseudo_text") or None),
            "byte_range_start": parse_int(r.get("byte_range_start", "")),
            "byte_range_len": parse_int(r.get("byte_range_len", "")),
            "content_hash": hash_blob(r.get("content_hash", "")),
            "callee": callee,
            "string_ref": (r.get("string_ref") or None),
        })
        n_st += 1
    print(f"  statements: {n_st} rows", flush=True)

    # --- 9. referenced_vars (DEV) ---
    print("  reading referenced_vars/ ...", flush=True)
    n_rv = 0
    for r in iter_table(dump_dir, "referenced_vars"):
        fn_rva = parse_int(r.get("function_rva", ""))
        av_id = rva_to_av_id.get(fn_rva)
        if av_id is None:
            continue
        rows["referenced_vars"].append({
            "address_version_id": av_id,
            "kcdx_id": rva_to_kcdx_id.get(fn_rva),   # NULL for uncurated
            "statement_idx": parse_int(r.get("statement_idx", "")),
            "var_name": (r.get("var_name") or None),
            "storage_kind": dicts.encode("referenced_vars", "storage_kind", r.get("storage_kind", "")),
            "storage_detail": (r.get("storage_detail") or None),
            "size_bytes": parse_int(r.get("size_bytes", "")),
            "data_type": dicts.encode("referenced_vars", "data_type", r.get("data_type", "")),
        })
        n_rv += 1
    print(f"  referenced_vars: {n_rv} rows", flush=True)

    # --- 10. call_edges (DEV) ---
    # Each edge points at caller + callee functions via address_version_id
    # (always set; the call must resolve to land here) + nullable kcdx_id for
    # each side when curated.
    print("  reading call_edges/ ...", flush=True)
    n_ce = 0
    n_ce_skip = 0
    for r in iter_table(dump_dir, "call_edges"):
        caller_rva = parse_int(r.get("caller_rva", ""))
        callee_rva = parse_int(r.get("callee_rva", ""))
        caller_av = rva_to_av_id.get(caller_rva)
        callee_av = rva_to_av_id.get(callee_rva)
        # Both endpoints must land in the dump's function set (skip
        # empty/indirect/external).
        if caller_av is None or callee_av is None:
            n_ce_skip += 1
            continue
        rows["call_edges"].append({
            "caller_address_version_id": caller_av,
            "callee_address_version_id": callee_av,
            "caller_kcdx_id": rva_to_kcdx_id.get(caller_rva),   # NULL for uncurated
            "callee_kcdx_id": rva_to_kcdx_id.get(callee_rva),   # NULL for uncurated
            "callsite_rva": parse_int(r.get("callsite_rva", "")),
        })
        n_ce += 1
    print(f"  call_edges: {n_ce} rows ({n_ce_skip} skipped: unresolved caller/callee)",
          flush=True)

    counts = {
        "functions": n_functions,
        "seed_minted_no_rva": n_seed_minted_noaddr,
        "seed_minted_with_rva": n_seed_minted_addr,
        "address_names": len(rows["address_names"]),
        "address_versions": len(rows["address_versions"]),
        "curated_kcdx_ids": n_curated_kcdx_ids,
    }
    return rows, counts


# ---------------------------------------------------------------------------
# Write one db (USER or DEV) from the shared row sets.
#
# USER (user_projection=True) is now the CURATED-ONLY production DB per the
# streamlined three-track model (parallel-ghidra-research.md §11.8): kcdx tracks
# only the curated set across versions; the bulk 321K functions live ONLY in
# the DEV discovery DB (Track 3, on-demand author download for kcdx.find).
# A Track-2 author hooking an uncurated function declares it themselves via
# kcdx.declare(module, name, versions) -- the engine does NOT need bulk rows.
#
# USER thus FILTERS ROWS too, not just columns: address_versions ships only for
# kcdx_ids referenced by at least one address_names row (the curated set);
# address_names is always all-rows (it's the curated registry). Bulk rows
# (uncurated functions) live only in DEV.
# ---------------------------------------------------------------------------
def write_db(db_path, rows, dicts, tables, user_projection, curated_kcdx_ids=None):
    if os.path.exists(db_path):
        os.remove(db_path)
    con = sqlite3.connect(db_path)
    con.executescript("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA page_size=4096;")

    # Materialize dict lookup tables FIRST (any partial-unique index references
    # them via the dict id).
    dict_entries = dicts.materialize(con)

    # USER row-filter: address_versions narrows to rows with a curated kcdx_id
    # (kcdx_id IS NOT NULL = curated). Bulk uncurated rows (kcdx_id NULL) are
    # DEV-only by construction. DEV writes all rows.
    def filter_rows(t, rs):
        if not user_projection:
            return rs
        if t == "address_versions":
            return [r for r in rs if r["kcdx_id"] is not None]
        return rs   # modules, game_versions, address_names, meta: no row filter

    for t in tables:
        cols = SCHEMA[t]
        if user_projection:
            allowed = USER_COLUMNS[t]
            cols = [(c, ty) for (c, ty) in cols if c in allowed]
        coldefs = ",".join(f'"{c}" {ty}' for c, ty in cols)
        con.execute(f'CREATE TABLE "{t}" ({coldefs})')

        colnames = [c for c, _ in cols]
        placeholders = ",".join("?" * len(colnames))
        ins = f'INSERT INTO "{t}" ({",".join(colnames)}) VALUES ({placeholders})'

        batch = []
        n = 0
        for row in filter_rows(t, rows[t]):
            batch.append([row.get(c) for c in colnames])
            n += 1
            if len(batch) >= 20000:
                con.executemany(ins, batch)
                batch = []
        if batch:
            con.executemany(ins, batch)
        print(f"    {t}: {n} rows", flush=True)

    # Indexes for the engine's lookup paths.
    con.execute('CREATE INDEX ix_av_kcdx ON address_versions(kcdx_id)')
    con.execute('CREATE INDEX ix_av_rva  ON address_versions(rva)')
    # Partial-unique: at most one OPEN interval per CURATED entity. kcdx_id is
    # NULL for bulk uncurated rows -- skip those (bulk has its own 1:1 av_id).
    con.execute('CREATE UNIQUE INDEX ix_av_open_unique ON address_versions(kcdx_id) '
                'WHERE kcdx_id IS NOT NULL AND valid_through IS NULL')
    # address_names.id IS the kcdx_id (PK already indexed); only need a name index.
    con.execute('CREATE INDEX ix_an_name ON address_names(name)')
    if "statements" in tables:
        # Index by av_id (the universal handle kcdx.find walks) + idx for the
        # statement-ordering query. kcdx_id is nullable; index it separately.
        con.execute('CREATE INDEX ix_st_av ON statements(address_version_id, idx)')
        con.execute('CREATE INDEX ix_st_kcdx ON statements(kcdx_id)')
    if "referenced_vars" in tables:
        con.execute('CREATE INDEX ix_rv_av ON referenced_vars(address_version_id)')
        con.execute('CREATE INDEX ix_rv_kcdx ON referenced_vars(kcdx_id)')
    if "call_edges" in tables:
        con.execute('CREATE INDEX ix_ce_caller_av ON call_edges(caller_address_version_id)')
        con.execute('CREATE INDEX ix_ce_callee_av ON call_edges(callee_address_version_id)')
        con.execute('CREATE INDEX ix_ce_caller_kcdx ON call_edges(caller_kcdx_id)')
        con.execute('CREATE INDEX ix_ce_callee_kcdx ON call_edges(callee_kcdx_id)')

    con.commit()
    con.execute("VACUUM")
    con.close()
    return dict_entries


def run_rebuild(dump_dir, out_dir):
    """REBUILD mode: from-scratch baseline build of both DBs from a dump dir."""
    os.makedirs(out_dir, exist_ok=True)
    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    bar = "=" * 70
    print(bar)
    print(f"[import_to_sqlite] mode: REBUILD (from-scratch baseline)")
    print(f"[import_to_sqlite] dump: {dump_dir}")
    print(f"[import_to_sqlite] module           seed: {MODULE_SEED_CSV}")
    print(f"[import_to_sqlite] address names    seed: {ADDRESS_NAMES_SEED_CSV}")
    print(f"[import_to_sqlite] address versions seed: {ADDRESS_VERSIONS_SEED_CSV}")
    print(bar)

    # Build the row sets ONCE (shared by both dbs). The dict encoder is shared.
    dicts = Dicts()
    t0 = time.time()
    print("\n== TRANSFORM (dump + seed -> schema rows)")
    rows, counts = build_rows(dump_dir, dicts)
    print(f"  transform done in {time.time()-t0:.0f}s")
    print(f"  address_names={counts['address_names']} "
          f"address_versions={counts['address_versions']} "
          f"curated_kcdx_ids={counts['curated_kcdx_ids']}")

    # USER ships address_versions rows whose kcdx_id IS NOT NULL (the curated
    # subset); the filter is now structural to the row (kcdx_id NULL <=> bulk).
    print(f"  curated kcdx_ids: {counts['curated_kcdx_ids']} (USER will ship only these)")

    print(f"\n== DEV DB (bulk discovery superset) -> {dev_db}")
    t0 = time.time()
    dd = write_db(dev_db, rows, dicts, DEV_TABLES, user_projection=False)
    dsz = os.path.getsize(dev_db)
    print(f"  built in {time.time()-t0:.0f}s; size {dsz/1e6:.1f} MB; dict entries {dd}")

    print(f"\n== USER DB (production, curated-only) -> {user_db}")
    t0 = time.time()
    ud = write_db(user_db, rows, dicts, USER_TABLES, user_projection=True)
    usz = os.path.getsize(user_db)
    print(f"  built in {time.time()-t0:.0f}s; size {usz/1e6:.1f} MB; dict entries {ud}")

    print(bar)
    print("SUMMARY")
    print(f"  USER reference.sqlite     : {usz/1e6:8.1f} MB  (curated-only, {counts['curated_kcdx_ids']} kcdx_ids)")
    print(f"  DEV  reference-dev.sqlite : {dsz/1e6:8.1f} MB  (bulk superset, {counts['address_versions']} rows)")
    print(f"  functions={counts['functions']} "
          f"seed_minted_with_rva={counts['seed_minted_with_rva']} "
          f"seed_minted_no_rva={counts['seed_minted_no_rva']}")
    print(bar)


def run_update(out_dir, game_dir):
    """UPDATE mode (default): detect whether the on-disk game is newer than the
    DB and, if so, run the version-update append. The append needs the
    cross-version matcher (separate, not-yet-built); until then this resolves the
    comparison and reports the decision without mutating the DB."""
    bar = "=" * 70
    print(bar)
    print(f"[import_to_sqlite] mode: UPDATE (default; per-version incremental)")
    print(bar)

    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    # 1. most-recent version in the DB.
    db_ord = db_latest_ordinal(dev_db)
    if db_ord is None:
        db_ord = db_latest_ordinal(user_db)
    if db_ord is None:
        print("  no existing DB with a game_versions row in %s." % out_dir)
        print("  -> nothing to update. Run with --rebuild <dump_dir> to build the "
              "baseline first.")
        return
    print(f"  DB latest version ordinal: {db_ord}")

    # 2. on-disk game version.
    build, tag = read_game_version(game_dir)
    print(f"  game on disk: ordinal={build} tag={tag}  (from whdlversions.json, "
          f"{SHIPPED_CONFIG_TOKEN})")

    # 3. compare.
    if build <= db_ord:
        print(f"  -> DB is already current (on-disk {build} <= DB {db_ord}). "
              f"Nothing to do.")
        return
    print(f"  -> NEWER game version detected (on-disk {build} > DB {db_ord}).")
    print("  -> The version-update APPEND requires the cross-version matcher "
          "(re-identifying each entity across the change to extend or split its")
    print("     interval). That matcher is separate, not-yet-implemented work. "
          "No DB mutation performed.")
    print("  -> When the matcher lands, this path will: append a game_versions "
          "row, then for each entity extend the open interval (unchanged) or")
    print("     close+open it (changed); the pairing trigger forks the curated "
          "overlay-version rows automatically.")
    sys.exit(3)   # distinct exit: "newer version, matcher required".


def main():
    args = sys.argv[1:]
    rebuild = False
    if args and args[0] == "--rebuild":
        rebuild = True
        args = args[1:]

    if rebuild:
        if len(args) < 2:
            print("usage: python import_to_sqlite.py --rebuild <dump_dir> <out_dir>")
            sys.exit(2)
        run_rebuild(args[0], args[1])
    else:
        if len(args) < 2:
            print("usage (default UPDATE mode): "
                  "python import_to_sqlite.py <out_dir> <game_dir>")
            print("       (rebuild):            "
                  "python import_to_sqlite.py --rebuild <dump_dir> <out_dir>")
            sys.exit(2)
        run_update(args[0], args[1])


if __name__ == "__main__":
    main()
