"""import_to_sqlite.py -- the maintainer-side import: CSV-per-table dump dirs +
the curated seed -> two encoded SQLite reference DBs on the FLATTENED
address-name/address-version schema.

PRODUCES TWO artifacts from one full-dump dir + the curated seed:
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
MODULE_NAME = "WHGame.dll"
MODULE_ID = 1
SCHEMA_VERSION = 1
ABI_CONFIDENCE = "count+width+caller_reg"

# Seed CSV: in-repo curated Address Library. Resolved from this file's location.
HERE = os.path.dirname(os.path.abspath(__file__))
REPO_ROOT = os.path.dirname(os.path.dirname(os.path.dirname(HERE)))  # tools/refdata-extractor/python -> repo
SEED_CSV = os.path.join(REPO_ROOT, "data", "address-library", "seed.csv")

# Dict-encoded columns, keyed by the SCHEMA table+column name (post-transform).
DICT_COLS = {
    "address_names":         ["source"],
    "address_versions":      ["kind", "status", "caller_arg_agreement",
                              "decompile_quality"],
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
    "modules":          ["id", "name"],
    "game_versions":    ["id", "tag", "ordinal", "released"],
    "address_names":    ["id", "name", "is_deprecated", "superseded_by"],
    # `id` IS the kcdx_id (the stable cross-version handle); no separate kcdx_id
    # column. address_versions.kcdx_id references address_names.id.
    # excludes source, notes (DEV-only).
    "address_versions": ["id", "kcdx_id", "kind", "module_id", "rva", "length",
                         "content_hash", "value", "signature",
                         "observed_arg_slots", "caller_reg_arg_count",
                         "caller_arg_agreement", "offset", "vtable_slot",
                         "status", "valid_from", "valid_through"],
    # excludes auto_name, decompile_quality (DEV-only discovery labels)
    "meta":             ["id", "schema_version", "abi_confidence"],
}

# Full schema (DEV): every table, every column, with its SQL column type.
# BLOB = content_hash; INTEGER = dict/int/fk; TEXT = the rest.
SCHEMA = {
    "modules": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("name", "TEXT"),
    ],
    "game_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("tag", "TEXT"),
        ("ordinal", "INTEGER"),
        ("released", "TEXT"),
    ],
    # address_names: ONE ROW PER ENTITY. `id` IS the kcdx_id (the stable
    # cross-version handle plugins reference); not autoincrement -- the importer
    # assigns it explicitly to match the rva-ordinal of the bulk function set,
    # so the same kcdx_id resolves the same entity across rebuilds. `name` is
    # the current canonical name for that entity. Renames overwrite name; an
    # ENTITY-level deprecation (this whole entity superseded by another) is
    # recorded via is_deprecated + superseded_by -> another address_names.id.
    "address_names": [
        ("id", "INTEGER PRIMARY KEY"),         # IS the kcdx_id; explicit, not autoincrement
        ("name", "TEXT"),
        ("is_deprecated", "INTEGER"),
        ("superseded_by", "INTEGER"),          # FK to another address_names.id (entity-to-entity)
        ("source", "INTEGER"),                 # dict, DEV-ONLY
        ("notes", "TEXT"),                     # DEV-ONLY
    ],
    # address_versions: per-(entity, version-interval) resolve facts. kcdx_id
    # non-unique; partial UNIQUE (kcdx_id) WHERE valid_through IS NULL enforces
    # "at most one current form per entity." Every fact a consumer needs to
    # resolve a name to an address + ABI lives here.
    "address_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),                # the stable entity handle
        ("kind", "INTEGER"),                   # dict: function | callsite | vtable_base | etc.
        ("module_id", "INTEGER"),              # FK to modules.id (per-version, theoretically)
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
        ("status", "INTEGER"),                 # dict: verified | unverified
        ("auto_name", "TEXT"),                 # DEV-ONLY (FUN_<rva> for bulk discovery)
        ("decompile_quality", "INTEGER"),      # dict, DEV-ONLY
        ("valid_from", "INTEGER"),             # FK to game_versions.id
        ("valid_through", "INTEGER"),          # NULL = current
    ],
    "meta": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("schema_version", "INTEGER"),
        ("abi_confidence", "TEXT"),
    ],
    "statements": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),
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
        ("kcdx_id", "INTEGER"),
        ("statement_idx", "INTEGER"),
        ("var_name", "TEXT"),
        ("storage_kind", "INTEGER"),            # dict
        ("storage_detail", "TEXT"),
        ("size_bytes", "INTEGER"),
        ("data_type", "INTEGER"),               # dict
    ],
    "call_edges": [   # DEV-ONLY
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("caller_kcdx_id", "INTEGER"),
        ("callee_kcdx_id", "INTEGER"),
        ("callsite_rva", "INTEGER"),
    ],
}

# Table sets per db.
DEV_TABLES = ["modules", "game_versions", "address_names", "address_versions",
              "meta", "statements", "referenced_vars", "call_edges"]
USER_TABLES = ["modules", "game_versions", "address_names", "address_versions",
               "meta"]

# The 9 kinds for address_versions.kind (covering every curated row type seen in
# seed.csv + the bulk function default).
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


def read_seed(seed_csv):
    """Read seed.csv non-comment rows as list[dict]. Skips '#' lines."""
    rows = []
    with open(seed_csv, newline="", encoding="utf-8", errors="replace") as f:
        # Filter comment lines before the CSV parser sees them.
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rd = csv.DictReader(lines)
    for r in rd:
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
# every curated NAME (from seed.csv) gets ONE address_names row pointing at the
# same kcdx_id. The USER projection filters out address_versions rows whose
# kcdx_id is not referenced by any address_names row -- so the bulk 321K stay
# in DEV only, the curated ~140 ship to USER.
# ---------------------------------------------------------------------------
def build_rows(dump_dir, dicts):
    rows = {t: [] for t in DEV_TABLES}

    # --- modules / game_versions / meta singletons ---
    rows["modules"].append({"id": MODULE_ID, "name": MODULE_NAME})
    rows["game_versions"].append({"id": GAME_VERSION_ID, "tag": GAME_VERSION_TAG,
                                  "ordinal": GAME_VERSION_ORDINAL, "released": None})
    rows["meta"].append({"id": 1, "schema_version": SCHEMA_VERSION,
                         "abi_confidence": ABI_CONFIDENCE})

    # --- 1. read functions/, sort by rva, assign kcdx_id 1..N ---
    print("  reading functions/ ...", flush=True)
    functions = []
    for r in iter_table(dump_dir, "functions"):
        functions.append(r)
    # sort by integer rva ascending (empty/None rva functions sort last)
    def fn_rva(r):
        v = parse_int(r.get("rva", ""))
        return (v is None, v if v is not None else 0)
    functions.sort(key=fn_rva)
    rva_to_kcdx = {}
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        if rv is not None:
            rva_to_kcdx[rv] = i
    n_functions = len(functions)
    print(f"  functions: {n_functions} rows -> kcdx_id 1..{n_functions}", flush=True)

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

    # --- 2. address_versions for each bulk function ---
    # Default kind for the bulk = 'function'; curated rows that share an rva will
    # OVERRIDE kind/status/value/etc. in the seed pass below by writing into
    # versions_by_kcdx and re-emitting.
    versions_by_kcdx = {}   # kcdx_id -> row dict (so seed pass can amend)
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        sig = sig_by_rva.get(rv)
        cra = cra_by_rva.get(rv)
        versions_by_kcdx[i] = {
            "kcdx_id": i,
            "kind": dicts.encode("address_versions", "kind", "function"),
            "module_id": MODULE_ID,
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
            # default status for bulk = 'unverified' (the abi_walker floor is not
            # a verified ABI; curated rows overwrite to 'verified' when the seed
            # says so).
            "status": dicts.encode("address_versions", "status", "unverified"),
            "auto_name": (r.get("auto_name") or None),
            "decompile_quality": dicts.encode("address_versions", "decompile_quality",
                                              r.get("decompile_quality", "")),
            "valid_from": GAME_VERSION_ID,
            "valid_through": None,
        }

    # --- 3. read seed.csv; mint curated-only entities; build address_names rows ---
    seed = read_seed(SEED_CSV)
    print(f"  seed.csv: {len(seed)} curated rows", flush=True)
    next_kcdx = n_functions + 1
    seed_id_to_kcdx = {}   # seed.id -> kcdx_id
    n_seed_mapped = 0      # seed rows whose rva matched a bulk function
    n_seed_minted_addr = 0  # seed rows with an rva not in the dump (mint a new kcdx_id)
    n_seed_minted_noaddr = 0  # seed rows with no rva at all (vtable_index)

    for s in seed:
        srva = (s.get("rva") or "").strip()
        if srva:
            rv = parse_int(srva)
            kid = rva_to_kcdx.get(rv)
            if kid is None:
                # seed code row whose rva is not in the dump (a curated address
                # the bulk extractor didn't enumerate -- e.g. a callsite mid-fn).
                kid = next_kcdx
                next_kcdx += 1
                versions_by_kcdx[kid] = {
                    "kcdx_id": kid,
                    "kind": None,   # filled below after infer_kind
                    "module_id": MODULE_ID,
                    "rva": rv,
                    "length": None,
                    "content_hash": None,
                    "value": None,
                    "signature": (s.get("signature") or None) or None,
                    "observed_arg_slots": None,
                    "caller_reg_arg_count": None,
                    "caller_arg_agreement": None,
                    "offset": None,
                    "vtable_slot": None,
                    "status": dicts.encode("address_versions", "status",
                                           s.get("status") or "unverified"),
                    "auto_name": None,
                    "decompile_quality": None,
                    "valid_from": GAME_VERSION_ID,
                    "valid_through": None,
                }
                n_seed_minted_addr += 1
            else:
                n_seed_mapped += 1
        else:
            # vtable_index (or similar) -- no rva; mint a fresh kcdx_id with a
            # version row that carries the slot integer.
            kid = next_kcdx
            next_kcdx += 1
            seed_id_to_kcdx[s.get("id")] = kid
            versions_by_kcdx[kid] = {
                "kcdx_id": kid,
                "kind": None,   # filled below
                "module_id": MODULE_ID,
                "rva": None,
                "length": None,
                "content_hash": None,
                "value": None,   # may be set below (slot int)
                "signature": (s.get("signature") or None) or None,
                "observed_arg_slots": None,
                "caller_reg_arg_count": None,
                "caller_arg_agreement": None,
                "offset": None,
                "vtable_slot": None,
                "status": dicts.encode("address_versions", "status",
                                       s.get("status") or "unverified"),
                "auto_name": None,
                "decompile_quality": None,
                "valid_from": GAME_VERSION_ID,
                "valid_through": None,
            }
            n_seed_minted_noaddr += 1

        # Curated row: override kind/status/offset/vtable_slot on the version row,
        # carry the verified signature, and emit an address_names row.
        kind = infer_kind(s)
        offset, vslot = kind_offset_and_slot(kind, (s.get("notes") or "").lower())
        v = versions_by_kcdx[kid]
        v["kind"] = dicts.encode("address_versions", "kind", kind)
        v["status"] = dicts.encode("address_versions", "status",
                                   s.get("status") or "verified")
        if (s.get("signature") or "").strip():
            v["signature"] = s.get("signature")
        if offset is not None:
            v["offset"] = offset
        if vslot is not None:
            v["vtable_slot"] = vslot
            v["value"] = vslot   # mirror slot int into the generic 'value' col

        rows["address_names"].append({
            "id": kid,                         # id IS the kcdx_id
            "name": (s.get("name") or None),
            "is_deprecated": 0,
            "superseded_by": None,
            "source": dicts.encode("address_names", "source", s.get("source", "")),
            "notes": (s.get("notes") or None),
        })

    print(f"  address_names: {len(rows['address_names'])} rows "
          f"(seed: bulk-matched={n_seed_mapped}, "
          f"minted-with-rva={n_seed_minted_addr}, minted-no-rva={n_seed_minted_noaddr})",
          flush=True)

    # Now emit the address_versions rows. Curated rows are already in
    # versions_by_kcdx (amended); bulk rows pass through unchanged.
    for kid in sorted(versions_by_kcdx.keys()):
        rows["address_versions"].append(versions_by_kcdx[kid])
    n_addr_versions = len(rows["address_versions"])
    n_curated_kcdx_ids = len({r["id"] for r in rows["address_names"]})   # id IS the kcdx_id
    print(f"  address_versions: {n_addr_versions} rows "
          f"(curated kcdx_ids: {n_curated_kcdx_ids})", flush=True)

    # --- 8. statements (DEV) ---
    print("  reading statements/ ...", flush=True)
    n_st = 0
    for r in iter_table(dump_dir, "statements"):
        kid = rva_to_kcdx.get(parse_int(r.get("function_rva", "")))
        if kid is None:
            continue
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
            "kcdx_id": kid,
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
        kid = rva_to_kcdx.get(parse_int(r.get("function_rva", "")))
        if kid is None:
            continue
        rows["referenced_vars"].append({
            "kcdx_id": kid,
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
    print("  reading call_edges/ ...", flush=True)
    n_ce = 0
    n_ce_skip = 0
    for r in iter_table(dump_dir, "call_edges"):
        caller = rva_to_kcdx.get(parse_int(r.get("caller_rva", "")))
        callee = rva_to_kcdx.get(parse_int(r.get("callee_rva", "")))
        # callee_kcdx_id must reference an entity: skip empty/indirect/unknown.
        if caller is None or callee is None:
            n_ce_skip += 1
            continue
        rows["call_edges"].append({
            "caller_kcdx_id": caller,
            "callee_kcdx_id": callee,
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

    # USER row-filter: address_versions narrows to the curated kcdx_ids.
    # DEV writes all rows as before.
    def filter_rows(t, rs):
        if not user_projection or curated_kcdx_ids is None:
            return rs
        if t == "address_versions":
            return [r for r in rs if r["kcdx_id"] in curated_kcdx_ids]
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
    # Partial-unique: at most one OPEN interval per entity. Enforces the "one
    # current form per kcdx_id" invariant at write time.
    con.execute('CREATE UNIQUE INDEX ix_av_open_unique ON address_versions(kcdx_id) '
                'WHERE valid_through IS NULL')
    # address_names.id IS the kcdx_id (PK already indexed); only need a name index.
    con.execute('CREATE INDEX ix_an_name ON address_names(name)')
    if "statements" in tables:
        con.execute('CREATE INDEX ix_st_kcdx ON statements(kcdx_id, idx)')
    if "referenced_vars" in tables:
        con.execute('CREATE INDEX ix_rv_kcdx ON referenced_vars(kcdx_id)')
    if "call_edges" in tables:
        con.execute('CREATE INDEX ix_ce_caller ON call_edges(caller_kcdx_id)')
        con.execute('CREATE INDEX ix_ce_callee ON call_edges(callee_kcdx_id)')

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
    print(f"[import_to_sqlite] seed: {SEED_CSV}")
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

    # Curated kcdx_ids: every entity touched by the curated overlay. This is
    # the set that ships in the USER (production) DB; everything else lives
    # only in the DEV bulk discovery DB. Per parallel-ghidra-research.md §11.8.
    curated_kcdx_ids = {r["id"] for r in rows["address_names"]}   # id IS the kcdx_id
    print(f"  curated kcdx_ids: {len(curated_kcdx_ids)} (USER will ship only these)")

    print(f"\n== DEV DB (bulk discovery superset) -> {dev_db}")
    t0 = time.time()
    dd = write_db(dev_db, rows, dicts, DEV_TABLES, user_projection=False)
    dsz = os.path.getsize(dev_db)
    print(f"  built in {time.time()-t0:.0f}s; size {dsz/1e6:.1f} MB; dict entries {dd}")

    print(f"\n== USER DB (production, curated-only) -> {user_db}")
    t0 = time.time()
    ud = write_db(user_db, rows, dicts, USER_TABLES, user_projection=True,
                  curated_kcdx_ids=curated_kcdx_ids)
    usz = os.path.getsize(user_db)
    print(f"  built in {time.time()-t0:.0f}s; size {usz/1e6:.1f} MB; dict entries {ud}")

    print(bar)
    print("SUMMARY")
    print(f"  USER reference.sqlite     : {usz/1e6:8.1f} MB  (curated-only, {len(curated_kcdx_ids)} kcdx_ids)")
    print(f"  DEV  reference-dev.sqlite : {dsz/1e6:8.1f} MB  (bulk superset, {counts['address_versions']} kcdx_ids)")
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
