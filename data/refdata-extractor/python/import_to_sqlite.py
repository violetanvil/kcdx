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

THE SHARED CORE (seeds_shared/): the schema declaration, the seed validators,
the value/dict codec, and the single address_versions row-builder were extracted
into the private seeds_shared/ package (db-updator Phase 1) so this file's
REBUILD path and the future incremental `apply` path share ONE definition of a
row and cannot drift. This file keeps the full-rebuild ORCHESTRATION (dump
reads, the bulk insert loop, the dev-table builds, write_db, run_rebuild,
read_game_version, the CLI); the shared definitions live in seeds_shared/ and are
re-exported here as module attributes for existing importers.

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

THE ON-DISK VERSION SOURCE this importer uses: <game>/whdlversions.json, which
holds PER-CONFIGURATION build ids; the SHIPPED config is MasterMasterPGO (the
live game runs from Bin/Win64MasterMasterSteamPGO/). The detector reads that
config's versionId, whose build number (e.g. 1164953) is the game's own
monotonic counter -> the ordinal (backfill-safe); tag = branch (release_1_5)
+ build -> "1.5.1164953".

Note: the DLL itself ALSO carries the version, just not as a PE VS_VERSIONINFO
resource. It is interned twice as a .rdata string ("release_1_5_1164953_841";
verified at va=0x183c3edef and va=0x183dba258 in the 1.5.1164953 build, see
_research/init-cycle-recon/_version_strings.txt). The importer uses
whdlversions.json because it's a structured JSON parse rather than a binary
.rdata scan -- not because the DLL lacks the version. Other tools (e.g. the
maintainer seed editor) MAY resolve from the DLL directly.

SCHEMA (FLATTENED 2026-05-28; 5 user tables + 3 DEV-only + _dict_* lookups):
  USER: modules, game_versions, address_names, address_versions, meta.
  DEV adds: statements, referenced_vars, call_edges.
  The full declaration lives in seeds_shared/schema.py.

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

# The shared seed->DB core (schema, validators, value/dict codec, the single
# address_versions row-builder). Extracted from this file so the REBUILD path
# here and the future incremental `apply` path share ONE definition of a row and
# cannot drift (db-updator Phase 1). Ensure this file's own dir is importable so
# `import seeds_shared` resolves whether run as a script or imported by a sibling
# harness (e.g. validate_db_shape.py).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import seeds_shared as ss   # noqa: E402

# Re-export the shared surface as module attributes so existing importers of
# import_to_sqlite (validate_db_shape.py uses imp.read_module_seed / imp.parse_int
# / etc.) keep working unchanged after the extraction.
from seeds_shared import (   # noqa: E402,F401
    SCHEMA,
    USER_COLUMNS,
    DEV_TABLES,
    USER_TABLES,
    DICT_COLS,
    EVIDENCE_KIND_ENUM,
    ADDRESS_KINDS,
    parse_int,
    hash_blob,
    Dicts,
    read_module_seed,
    read_address_names_seed,
    read_address_versions_seed,
    check_kcdx_id_known,
    check_every_entity_covered,
    check_survival_derives_from_known,
    resolve_and_check_name_refs,
    check_supersession_acyclic,
    resolve_version,
    VersionResolveError,
)

# Allow very large quoted CSV fields (seed notes can be long).
csv.field_size_limit(1 << 24)

# ---------------------------------------------------------------------------
# Constants for this baseline import (single game version).
#
# These parameterize the single-version baseline REBUILD orchestration (which
# game_versions / meta rows to emit) -- NOT the schema shape -- so they stay in
# the orchestrator here rather than in seeds_shared/. The row-builder takes the
# version id as an argument, keeping it version-agnostic.
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


# ---------------------------------------------------------------------------
# On-disk game version detection (update mode).
# ---------------------------------------------------------------------------
# The shipped game config, as it appears in whdlversions.json versionId strings
# (the live game runs from Bin/Win64MasterMasterSteamPGO/). The DLL has no PE
# VS_VERSIONINFO resource; this JSON is the source THIS importer uses. (The
# version IS also interned in WHGame.dll's .rdata -- see the module docstring
# at the top of this file -- but parsing JSON is simpler than scanning .rdata.)
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
                           "source this importer parses; an alternative is the "
                           "DLL's own .rdata version string -- see the module "
                           "docstring -- but this importer uses the JSON)"
                           % game_dir)
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
# The full transform: dump + seed -> in-memory row sets for every table.
# Returns a dict table -> list[dict-of-column->value], plus the Dicts encoder.
#
# Flattened schema (2026-05-28): no entities / entity_versions split. Every
# function (curated + bulk) gets ONE address_versions row keyed by its kcdx_id;
# every curated NAME (from address_names_seed.csv) gets ONE address_names row pointing at the
# same kcdx_id. The USER projection filters out address_versions rows whose
# kcdx_id is not referenced by any address_names row -- so the bulk 321K stay
# in DEV only, the curated ~140 ship to USER.
#
# Every address_versions row -- bulk AND curated -- is constructed by the shared
# row-builder in seeds_shared/row_builder.py (build_bulk_row / build_curated_row)
# so the rebuild here and a future incremental `apply` emit byte-identical rows.
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
    #        and sets their kcdx_id). Built via the shared row-builder.
    versions_by_av_id = {}   # av_id -> row dict (so seed pass can amend)
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        sig = sig_by_rva.get(rv)
        cra = cra_by_rva.get(rv)
        versions_by_av_id[i] = ss.build_bulk_row(
            i, rv, r, sig, cra,
            module_id=bulk_module_id,
            valid_from_id=GAME_VERSION_ID,
            kind_id=dicts.encode("address_versions", "kind", "function"),
            agreement_id=(dicts.encode("address_versions", "caller_arg_agreement",
                                       cra.get("agreement", "")) if cra else None),
            decompile_quality_id=dicts.encode("address_versions", "decompile_quality",
                                              r.get("decompile_quality", "")),
            length=parse_int(r.get("length", "")),
            content_hash=hash_blob(r.get("content_hash", "")),
        )

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
            # The cross-row pass below resolves them to ids once the name -> id
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
    #         version) or mint a fresh address_versions row -- both via the
    #         shared row-builder. Every kcdx_id must resolve to an
    #         address_names_seed row.
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

    next_av_id = n_functions + 1
    n_seed_mapped = 0
    n_seed_minted_addr = 0
    n_seed_minted_noaddr = 0

    # Per-kcdx_id survival inputs captured during the seed loop (the kind string
    # + the row's raw survival seed cells). After the curated rows are finalized
    # we map kcdx_id -> av_id and build the `survival` table via the shared
    # builder (db-updator step 5.1). Keyed by kcdx_id: one curated entity per kid
    # at the baseline version, 1:1 with its curated address_versions row.
    survival_inputs_by_kid = {}

    for vs in versions_seed:
        kid = int(vs["kcdx_id"])
        vfv_tag = vs["valid_from_version"].strip()
        ss.check_kcdx_id_known(kid, vfv_tag, valid_kcdx_ids)
        # Only rows for the baseline import version are materialized today.
        # Future-version rows live in the seed but get materialized when the
        # importer is re-run with that version's dump.
        if vfv_tag != GAME_VERSION_TAG:
            continue

        where = (f"address_versions_seed.csv (kcdx_id={kid}, "
                 f"valid_from_version={vfv_tag!r})")
        module_id = _resolve_module(vs["module"].strip(), where)

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

        # Read the AUTHORED kind column (no longer inferred from notes prose).
        # ss.authored_kind re-validates present + in-enum; the seed reader already
        # enforced it at file-read time.
        kind = ss.authored_kind(vs)
        # AUTHORED per-kind datum columns. Each has its own explicit seed column;
        # offset / vtable_slot / struct_offset are read straight from the authored
        # cells (parse_int -> int or NULL). NO prose parsing, NO inference, NO
        # fallback -- the authored column is the SOLE source. `value` is not wired
        # here (build_curated_row has no `value` parameter; it synthesises value
        # from vtable_slot).
        offset = parse_int(vs.get("offset") or "")
        vslot  = parse_int(vs.get("vtable_slot") or "")
        struct_offset = parse_int(vs.get("struct_offset") or "")
        kind_id = dicts.encode("address_versions", "kind", kind)

        # Capture the survival inputs for this curated entity (the kind string +
        # the raw survival seed cells). The survival row is built AFTER the av
        # rows are finalized (so kcdx_id -> av_id is known for derives_from). The
        # survival columns are NULL-valid; step 5.2 fills them. NEVER parse notes.
        sdf = (vs.get("survival_derives_from") or "").strip()
        seu = (vs.get("survival_expect_unique") or "").strip()
        survival_inputs_by_kid[kid] = {
            "kind": kind,
            "survival_aob": (vs.get("survival_aob") or "").strip() or None,
            "anchor_string": (vs.get("survival_anchor_string") or "").strip() or None,
            "rule": (vs.get("survival_rule") or "").strip() or None,
            "slot_count": parse_int(vs.get("survival_slot_count") or ""),
            "expect_unique": int(seu) if seu else None,
            "derives_from_kid": int(sdf) if sdf else None,
        }

        if srva:
            rv = parse_int(srva)
            av_id = rva_to_av_id.get(rv)
            if av_id is None:
                av_id = next_av_id
                next_av_id += 1
                versions_by_av_id[av_id] = ss.build_curated_row(
                    av_id, kid, base_row=None,
                    module_id=module_id, rva=rv,
                    valid_from_id=GAME_VERSION_ID, kind_id=kind_id,
                    signature=sig, lvv_id=lvv_id, verified_by=vby,
                    verified_date=vdt, evidence_kind_id=ekn_id,
                    offset=offset, vtable_slot=vslot, struct_offset=struct_offset)
                n_seed_minted_addr += 1
            elif kind in ss.FUNCTION_KINDS:
                # Function kind whose RVA matches a bulk row: PROMOTE it, keeping
                # the bulk fingerprint columns (length/content_hash/abi_walker).
                # The fingerprint is a function-body checksum the survival check
                # re-hashes; it is meaningful only for a function.
                versions_by_av_id[av_id] = ss.build_curated_row(
                    av_id, kid, base_row=versions_by_av_id[av_id],
                    module_id=module_id, rva=rv,
                    valid_from_id=GAME_VERSION_ID, kind_id=kind_id,
                    signature=sig, lvv_id=lvv_id, verified_by=vby,
                    verified_date=vdt, evidence_kind_id=ekn_id,
                    offset=offset, vtable_slot=vslot, struct_offset=struct_offset)
                n_seed_mapped += 1
            else:
                # NON-function kind whose RVA coincides with a bulk function
                # entry (e.g. a callsite or wrapper at a function's first byte):
                # MINT with base_row=None so the fingerprint columns stay NULL.
                # A function-body hash is the wrong survival datum for a
                # callsite/data_slot/etc. (it resolves by AOB/derivation, not a
                # body checksum), so it must NOT inherit the bulk fingerprint.
                # This MINTS a fresh av_id rather than promoting the bulk row in
                # place, leaving the bulk row uncurated (kcdx_id NULL) as it was.
                av_id = next_av_id
                next_av_id += 1
                versions_by_av_id[av_id] = ss.build_curated_row(
                    av_id, kid, base_row=None,
                    module_id=module_id, rva=rv,
                    valid_from_id=GAME_VERSION_ID, kind_id=kind_id,
                    signature=sig, lvv_id=lvv_id, verified_by=vby,
                    verified_date=vdt, evidence_kind_id=ekn_id,
                    offset=offset, vtable_slot=vslot, struct_offset=struct_offset)
                n_seed_minted_addr += 1
        else:
            av_id = next_av_id
            next_av_id += 1
            versions_by_av_id[av_id] = ss.build_curated_row(
                av_id, kid, base_row=None,
                module_id=module_id, rva=None,
                valid_from_id=GAME_VERSION_ID, kind_id=kind_id,
                signature=sig, lvv_id=lvv_id, verified_by=vby,
                verified_date=vdt, evidence_kind_id=ekn_id,
                offset=offset, vtable_slot=vslot, struct_offset=struct_offset)
            n_seed_minted_noaddr += 1

    # Every kcdx_id in address_names_seed.csv must have a matching row in
    # address_versions_seed.csv for the baseline import version -- else the
    # entity has a name but no resolve facts.
    covered_kids = {v["kcdx_id"] for v in versions_by_av_id.values()
                    if v["kcdx_id"] is not None}
    ss.check_every_entity_covered(valid_kcdx_ids, covered_kids, GAME_VERSION_TAG)

    # Survival DAG FK closure: every non-empty survival_derives_from must point at
    # an existing entity (shared with apply via seeds_shared.validators).
    ss.check_survival_derives_from_known(versions_seed, valid_kcdx_ids)

    # rva -> kcdx_id (curated only) for the DEV-only tables' kcdx_id column.
    # rva -> av_id (universal) for the DEV-only tables' address_version_id column.
    rva_to_kcdx_id = {v["rva"]: v["kcdx_id"]
                      for v in versions_by_av_id.values()
                      if v["rva"] is not None and v["kcdx_id"] is not None}

    # --- 4. RESOLVE supersession / deprecation references + integrity checks.
    #
    # The cross-row checks (pair integrity, name/tag FK resolution, supersession
    # acyclicity) live in seeds_shared/validators.py so the rebuild and a future
    # apply share them. They mutate the address_names rows in place, replacing
    # the seed strings with resolved ids.
    name_to_id = {r["name"]: r["id"] for r in rows["address_names"] if r["name"]}
    tag_to_id  = {gv["tag"]: gv["id"] for gv in rows["game_versions"]}

    ss.resolve_and_check_name_refs(rows["address_names"], name_to_id, tag_to_id)

    n_superseded = ss.check_supersession_acyclic(rows["address_names"])
    if n_superseded:
        print(f"  supersession: {n_superseded} edge(s); acyclicity validated",
              flush=True)

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

    # --- survival (db-updator step 5.1) ---
    # One `survival` row per CURATED address_versions row, via the shared builder
    # (so rebuild + apply emit identical survival rows). function kinds carry the
    # body fingerprint already on the av row (reused); the search/derivation kinds
    # carry their seed survival datum when present (step 5.2) and an empty payload
    # when not. derives_from: the seed's survival_derives_from kcdx_id maps to the
    # dependency entity's curated address_versions.id.
    kid_to_av_id = {v["kcdx_id"]: v["id"]
                    for v in versions_by_av_id.values()
                    if v["kcdx_id"] is not None}
    n_surv = 0
    for av_id in sorted(versions_by_av_id.keys()):
        v = versions_by_av_id[av_id]
        if v["kcdx_id"] is None:
            continue   # bulk uncurated rows get no survival datum
        si = survival_inputs_by_kid.get(v["kcdx_id"])
        if si is None:
            # A curated row with no captured survival input would mean the seed
            # loop never saw it -- impossible for a baseline curated entity. Fail
            # loud rather than silently skip (the survival table must be 1:1).
            raise RuntimeError(
                f"build_rows: curated address_versions id={av_id} "
                f"(kcdx_id={v['kcdx_id']}) has no survival input captured")
        df_kid = si["derives_from_kid"]
        derives_from_av_id = kid_to_av_id.get(df_kid) if df_kid is not None else None
        rows["survival"].append(ss.build_survival_row(
            av_id, si["kind"],
            survival_aob=si["survival_aob"],
            anchor_string=si["anchor_string"],
            rule=si["rule"],
            slot_count=si["slot_count"],
            expect_unique=si["expect_unique"],
            derives_from_av_id=derives_from_av_id,
            content_hash=v.get("content_hash"),
            length=v.get("length")))
        n_surv += 1
    print(f"  survival: {n_surv} rows (1:1 with curated address_versions)",
          flush=True)

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
    if "survival" in tables:
        # 1:1 join key (survival -> its owning av row) + the DAG walk key
        # (derives_from). Both DBs carry the survival table.
        con.execute('CREATE UNIQUE INDEX ix_sv_av ON survival(address_version_id)')
        con.execute('CREATE INDEX ix_sv_derives ON survival(derives_from)')

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


# ---------------------------------------------------------------------------
# APPLY mode (incremental seed->DB applier; db-updator Phase 1).
#
# Lands hand-edited seed-CSV deltas into BOTH reference DBs (user + dev) WITHOUT
# a full rebuild. This step (Phase 1, step 3) wires the full spine and implements
# the SIMPLEST action -- re-verify (the audit-trio UPDATE). Add-entity /
# add-versions-row / deprecate / supersede are later steps (plan.md S3); a seed
# row that would require an INSERT is counted and SKIPPED here, never inserted.
#
# Re-verify mutates only the four audit-trio columns of an existing
# address_versions row (last_verified_at_version, verified_by, verified_date,
# evidence_kind) -- see plan.md S2 "Where each field comes from" + S3 "Re-verify".
# It needs no dump and no dev-DB fingerprint read, so it is the right first action
# to exercise the scaffold (resolve version -> validate full seed -> diff -> write
# both DBs) without the kind-class complexity.
#
# The re-verify path is IDEMPOTENT: the UPDATE's predicate selects the same row
# and the column values derive purely from the CSV, so applying twice == once.
# ---------------------------------------------------------------------------
APPLY_VERSION_REFUSE_EXIT = 4   # distinct from run_update's sys.exit(3).


def _open_rw(db_path, which):
    """Open an existing reference DB read-write. Refuse (clear message) if the
    file is missing -- re-verify amends an existing row, so a baseline must
    already be present (plan.md S3 step 3)."""
    if not os.path.isfile(db_path):
        raise RuntimeError(
            f"no baseline {which} DB at {db_path}; run --rebuild first "
            f"(apply amends existing rows, it does not build the baseline)")
    return sqlite3.connect(db_path)


def _db_tag_to_id(con, tag, where):
    """Map a game_versions.tag (e.g. '1.5.1164953') to its game_versions.id FK.
    The audit-trio columns (last_verified_at_version, valid_from) store
    game_versions.id ints; the seed carries TAGS. Refuse if the tag is absent
    (a version the DB was never built for)."""
    row = con.execute("SELECT id FROM game_versions WHERE tag = ?", (tag,)).fetchone()
    if row is None:
        raise RuntimeError(
            f"{where}: version tag {tag!r} is not present in the DB's "
            f"game_versions table; the baseline was never built for it "
            f"(run --rebuild for that version first)")
    return row[0]


def _db_evidence_kind_id(con, val, where):
    """Look up the EXISTING dict id for an evidence_kind value from the DB's
    _dict_address_versions_evidence_kind table. CRITICAL: we must reuse the dict
    id the rebuild minted -- calling Dicts().encode() fresh would start ids at 1
    and not match the DB's existing ids. A value absent from the dict table means
    it was never seen at rebuild time; that needs a rebuild, so we refuse (a new
    evidence_kind cannot be introduced by an incremental re-verify)."""
    if val is None or val == "":
        return None
    row = con.execute(
        "SELECT id FROM _dict_address_versions_evidence_kind WHERE val = ?",
        (val,)).fetchone()
    if row is None:
        raise RuntimeError(
            f"{where}: evidence_kind {val!r} is not a known dict value in the DB "
            f"(_dict_address_versions_evidence_kind); a new evidence_kind needs a "
            f"--rebuild, it cannot be introduced by an incremental apply")
    return row[0]


def _db_dict_id(con, table, col, val, where):
    """Look up the EXISTING dict id for a (table, col, val) from the DB's
    _dict_<table>_<col> table. Same reuse-not-re-encode rule as
    _db_evidence_kind_id: the rebuild minted these ids; apply must reuse them so a
    minted/promoted row's dict-encoded column (e.g. `kind`) matches the rebuild's
    id, not a fresh-from-1 id. A value absent from the dict table means it was
    never seen at rebuild time -> refuse (a new dict value needs a --rebuild).

    `kind` is the column that matters here: every curated row carries a kind, and
    all nine kinds are seen during a rebuild (the bulk pass registers `function`
    first; the seed pass registers the rest), so a function/callsite/... kind
    always resolves. The refusal guards a hypothetical never-before-seen kind."""
    if val is None or val == "":
        return None
    tbl = f"_dict_{table}_{col}"
    row = con.execute(f'SELECT id FROM "{tbl}" WHERE val = ?', (val,)).fetchone()
    if row is None:
        raise RuntimeError(
            f"{where}: {col}={val!r} is not a known dict value in the DB "
            f"({tbl}); a new {col} value needs a --rebuild, it cannot be "
            f"introduced by an incremental apply")
    return row[0]


def _validate_full_seed_state():
    """Run the FULL seed-CSV validation gate (plan.md S6 'Validation gate'):
    read all three seeds through the shared validators and run every cross-row
    check, exactly as build_rows does. Raises RuntimeError on ANY failure so the
    caller aborts before opening or writing a DB.

    This mirrors build_rows' seed-read + validator-call sequence WITHOUT the dump
    reads: read_module_seed / read_address_names_seed / read_address_versions_seed
    (each fail-loud on structure), then check_kcdx_id_known per versions row,
    check_every_entity_covered, resolve_and_check_name_refs,
    check_supersession_acyclic over the FULL seed state."""
    module_rows = read_module_seed(MODULE_SEED_CSV)
    modules_by_id = {}
    modules_by_name = {}
    for m in module_rows:
        mid = int(m["id"])
        modules_by_id[mid] = mid
        modules_by_name[m["name"].strip()] = mid

    names_seed = read_address_names_seed(ADDRESS_NAMES_SEED_CSV)
    valid_kcdx_ids = {int(ns["id"]) for ns in names_seed}

    versions_seed = read_address_versions_seed(ADDRESS_VERSIONS_SEED_CSV)

    # Per-row: kcdx_id must be a known name id, and module must resolve. Only
    # baseline-version rows are covered/materialized (mirrors build_rows).
    covered_kids = set()
    for vs in versions_seed:
        kid = int(vs["kcdx_id"])
        vfv_tag = vs["valid_from_version"].strip()
        check_kcdx_id_known(kid, vfv_tag, valid_kcdx_ids)
        if vfv_tag != GAME_VERSION_TAG:
            continue
        where = (f"address_versions_seed.csv (kcdx_id={kid}, "
                 f"valid_from_version={vfv_tag!r})")
        raw_mod = vs["module"].strip()
        try:
            mid = int(raw_mod)
            if mid not in modules_by_id:
                raise RuntimeError(
                    f"{where}: module id {mid} matches no module_seed.csv row")
        except ValueError:
            if raw_mod not in modules_by_name:
                raise RuntimeError(
                    f"{where}: module {raw_mod!r} matches no module_seed.csv row "
                    f"by name")
        covered_kids.add(kid)

    check_every_entity_covered(valid_kcdx_ids, covered_kids, GAME_VERSION_TAG)

    # Survival DAG FK closure (db-updator step 5.1): every non-empty
    # survival_derives_from references an existing entity. Shared with rebuild.
    check_survival_derives_from_known(versions_seed, valid_kcdx_ids)

    # Cross-row name/tag resolution + supersession acyclicity over the full
    # name seed. Build the same pre-resolution name rows build_rows constructs.
    name_rows = []
    for ns in names_seed:
        dep = (ns.get("is_deprecated") or "").strip()
        name_rows.append({
            "id": int(ns["id"]),
            "name": ns["name"].strip(),
            "superseded_by": (ns.get("superseded_by") or "").strip() or None,
            "superseded_at_version": (ns.get("superseded_at_version") or "").strip() or None,
            "is_deprecated": 1 if dep in ("1", "true", "yes") else 0,
            "deprecated_at_version": (ns.get("deprecated_at_version") or "").strip() or None,
            "deprecation_replacement": (ns.get("deprecation_replacement") or "").strip() or None,
            "notes": (ns.get("notes") or None),
        })
    name_to_id = {r["name"]: r["id"] for r in name_rows if r["name"]}
    # tag_to_id: the baseline version tag is the one game_versions row.
    tag_to_id = {GAME_VERSION_TAG: GAME_VERSION_ID}
    resolve_and_check_name_refs(name_rows, name_to_id, tag_to_id)
    check_supersession_acyclic(name_rows)

    # name_rows are now RESOLVED (seed strings replaced by ids in place), exactly
    # as build_rows leaves them before write_db -- so the add-entity names INSERT
    # below writes the same column values the rebuild would. Index them by id for
    # the add path (the per-entity name row). kind / offset / vtable_slot /
    # struct_offset are all AUTHORED versions-seed columns now -- no notes cue is
    # read for any value.
    names_by_id = {r["id"]: r for r in name_rows}
    id_to_name = {r["id"]: r["name"] for r in name_rows}

    return {
        "versions_seed": versions_seed,
        "names_by_id": names_by_id,        # id -> RESOLVED address_names row dict
        "id_to_name": id_to_name,          # id -> name
        "modules_by_id": modules_by_id,
        "modules_by_name": modules_by_name,
    }


# Function kind-classes drive the fingerprint-vs-NULL decision (context.md
# decision 3; plan.md S3). The kind-class -- NOT the rva-match -- decides:
#   (a) ss.FUNCTION_KINDS -> promote a bulk row, KEEP its fingerprint columns.
#   (b) RVA-bearing non-function kinds -> mint, fingerprint columns NULL.
#   (c) vtable_index (rva-less)        -> mint, all NULL.
# FUNCTION_KINDS is the single shared definition in seeds_shared.schema (ss.),
# so the rebuild promote-gate and this apply path cannot drift.
APPLY_BASELINE_REFUSE_EXIT = 5   # function-kind add with no bulk baseline.


def _seed_action_rows(state):
    """From the validated seed state, build the per-row action facts the apply
    diff classifies against each DB. Returns list of dicts -- one per
    baseline-version versions-seed row -- carrying everything BOTH the re-verify
    and the add/promote paths need, kind-derived ONCE here (kind derivation is
    DB-independent; only present-vs-absent classification is per-DB).

    Mirrors build_rows: kind / offset / vtable_slot / struct_offset are all
    AUTHORED columns on the versions seed (kind via ss.authored_kind; the per-kind
    datum cells via parse_int). NO prose parsing, NO inference -- the authored
    columns are the sole source, read here exactly as build_rows reads them."""
    versions_seed = state["versions_seed"]

    out = []
    for vs in versions_seed:
        vfv_tag = vs["valid_from_version"].strip()
        if vfv_tag != GAME_VERSION_TAG:
            continue
        kid = int(vs["kcdx_id"])
        srva = (vs.get("rva") or "").strip()
        sig = (vs.get("signature") or "").strip()
        lvv = (vs.get("last_verified_at_version") or "").strip()
        vby = (vs.get("verified_by") or "").strip()
        vdt = (vs.get("verified_date") or "").strip()
        ekn = (vs.get("evidence_kind") or "").strip()

        kind = ss.authored_kind(vs)
        # AUTHORED per-kind datum columns -- read straight from the authored cells,
        # identically to build_rows (the same authored columns, the same way), so
        # apply == rebuild. NO prose parsing, NO inference, NO fallback. `value` is
        # not wired (see build_rows).
        offset = parse_int(vs.get("offset") or "")
        vslot  = parse_int(vs.get("vtable_slot") or "")
        struct_offset = parse_int(vs.get("struct_offset") or "")

        # Survival inputs (db-updator step 5.1): the raw survival seed cells, kept
        # alongside the action so the add path can build the survival row via the
        # SAME shared builder the rebuild uses. NULL-valid; step 5.2 fills them.
        sdf = (vs.get("survival_derives_from") or "").strip()

        out.append({
            "kcdx_id": kid,
            "module": vs["module"].strip(),
            "valid_from_tag": vfv_tag,
            "rva": parse_int(srva) if srva else None,
            "kind": kind,
            "signature": sig,
            "offset": offset,
            "vtable_slot": vslot,
            "struct_offset": struct_offset,
            "lvv_tag": lvv,
            "verified_by": vby or None,
            "verified_date": vdt or None,
            "evidence_kind": ekn or None,
            # survival seed cells (raw; the builder maps them to the payload).
            "survival_aob": (vs.get("survival_aob") or "").strip() or None,
            "survival_anchor_string": (vs.get("survival_anchor_string") or "").strip() or None,
            "survival_rule": (vs.get("survival_rule") or "").strip() or None,
            "survival_slot_count": parse_int(vs.get("survival_slot_count") or ""),
            "survival_expect_unique": (
                int((vs.get("survival_expect_unique") or "").strip())
                if (vs.get("survival_expect_unique") or "").strip() else None),
            "survival_derives_from_kid": int(sdf) if sdf else None,
        })
    return out


def _resolve_module_id(con, raw, where):
    """Map the seed `module` value (id int or name) to modules.id in the open DB.
    Mirrors build_rows' _resolve_module, but reads the DB's modules table rather
    than the in-memory seed (apply never builds the seed module rows)."""
    try:
        mid = int(raw)
        row = con.execute("SELECT id FROM modules WHERE id = ?", (mid,)).fetchone()
        if row is not None:
            return row[0]
        raise RuntimeError(
            f"{where}: module={raw!r} parses as int but no modules row has id={mid}")
    except ValueError:
        pass
    row = con.execute("SELECT id FROM modules WHERE name = ?", (raw,)).fetchone()
    if row is None:
        raise RuntimeError(
            f"{where}: module={raw!r} matches no modules row by name")
    return row[0]


def _next_av_id(con):
    """MAX(id)+1 from this DB's address_versions (the mint id; context.md
    decision / brief item 3). Queried PER-DB -- the user and dev DBs carry
    different max ids (dev has the ~321K bulk rows; user only the curated set),
    so apply must NOT assume they match. The id is an internal handle; the oracle
    compares row-SETS keyed by (kcdx_id, valid_from) with id EXCLUDED, so a valid
    non-colliding id that differs from the rebuild's is acceptable."""
    row = con.execute("SELECT MAX(id) FROM address_versions").fetchone()
    return (row[0] or 0) + 1


def _read_bulk_row(con, rva):
    """Read the bulk (uncurated) address_versions row at `rva` from the DEV DB as
    a dict shaped like build_curated_row's `base_row` (the same keys build_bulk_row
    emits). Returns None if no bulk row at that rva. Used by the function-kind
    PROMOTE path: build_curated_row copies this dict and overwrites the curated/
    audit fields, KEEPING the fingerprint columns -- byte-identical to how the
    rebuild promotes from its in-memory bulk dict."""
    cols = [c for c, _ in SCHEMA["address_versions"]]
    sel = ",".join(f'"{c}"' for c in cols)
    row = con.execute(
        f'SELECT {sel} FROM address_versions WHERE rva = ? AND kcdx_id IS NULL',
        (rva,)).fetchone()
    if row is None:
        return None
    return dict(zip(cols, row))


def _projected_insert(con, av_row, user_projection):
    """INSERT one fully-built address_versions row dict into the open DB, applying
    the same column projection write_db uses (USER drops the DEV-only columns).
    The row dict is the build_curated_row output (all DEV columns present); the
    USER projection emits only USER_COLUMNS['address_versions']."""
    cols = [c for c, _ in SCHEMA["address_versions"]]
    if user_projection:
        allowed = USER_COLUMNS["address_versions"]
        cols = [c for c in cols if c in allowed]
    placeholders = ",".join("?" * len(cols))
    con.execute(
        f'INSERT INTO address_versions ({",".join(cols)}) VALUES ({placeholders})',
        [av_row.get(c) for c in cols])


def _insert_names_row(con, name_row, user_projection):
    """INSERT one RESOLVED address_names row dict into the open DB. address_names
    is all-rows in BOTH DBs (USER drops only `notes`, the lone DEV-only column).
    The row dict is a resolved name row from _validate_full_seed_state (seed
    strings already replaced by ids), so its supersession/deprecation FKs match
    what the rebuild writes."""
    cols = [c for c, _ in SCHEMA["address_names"]]
    if user_projection:
        allowed = USER_COLUMNS["address_names"]
        cols = [c for c in cols if c in allowed]
    placeholders = ",".join("?" * len(cols))
    con.execute(
        f'INSERT INTO address_names ({",".join(cols)}) VALUES ({placeholders})',
        [name_row.get(c) for c in cols])


def _resolve_derives_from_av_id(con, df_kid):
    """Map a survival_derives_from kcdx_id -> the dependency entity's curated
    address_versions.id in THIS open DB (the survival DAG edge is an FK to
    address_versions.id, but the seed carries a kcdx_id). Returns None when the
    seed has no dependency (df_kid is None). Picks the OPEN interval row
    (valid_through IS NULL) -- the current curated form of the dependency, the
    same row the rebuild's kid_to_av_id maps to (the baseline has one open row
    per curated entity). A df_kid with no curated row in the DB -> refuse (the
    full-seed validator already FK-checked it against the names seed; a missing
    DB row means the dependency entity itself was never added)."""
    if df_kid is None:
        return None
    row = con.execute(
        "SELECT id FROM address_versions WHERE kcdx_id = ? AND "
        "valid_through IS NULL", (df_kid,)).fetchone()
    if row is None:
        raise RuntimeError(
            f"survival_derives_from kcdx_id={df_kid} has no curated "
            f"address_versions row in the DB (add the dependency entity first)")
    return row[0]


def _insert_survival_row(con, sv_row, user_projection):
    """INSERT one fully-built survival row dict into the open DB, applying the
    same column projection write_db uses (`id` is autoincrement, omitted; USER
    drops any DEV-only survival column -- there are none today, so USER == DEV for
    survival). The row dict is the build_survival_row output."""
    cols = [c for c, _ in SCHEMA["survival"] if c != "id"]
    if user_projection:
        allowed = USER_COLUMNS["survival"]
        cols = [c for c in cols if c in allowed]
    placeholders = ",".join("?" * len(cols))
    con.execute(
        f'INSERT INTO survival ({",".join(cols)}) VALUES ({placeholders})',
        [sv_row.get(c) for c in cols])


# The supersession + deprecation columns -- the names-side entity-level state.
# A change to any of these on an EXISTING names row is the deprecate/supersede
# delta this step classifies + applies (db-updator step 6).
_NAME_DEP_SUP_COLS = ("superseded_by", "superseded_at_version", "is_deprecated",
                      "deprecated_at_version", "deprecation_replacement")

# The supersession (predecessor-edge) subset and the deprecation subset. A names
# edit is classified by which subset changed; both are independent names-side
# UPDATEs that the validator's pair-integrity + acyclicity gate has already
# accepted over the FULL seed state before any of this runs.
_SUP_COLS = ("superseded_by", "superseded_at_version")
_DEP_COLS = ("is_deprecated", "deprecated_at_version", "deprecation_replacement")


def _classify_name_edits(con, state):
    """Classify (do NOT write) the deprecate/supersede edits on EXISTING names
    rows for one open DB.

    For every kcdx_id whose names row is already in this DB, compare the resolved
    seed names row's entity-level supersession/deprecation columns against the DB
    row. Returns (deprecate_actions, supersede_actions, n_other_skipped):
      - supersede_actions -- the supersession-edge subset changed (the
        predecessor gained/changed superseded_by + superseded_at_version). Each is
        {kcdx_id, superseded_by, superseded_at_version} -- the resolved ids to
        write. (Supersede's SUCCESSOR entity Y lands via the existing add-entity
        path; it is a separate versions-seed action, not handled here.)
      - deprecate_actions -- the deprecation subset changed (is_deprecated +
        deprecated_at_version + optional deprecation_replacement). Each is
        {kcdx_id, is_deprecated, deprecated_at_version, deprecation_replacement}.
      - n_other_skipped -- a names row whose columns differ but in neither subset
        we apply (e.g. an UN-deprecate / UN-supersede back to NULL, which Phase 1
        does not yet model); counted, not written.

    `is_deprecated` is normalized (DB stores 0/1; the resolved seed row already
    carries 0/1). A brand-new entity (no DB names row) is the add path's job and
    is not classified here. A row whose dep/sup columns already MATCH the DB is a
    no-op (not counted in any bucket -- nothing to do)."""
    deprecate_actions = []
    supersede_actions = []
    n_other = 0
    for kid, name_row in state["names_by_id"].items():
        db = con.execute(
            f'SELECT {",".join(_NAME_DEP_SUP_COLS)} FROM address_names '
            f'WHERE id = ?', (kid,)).fetchone()
        if db is None:
            continue   # brand-new entity -> handled by the add path, not here
        db_map = dict(zip(_NAME_DEP_SUP_COLS, db))
        seed_map = {
            c: ((1 if name_row.get(c) else 0) if c == "is_deprecated"
                else name_row.get(c))
            for c in _NAME_DEP_SUP_COLS}
        if db_map == seed_map:
            continue   # no-op: nothing changed on this entity's edges

        # A NEW supersession edge: the predecessor had no superseded_by in the DB
        # and the seed now sets one (Phase 1 models setting the edge, not clearing
        # or rewriting an existing one).
        sup_changed = any(db_map[c] != seed_map[c] for c in _SUP_COLS)
        new_supersede = (sup_changed and db_map["superseded_by"] is None
                         and seed_map["superseded_by"] is not None)
        # A NEW deprecation: the entity was not deprecated in the DB and the seed
        # now deprecates it (Phase 1 models setting deprecation, not un-setting).
        dep_changed = any(db_map[c] != seed_map[c] for c in _DEP_COLS)
        new_deprecate = (dep_changed and not db_map["is_deprecated"]
                         and seed_map["is_deprecated"])

        if new_supersede:
            supersede_actions.append({
                "kcdx_id": kid,
                "superseded_by": seed_map["superseded_by"],
                "superseded_at_version": seed_map["superseded_at_version"],
            })
        if new_deprecate:
            deprecate_actions.append({
                "kcdx_id": kid,
                "is_deprecated": seed_map["is_deprecated"],
                "deprecated_at_version": seed_map["deprecated_at_version"],
                "deprecation_replacement": seed_map["deprecation_replacement"],
            })
        if not (new_supersede or new_deprecate):
            # A diff we do not yet model (un-deprecate, un-supersede, or rewriting
            # an existing edge) -> count skipped, leave the DB untouched.
            n_other += 1
    return deprecate_actions, supersede_actions, n_other


def _apply_deprecate(con, action, tx):
    """Apply one deprecate action: UPDATE the entity's address_names row to set
    is_deprecated + deprecated_at_version + (optional) deprecation_replacement.
    Its own per-action transaction via `tx` (plan.md S6) -- BEGIN/COMMIT immediate,
    SAVEPOINT/RELEASE inside the held outer txn deferred. The resolved ids in
    `action` already match what the rebuild writes (validation ran the resolution +
    pair-integrity + acyclicity gate over the full seed). address_names is all-rows
    in BOTH DBs, so the projection does not drop any of these columns -- the UPDATE
    is the same for user + dev."""
    kid = action["kcdx_id"]
    tx.begin()
    try:
        con.execute(
            "UPDATE address_names SET is_deprecated = ?, "
            "deprecated_at_version = ?, deprecation_replacement = ? WHERE id = ?",
            (action["is_deprecated"], action["deprecated_at_version"],
             action["deprecation_replacement"], kid))
        tx.commit()
    except Exception:
        tx.rollback()
        raise


def _apply_supersede(con, action, tx):
    """Apply one supersede action: UPDATE the PREDECESSOR's address_names row to
    set superseded_by + superseded_at_version. Its own per-action transaction via
    `tx` (plan.md S6) -- BEGIN/COMMIT immediate, SAVEPOINT/RELEASE inside the held
    outer txn deferred. The successor entity Y lands via the existing add-entity
    path (a separate versions-seed action); this writes ONLY the predecessor edge.
    The resolved ids in `action` already match the rebuild's (validation's
    acyclicity + pair-integrity gate accepted the full seed before any write)."""
    kid = action["kcdx_id"]
    tx.begin()
    try:
        con.execute(
            "UPDATE address_names SET superseded_by = ?, "
            "superseded_at_version = ? WHERE id = ?",
            (action["superseded_by"], action["superseded_at_version"], kid))
        tx.commit()
    except Exception:
        tx.rollback()
        raise


def _apply_one_db(con, actions, state, which, user_projection, deferred=False):
    """Compute + apply the full apply delta for one open DB connection.

    For each per-row action fact, look the row up by (kcdx_id, valid_from-as-id):
      - PRESENT -> re-verify (audit-trio UPDATE) or no-op (step-3 path).
      - ABSENT  -> an ADD, sub-classified:
          * the kcdx_id has NO address_versions row at all -> add-entity
            (also INSERT the address_names row).
          * the kcdx_id has a row at a DIFFERENT valid_from -> add-versions-row
            (close the prior open interval, then INSERT the new row).
        The kind-class (NOT the rva-match) decides fingerprint-vs-NULL:
          (a) function kind -> PROMOTE the bulk row (KEEP fingerprint); the
              baseline-present gate refuses if the DEV DB has no bulk row at the
              rva (never mints a NULL-fingerprint function).
          (b) rva-bearing non-function kind -> mint, fingerprint NULL.
          (c) vtable_index -> mint, all NULL.

    Each ACTION is wrapped in its own per-action transaction via the `tx` control
    object (plan.md S6). IMMEDIATE (deferred=False, default): that is a real
    BEGIN; ...; COMMIT; per action -- byte-identical to before. DEFERRED
    (deferred=True, step 4a): each action is a SAVEPOINT/RELEASE pair nested inside
    one outer txn the caller opened + holds, so nothing commits until commit(handle)
    -- the WRITE logic is identical, only WHEN it commits differs. A
    deprecate/supersede edit on an EXISTING names row is a names-side UPDATE
    (db-updator step 6); an edit we do not yet model (un-deprecate / rewrite an
    existing edge) is counted skipped, never written. Returns a counts dict."""
    # The per-action transaction seam: BEGIN/COMMIT in immediate mode (default) vs
    # SAVEPOINT/RELEASE inside the caller's held outer txn in deferred mode. Threaded
    # to every action site below + the two lifecycle helpers so the deferred mode
    # holds ALL of this DB's writes in one outer txn.
    tx = _Tx(con, deferred)
    counts = {"reverified": 0, "noop": 0, "added_entity": 0,
              "added_versions_row": 0, "deprecated": 0, "superseded": 0,
              "skipped_dep_sup": 0}

    # Classify the entity-level deprecation/supersession edits on EXISTING names
    # rows: compare the resolved seed names row against the DB names row for every
    # kcdx_id already present in this DB. A NEW deprecation or a NEW supersession
    # edge is applied below as its own names-side UPDATE; an edit we do not yet
    # model is counted skipped (plan.md S3). Brand-new entities are handled by the
    # add path below (their names row -- supersession/deprecation FKs included --
    # is INSERTed there), so they are not classified here. The full-seed validator
    # already ran the pair-integrity + acyclicity gate, so no cycle / half-set pair
    # can reach the write below.
    deprecate_actions, supersede_actions, n_skipped = _classify_name_edits(
        con, state)
    counts["skipped_dep_sup"] = n_skipped
    for da in deprecate_actions:
        _apply_deprecate(con, da, tx)
        counts["deprecated"] += 1
    for sa in supersede_actions:
        _apply_supersede(con, sa, tx)
        counts["superseded"] += 1

    for a in actions:
        kid = a["kcdx_id"]
        where = (f"{which} DB (kcdx_id={kid}, valid_from={a['valid_from_tag']!r})")
        vf_id = _db_tag_to_id(con, a["valid_from_tag"], where)
        lvv_id = _db_tag_to_id(con, a["lvv_tag"], where) if a["lvv_tag"] else None
        ekn_id = _db_evidence_kind_id(con, a["evidence_kind"], where)

        existing = con.execute(
            "SELECT last_verified_at_version, verified_by, verified_date, "
            "evidence_kind FROM address_versions WHERE kcdx_id = ? AND "
            "valid_from = ?", (kid, vf_id)).fetchone()

        if existing is not None:
            # PRESENT -> re-verify or no-op. The audit trio is the only mutable
            # part (plan.md S2). A change to a deprecation/supersession edge on
            # the names row is step-5 scope and not handled here.
            cur = (existing[0], existing[1], existing[2], existing[3])
            new = (lvv_id, a["verified_by"], a["verified_date"], ekn_id)
            if cur == new:
                counts["noop"] += 1
                continue
            tx.begin()
            try:
                con.execute(
                    "UPDATE address_versions SET last_verified_at_version = ?, "
                    "verified_by = ?, verified_date = ?, evidence_kind = ? "
                    "WHERE kcdx_id = ? AND valid_from = ?",
                    (lvv_id, a["verified_by"], a["verified_date"], ekn_id,
                     kid, vf_id))
                tx.commit()
            except Exception:
                tx.rollback()
                raise
            counts["reverified"] += 1
            continue

        # ABSENT -> an ADD. Distinguish add-entity (kcdx_id unknown to the DB)
        # from add-versions-row (kcdx_id present at a different valid_from).
        entity_rows = con.execute(
            "SELECT id, valid_from FROM address_versions WHERE kcdx_id = ?",
            (kid,)).fetchall()
        is_add_entity = len(entity_rows) == 0

        # Derive the curated row via the SHARED row-builder, kind-class-gated.
        module_id = _resolve_module_id(con, a["module"], where)
        kind_id = _db_dict_id(con, "address_versions", "kind", a["kind"], where)

        if a["kind"] in ss.FUNCTION_KINDS:
            # (a) PROMOTE. The baseline-present gate: the DEV DB must carry a bulk
            # row at this rva. No bulk row -> REFUSE (never mint a NULL-finger-
            # print function -- that would disguise a missing baseline as a real
            # entity; plan.md S3 baseline-present check). The user DB has no bulk
            # rows, so the gate reads the DEV connection passed in `state`.
            dev_con = state["dev_con"]
            base_row = _read_bulk_row(dev_con, a["rva"]) if a["rva"] is not None else None
            if base_row is None:
                raise BaselineRefusal(
                    f"{where}: no bulk baseline for version "
                    f"{a['valid_from_tag']} at rva "
                    f"{('0x%X' % a['rva']) if a['rva'] is not None else None}; "
                    f"run --rebuild for {a['valid_from_tag']} before adding "
                    f"function-kind entities")
            if user_projection:
                # USER INSERTs the projected curated row (built from the DEV bulk
                # base so the fingerprint carries identically into both DBs).
                av_id = _next_av_id(con)
                av_row = ss.build_curated_row(
                    av_id, kid, base_row=base_row, module_id=module_id,
                    rva=a["rva"], valid_from_id=vf_id, kind_id=kind_id,
                    signature=a["signature"], lvv_id=lvv_id,
                    verified_by=a["verified_by"], verified_date=a["verified_date"],
                    evidence_kind_id=ekn_id, offset=a["offset"],
                    vtable_slot=a["vtable_slot"], struct_offset=a["struct_offset"])
            else:
                # DEV does an IN-PLACE UPDATE of the bulk row (reuse its id; keep
                # fingerprint), mirroring the rebuild's promote-in-place.
                av_id = base_row["id"]
                av_row = ss.build_curated_row(
                    av_id, kid, base_row=base_row, module_id=module_id,
                    rva=a["rva"], valid_from_id=vf_id, kind_id=kind_id,
                    signature=a["signature"], lvv_id=lvv_id,
                    verified_by=a["verified_by"], verified_date=a["verified_date"],
                    evidence_kind_id=ekn_id, offset=a["offset"],
                    vtable_slot=a["vtable_slot"], struct_offset=a["struct_offset"])
        else:
            # (b)/(c) MINT, fingerprint NULL. base_row=None for both; rva is None
            # for vtable_index. No baseline gate (these never read a bulk row).
            av_id = _next_av_id(con)
            av_row = ss.build_curated_row(
                av_id, kid, base_row=None, module_id=module_id,
                rva=a["rva"], valid_from_id=vf_id, kind_id=kind_id,
                signature=a["signature"], lvv_id=lvv_id,
                verified_by=a["verified_by"], verified_date=a["verified_date"],
                evidence_kind_id=ekn_id, offset=a["offset"],
                vtable_slot=a["vtable_slot"], struct_offset=a["struct_offset"])

        # One per-action transaction (tx -- BEGIN/COMMIT immediate, SAVEPOINT/RELEASE
        # deferred). add-versions-row closes the prior open interval FIRST (so
        # ix_av_open_unique -- kcdx_id IS NOT NULL AND valid_through IS NULL -- is
        # never violated).
        tx.begin()
        try:
            if is_add_entity:
                name_row = state["names_by_id"].get(kid)
                if name_row is None:
                    raise RuntimeError(
                        f"{where}: add-entity has no address_names_seed row for "
                        f"kcdx_id={kid} (validation should have caught this)")
                _insert_names_row(con, name_row, user_projection)
            else:
                # add-versions-row: close the prior open interval. valid_through
                # := the prior-version id is the standard close; with a single
                # baseline version the prior open row's valid_from is the close
                # boundary (this exercises the interval-close machinery; the real
                # multi-version close lands when a 2nd game_versions row exists).
                prev_vf = max(r[1] for r in entity_rows)
                con.execute(
                    "UPDATE address_versions SET valid_through = ? "
                    "WHERE kcdx_id = ? AND valid_through IS NULL",
                    (prev_vf, kid))
            if user_projection:
                _projected_insert(con, av_row, user_projection=True)
            else:
                if a["kind"] in ss.FUNCTION_KINDS:
                    # DEV promote: UPDATE the bulk row in place (keep its id).
                    _promote_bulk_in_place(con, av_row)
                else:
                    _projected_insert(con, av_row, user_projection=False)
            # Survival row for the new curated entity (db-updator step 5.1), in
            # the SAME transaction + via the SAME shared builder the rebuild uses,
            # so apply's survival row is byte-identical to a rebuild's. function
            # kinds reuse the av row's fingerprint; the rest carry the seed datum
            # (empty until step 5.2). derives_from: map the seed kcdx_id -> the
            # dependency's curated av_id in this DB.
            derives_from_av_id = _resolve_derives_from_av_id(
                con, a["survival_derives_from_kid"])
            sv_row = ss.build_survival_row(
                av_row["id"], a["kind"],
                survival_aob=a["survival_aob"],
                anchor_string=a["survival_anchor_string"],
                rule=a["survival_rule"],
                slot_count=a["survival_slot_count"],
                expect_unique=a["survival_expect_unique"],
                derives_from_av_id=derives_from_av_id,
                content_hash=av_row.get("content_hash"),
                length=av_row.get("length"))
            _insert_survival_row(con, sv_row, user_projection)
            tx.commit()
        except Exception:
            tx.rollback()
            raise
        counts["added_entity" if is_add_entity else "added_versions_row"] += 1

    return counts


class BaselineRefusal(RuntimeError):
    """Raised when a function-kind add has no bulk baseline at its rva in the DEV
    DB. Caught in run_apply -> a clear refuse message + a distinct exit code."""


class VersionRefusal(RuntimeError):
    """Raised by the library applier apply_seeds when the linked DLL's resolved
    version can't be used: the .rdata resolver failed, or the DLL is a version the
    baseline + seeds don't cover. The CLI wrapper run_apply catches it and maps it
    to the historical APPLY_VERSION_REFUSE_EXIT message + exit code (so the CLI's
    observable behavior is unchanged); an in-process caller (db_editor) catches the
    typed exception directly. Carries the resolved tag (or None on resolve failure)
    for a precise in-process error message."""

    def __init__(self, message, *, tag=None):
        super().__init__(message)
        self.tag = tag


def _promote_bulk_in_place(con, av_row):
    """DEV-DB promote: UPDATE the existing bulk row (matched by its reused id) to
    the curated row, setting kcdx_id + the curated/audit/derived columns and
    KEEPING the fingerprint columns (length/content_hash/observed_arg_slots/
    caller_reg_arg_count/caller_arg_agreement/auto_name/decompile_quality). The
    fingerprint is already what build_curated_row copied from base_row, so writing
    the FULL row back (every DEV column) is equivalent and simplest -- it changes
    only the curated/audit/identity columns, leaving the fingerprint untouched."""
    cols = [c for c, _ in SCHEMA["address_versions"] if c != "id"]
    set_clause = ",".join(f'"{c}" = ?' for c in cols)
    con.execute(
        f'UPDATE address_versions SET {set_clause} WHERE id = ?',
        [av_row.get(c) for c in cols] + [av_row["id"]])


# ---------------------------------------------------------------------------
# Per-action transaction control -- the ONE seam the deferred-commit mode turns.
#
# WHY a control object instead of raw con.execute("BEGIN"/"COMMIT"/"ROLLBACK"):
# the apply path wraps EACH action in its own BEGIN; ...; COMMIT; (plan.md S6), so
# every action self-commits and the connection is closed before apply_seeds
# returns -- there is no seam to hold the transaction open across the maintainer's
# confirm. The deferred-commit mode (step 4a) needs the per-action writes to stay
# PENDING in ONE outer transaction per DB that the caller commits later, while
# keeping the default path BYTE-IDENTICAL for every desktop/CLI/test caller.
#
# A naive "skip the COMMITs in deferred mode" cannot work: a COMMIT ENDS the SQLite
# transaction, so letting the per-action COMMITs fire would close the very txn we
# need to hold; and skipping BEGIN/COMMIT entirely would let the driver auto-manage
# each statement, again ending the hold. The clean primitive is SAVEPOINT/RELEASE:
# a SAVEPOINT nests inside the outer txn and a RELEASE pops the marker WITHOUT
# ending the outer txn (verified: an outer BEGIN stays in_transaction across N
# SAVEPOINT/RELEASE pairs). So:
#   - IMMEDIATE (default): begin/commit/rollback emit BEGIN / COMMIT / ROLLBACK --
#     exactly the statements the code emitted before; the default path is unchanged.
#   - DEFERRED: begin/commit/rollback emit SAVEPOINT sN / RELEASE sN /
#     (ROLLBACK TO sN; RELEASE sN), all nested inside the single outer BEGIN
#     apply_seeds opened and never committed. A per-action error path rolls back
#     ONLY its savepoint (ROLLBACK TO + RELEASE), never the outer txn -- a bare
#     ROLLBACK would abort the WHOLE held txn, which is why deferred rollback is
#     savepoint-scoped.
# The object is payload-agnostic: it issues only transaction-control SQL, so the
# write logic in _apply_one_db is identical in both modes -- only WHEN/whether the
# work commits differs, never WHAT is written (the convergence the 4a test pins).
# ---------------------------------------------------------------------------
class _Tx:
    """Per-action transaction control for one DB connection, in one of two modes.
    IMMEDIATE (default) emits BEGIN/COMMIT/ROLLBACK -- byte-identical to the
    pre-4a code. DEFERRED emits SAVEPOINT/RELEASE/(ROLLBACK TO+RELEASE) inside an
    already-open outer transaction the caller commits later."""

    def __init__(self, con, deferred):
        self.con = con
        self.deferred = deferred
        # Monotonic savepoint counter: each action gets a UNIQUE name (actions never
        # nest -- a begin() is matched by its commit()/rollback() before the next
        # begin()), so a name is never live-duplicated.
        self._n = 0
        self._sp = None

    def begin(self):
        if self.deferred:
            self._n += 1
            self._sp = f"_kcdx_sp_{self._n}"
            self.con.execute(f"SAVEPOINT {self._sp}")
        else:
            self.con.execute("BEGIN")

    def commit(self):
        if self.deferred:
            # RELEASE pops the savepoint INTO the outer txn (does NOT end it).
            self.con.execute(f"RELEASE {self._sp}")
        else:
            self.con.execute("COMMIT")

    def rollback(self):
        if self.deferred:
            # Undo ONLY this action's savepoint; the outer txn stays open + holds
            # every prior released action. A bare ROLLBACK here would kill the
            # whole held txn -- never that in deferred mode.
            self.con.execute(f"ROLLBACK TO {self._sp}")
            self.con.execute(f"RELEASE {self._sp}")
        else:
            self.con.execute("ROLLBACK")


class DeferredCommit:
    """The handle a deferred-commit apply_seeds returns: the two OPEN, uncommitted
    DB connections (user + dev) plus the apply result dict. The caller (the
    maintainer-tool backend, step 4b) holds it across the user's confirm and calls
    commit(handle) on Confirm or rollback(handle) on Cancel -- 'on Cancel nothing
    lands' is then LITERAL: an uncommitted transaction is invisible to every other
    connection and is discarded whole on ROLLBACK, with no file copy and no live
    mutation before confirm (design S7; plan-spec 'Deferred commit is THE write
    mechanism').

    A deferred apply leaves an OUTER `BEGIN` open on each connection (the per-action
    writes are SAVEPOINT/RELEASE pairs nested inside it -- see _Tx). commit() COMMITs
    both outer txns + closes; rollback() ROLLBACKs both + closes. The handle is
    single-use: once finished (committed or rolled back) a second commit/rollback is
    a clear error (DeferredCommitError), never a crash or a partial state.

    `result` carries the same dict the immediate path returns (tag / ordinal /
    n_actions / counts) so a deferred caller has the identical post-apply summary;
    `out_dir` is the directory whose two DBs the held txns belong to."""

    def __init__(self, ucon, dcon, result, out_dir):
        self.ucon = ucon
        self.dcon = dcon
        self.result = result
        self.out_dir = out_dir
        self._finished = False   # set once committed OR rolled back (single-use)

    @property
    def finished(self):
        return self._finished


class DeferredCommitError(RuntimeError):
    """Raised on a misuse of a DeferredCommit handle: a commit/rollback on a handle
    already committed or rolled back (single-use). Distinct from a write/validation
    failure -- it is a CALLER protocol error (the held txn is already resolved), so
    the caller can tell 'I double-committed' apart from 'the COMMIT itself failed'."""


def commit(handle):
    """COMMIT the held deferred-commit transaction on BOTH DBs, then close both
    connections. Idempotent-safe: a commit on an already-finished handle raises
    DeferredCommitError (single-use), never a crash or a partial second write.

    TWO-DB COMMIT ORDERING -- the surfaced 'atomic guarantee's edge' (plan-spec
    'OPEN sub-decision'; step 4a). The user + dev DBs are two SEPARATE SQLite files,
    so a single OS-atomic two-file commit does not exist; if the first COMMIT
    succeeds and the second fails (disk-full, I/O error) the result is a SPLIT state
    -- one DB advanced, the other not. The implemented ordering is USER-DB-FIRST,
    DEV-DB-SECOND, chosen deliberately:
      - The USER DB (reference.sqlite) is the SHIPPED artifact -- the curated set
        that goes in every kcdx release and the maintainer-tool reads back. The DEV
        DB (reference-dev.sqlite) is the on-demand bulk superset; a DEV DB lagging
        the USER DB by one edit is the more-recoverable split (re-run the same edit
        in deferred mode -- the USER row already matches, so apply diffs only the DEV
        side; the convergence/idempotency the applier already has makes the re-apply
        safe).
      - The inverse (dev-first) would leave the SHIPPED DB stale while the bulk DB
        advanced -- the worse split, since the shipped artifact is the one a release
        and the tool surface depend on.
    On a second-COMMIT failure the FIRST DB is ALREADY committed (SQLite gives no
    cross-file rollback) -- this function re-raises the underlying error AFTER
    marking the handle finished + closing both connections, so the caller (step 4b)
    surfaces the split to the operator. It does NOT silently swallow it and does NOT
    pretend atomicity across two files. (A future single-file or attached-DB layout
    would close this edge; today's two-file layout cannot, by construction.)"""
    if handle.finished:
        raise DeferredCommitError(
            "commit() on an already-finished DeferredCommit handle (it was already "
            "committed or rolled back; a handle is single-use)")
    handle._finished = True
    try:
        # USER first (the shipped DB), DEV second -- see the ordering rationale above.
        handle.ucon.execute("COMMIT")
        handle.dcon.execute("COMMIT")
    finally:
        # Always close BOTH, even if the second COMMIT raised (the first is already
        # durable; leaving a connection open would leak a file handle + a lock).
        _close_quiet(handle.ucon)
        _close_quiet(handle.dcon)


def rollback(handle):
    """ROLLBACK the held deferred-commit transaction on BOTH DBs, then close both
    connections -- the literal 'on Cancel nothing lands'. Idempotent-safe: a
    rollback on an already-finished handle raises DeferredCommitError (single-use).
    Order does not matter for rollback (neither DB has committed; both held txns are
    discarded), so both are rolled back unconditionally and both are closed."""
    if handle.finished:
        raise DeferredCommitError(
            "rollback() on an already-finished DeferredCommit handle (it was already "
            "committed or rolled back; a handle is single-use)")
    handle._finished = True
    try:
        # Both txns are uncommitted; discard each. ROLLBACK on a connection with no
        # open txn is harmless, so a partially-applied handle still rolls back clean.
        _rollback_quiet(handle.ucon)
        _rollback_quiet(handle.dcon)
    finally:
        _close_quiet(handle.ucon)
        _close_quiet(handle.dcon)


def _rollback_quiet(con):
    """ROLLBACK a connection's open transaction; swallow the 'no transaction is
    active' case (a handle whose outer BEGIN never opened on this DB) -- logged via
    the swallow being the documented safe case, not an error to surface."""
    try:
        con.execute("ROLLBACK")
    except sqlite3.OperationalError:
        # No active transaction to roll back -- the safe, expected case when the
        # outer BEGIN never opened on this connection. Nothing to undo.
        pass


def _close_quiet(con):
    """Close a connection, swallowing a double-close (the connection is already
    closed) -- closing is idempotent from the caller's view."""
    try:
        con.close()
    except sqlite3.ProgrammingError:
        # Already closed -- idempotent close, the safe expected case.
        pass


def apply_seeds(out_dir, dll_path, *, version=None, log=None, defer_commit=False):
    """APPLY mode, LIBRARY entry: the incremental seed->DB applier as a callable
    that takes parameters and RETURNS a result -- no sys.exit, no CLI parsing, and
    no print-as-sole-output-channel. The CLI wrapper run_apply (below) and the
    in-process db_editor both invoke this; the apply==rebuild oracle is unchanged
    because the body is the same classify->validate->apply sequence run_apply ran,
    only the OUTPUT CHANNEL (exceptions instead of sys.exit, a returned dict
    instead of a printed summary) moved out.

    Implements re-verify (audit-trio UPDATE) plus the two add actions: add-entity
    (a new names row + a new versions row) and add-versions-row (close the prior
    open interval + insert).

    Spine (plan.md S3 'Running an incremental build'):
      1. resolve target version from the linked DLL (.rdata resolver), OR take the
         caller-supplied pre-resolved version (see `version` below)
      2. validate the FULL seed CSV state (abort with no DB write on failure)
      3. open BOTH DBs read-write (refuse if a baseline is missing)
      4/5/6. classify the seed-vs-DB delta + apply it per DB (user then dev),
             each ACTION in its own BEGIN/COMMIT; function-kind adds first pass
             the baseline-present gate (no bulk row -> REFUSE, no write either DB)

    Parameters:
      out_dir  -- the directory holding the two reference DBs (reference.sqlite +
                  reference-dev.sqlite) the apply amends in place.
      dll_path -- the linked WHGame.dll the .rdata version resolver reads (the
                  apply path's version source). Supply this OR `version`, never
                  both and never neither (a ValueError otherwise).
      version  -- a pre-resolved (tag, ordinal); when given, the DLL is not read --
                  the caller already resolved the version, e.g. the web backend per
                  data/maintainer-tool/design.md D15 (no DLL server-side). Supply
                  EXACTLY ONE of dll_path / version. The (tag, ordinal) is trusted
                  exactly as resolve_version would have returned it; the
                  tag != GAME_VERSION_TAG refusal gate still fires on the supplied
                  tag.
      log      -- an optional callable(str) for progress lines; None suppresses
                  them (the in-process caller wants no prints, the CLI passes
                  print). The RESULT is returned, never only printed.
      defer_commit -- DEFAULT False = the historical path: each action self-commits
                  in its own BEGIN/COMMIT, both DBs close, a result DICT is returned
                  (byte-identical to before -- every landed oracle exercises this).
                  When True (step 4a -- THE maintainer-tool write mechanism): the
                  per-DB writes run under ONE outer transaction per DB (the
                  per-action commits become SAVEPOINT/RELEASE pairs nested inside
                  it, never committed), the connections are NOT closed, and a
                  DeferredCommit HANDLE is returned carrying the two OPEN,
                  uncommitted connections + the result dict. The caller commits the
                  held txns later via commit(handle) / discards via rollback(handle).
                  Validation + the version/baseline refusals still gate BEFORE any
                  DB open, exactly as in the default path.

    Returns:
      defer_commit=False -- a dict:
        {"tag", "ordinal", "n_actions", "counts": {"user": <c>, "dev": <c>}}
      where each <c> is the per-DB counts dict _apply_one_db returns.
      defer_commit=True  -- a DeferredCommit handle whose `.result` is that SAME
        dict and whose `.ucon`/`.dcon` are the two open uncommitted connections.

    Raises (no sys.exit -- the caller maps these to its own exit/UI behaviour):
      ValueError          -- neither dll_path nor version was supplied, or BOTH were
                             (a caller programming error -- never a silent pick).
      VersionResolveError -- the DLL's .rdata version could not be resolved.
      VersionRefusal      -- the DLL is a version the baseline + seeds don't cover.
      BaselineRefusal     -- a function-kind add has no bulk baseline (no DB write).
      RuntimeError        -- a seed-state validation failure (no DB open/write).
    On a refusal/validation/baseline/apply error in DEFERRED mode, both connections
    are rolled back + closed before the error propagates -- a deferred failure leaves
    NO open txn and NO partial write, the same no-write guarantee as the default path.
    """
    def _emit(msg):
        if log is not None:
            log(msg)

    # 0. EXACTLY ONE of dll_path / version. A caller passing both is a programming
    #    error (which version is authoritative?) -- refuse loudly rather than
    #    silently prefer one; a caller passing neither has no version source at all.
    if (dll_path is None) == (version is None):
        raise ValueError(
            "apply_seeds needs EXACTLY ONE of dll_path or version: supply a DLL to "
            "resolve the version from, OR a pre-resolved (tag, ordinal) -- never "
            "both and never neither.")

    # 1. Determine the target version. Either the caller pre-resolved it (the web
    #    backend per D15 -- no DLL server-side) and passed `version`, or we resolve
    #    it from the linked DLL's .rdata interns (the apply path's primary version
    #    source, NOT whdlversions.json). A resolve failure PROPAGATES
    #    (VersionResolveError) -- the caller decides how to surface it; nothing is
    #    opened or written.
    if version is not None:
        tag, ordinal = version
        _emit(f"  target version (pre-resolved by caller): tag={tag} ordinal={ordinal}")
    else:
        tag, ordinal = resolve_version(dll_path)
        _emit(f"  target version (from DLL .rdata): tag={tag} ordinal={ordinal}")
    if tag != GAME_VERSION_TAG:
        # The baseline + seeds only know GAME_VERSION_TAG today; a different
        # linked DLL means the DB has no baseline for it. Refuse clearly.
        raise VersionRefusal(
            f"linked DLL is version {tag!r} but the baseline + seeds only cover "
            f"{GAME_VERSION_TAG!r}; run --rebuild for {tag!r} first.", tag=tag)

    # 2. Validate the FULL seed CSV state. Any failure raises (RuntimeError) before
    #    any DB open or write -- nothing written on a validation abort.
    state = _validate_full_seed_state()
    actions = _seed_action_rows(state)
    _emit(f"  seed validated; {len(actions)} versions-seed row(s) to diff")

    # 3. Open BOTH DBs read-write (refuse if a baseline is missing).
    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    if defer_commit:
        # DEFERRED MODE -- run validate (above, already done) -> the per-DB writes
        # under ONE outer txn per DB -> RETURN the open uncommitted connections.
        # The per-action commits become SAVEPOINT/RELEASE pairs nested inside each
        # outer BEGIN (see _Tx); the outer BEGIN is opened here and NEVER committed
        # by apply_seeds -- commit(handle)/rollback(handle) resolve it later. On ANY
        # error here, both txns are rolled back + both connections closed before the
        # error propagates, so a deferred failure leaves NO open txn and NO write --
        # the same no-write guarantee the default path gives.
        ucon = _open_rw(user_db, "user (reference.sqlite)")
        try:
            dcon = _open_rw(dev_db, "dev (reference-dev.sqlite)")
        except BaseException:
            _close_quiet(ucon)
            raise
        try:
            state["dev_con"] = dcon
            # Open the held outer transaction on EACH DB. Every per-action write
            # below nests as a SAVEPOINT inside these; nothing commits until the
            # caller's commit(handle).
            ucon.execute("BEGIN")
            dcon.execute("BEGIN")
            # 4/5/6. Apply user first, then dev (plan.md S6 user->dev order), each
            # action a SAVEPOINT/RELEASE inside its outer txn (deferred=True).
            u = _apply_one_db(ucon, actions, state, "user",
                              user_projection=True, deferred=True)
            d = _apply_one_db(dcon, actions, state, "dev",
                              user_projection=False, deferred=True)
        except BaseException:
            # No commit happened; discard both held txns + close. _rollback_quiet
            # tolerates a connection whose BEGIN never opened (e.g. the dcon BEGIN
            # raised). The original error propagates.
            _rollback_quiet(ucon)
            _rollback_quiet(dcon)
            _close_quiet(ucon)
            _close_quiet(dcon)
            raise
        result = {"tag": tag, "ordinal": ordinal, "n_actions": len(actions),
                  "counts": {"user": u, "dev": d}}
        # Connections stay OPEN + uncommitted; the handle carries them to the caller.
        return DeferredCommit(ucon, dcon, result, out_dir)

    # IMMEDIATE MODE (default) -- byte-identical to the pre-4a path: each action
    # self-commits in its own BEGIN/COMMIT (deferred=False), both DBs close before
    # return, a result dict is returned. Untouched so every landed oracle stays green.
    ucon = _open_rw(user_db, "user (reference.sqlite)")
    try:
        dcon = _open_rw(dev_db, "dev (reference-dev.sqlite)")
        # The function-kind PROMOTE reads the bulk fingerprint from the DEV DB
        # for BOTH passes (the user DB has no bulk rows). Stash the dev connection
        # so the user pass can read it for the baseline-present gate; the gate
        # fires BEFORE the user pass writes, so a missing baseline refuses with
        # neither DB touched. BaselineRefusal PROPAGATES (the user pass raises it
        # before writing; nothing committed in either DB).
        state["dev_con"] = dcon
        try:
            # 4/5/6. Apply user first, then dev (plan.md S6 user->dev order).
            u = _apply_one_db(ucon, actions, state, "user", user_projection=True)
            d = _apply_one_db(dcon, actions, state, "dev", user_projection=False)
        finally:
            dcon.close()
    finally:
        ucon.close()

    return {"tag": tag, "ordinal": ordinal, "n_actions": len(actions),
            "counts": {"user": u, "dev": d}}


# ---------------------------------------------------------------------------
# DIRECT-WRITE mode (the maintainer-tool write mechanism; design D19/D20).
#
# WHY this exists vs apply_seeds: design D1 makes the DB the ORIGINATOR -- a
# maintainer edit is a DIRECT INSERT/UPDATE on the DB, NOT a seed rebuild. A
# ground-truth probe established that _apply_one_db ALREADY runs the real
# INSERT/UPDATE statements; apply_seeds is the SEED-CSV-rebuild WRAPPER around them
# (it re-opens the DBs, re-runs the GAME_VERSION_TAG/VersionRefusal gate -- which
# materialises ZERO rows for a NEW game tag -- and applies the diff). The direct
# drive below KEEPS the write helpers + the whole-state validator and DROPS the
# wrapper: it opens the deferred-commit txn ITSELF and calls _apply_one_db DIRECTLY
# on the held connections. PROVEN convergent (a direct _apply_one_db call over the
# SAME validated state produces a DB byte-identical to apply_seeds(defer_commit) +
# commit), so the rework preserves the mechanism; what it unlocks is create-version
# AT A NEW game tag (the wrapper's GAME_VERSION_TAG gate forbade it).
#
# PROSPECTIVE-DB-STATE VALIDATION (design D19): the validation re-targets to the
# prospective DB STATE (the DB as it would be AFTER the edit). The prospective state
# is materialised as the prospective SEED -- export(committed DB) + the edit folded
# in -- which the round-trip contract (import(export(DB))==DB, design D2/D4) makes a
# FAITHFUL serialisation of the prospective DB rows: validating the prospective seed
# IS validating the prospective DB state. This reuses the SINGLE whole-state
# validator gate UNCHANGED (_validate_full_seed_state -- the same tuple-uniqueness /
# audit-trio / supersession-acyclicity / FK-closure / enum invariants), so the gate
# sees the SAME invariants it always has, DB-sourced -- never a reimplemented
# row-level check (the validator has no row-level entry point; D13/D19). A failure
# aborts BEFORE any DB open -- the DB is byte-identical.
# ---------------------------------------------------------------------------
def _pointed_at_seed(prospective_seed_dir):
    """Repoint the importer's three seed-path module constants at the prospective
    seed dir for the duration of a validate/state-build, restoring them after. The
    same global-constant convention the rebuild oracles + the prior bridge used;
    needed because _validate_full_seed_state reads MODULE_SEED_CSV/etc. by name. A
    context manager so the restore runs on every path (success or raise)."""
    import contextlib

    @contextlib.contextmanager
    def _cm():
        saved = (MODULE_SEED_CSV, ADDRESS_NAMES_SEED_CSV, ADDRESS_VERSIONS_SEED_CSV)
        globals()["MODULE_SEED_CSV"], globals()["ADDRESS_NAMES_SEED_CSV"], \
            globals()["ADDRESS_VERSIONS_SEED_CSV"] = _seed_paths_in(
                prospective_seed_dir)
        try:
            yield
        finally:
            (globals()["MODULE_SEED_CSV"], globals()["ADDRESS_NAMES_SEED_CSV"],
             globals()["ADDRESS_VERSIONS_SEED_CSV"]) = saved
    return _cm()


def _seed_paths_in(seed_dir):
    return (os.path.join(seed_dir, "module_seed.csv"),
            os.path.join(seed_dir, "address_names_seed.csv"),
            os.path.join(seed_dir, "address_versions_seed.csv"))


def _validate_prospective_db_state(prospective_seed_dir):
    """Run the SINGLE whole-state validator gate against the prospective DB state,
    materialised as the prospective seed (export(DB)+edit). Returns the resolved
    `state` (the same dict _validate_full_seed_state returns -- names_by_id, the
    module lookups, the validated versions seed). Raises the validator's RuntimeError
    (or a typed reader error) on any invariant violation -- the caller aborts with NO
    DB write. Reuses _validate_full_seed_state UNCHANGED (the SAME invariants the
    bridge + the rebuild run), only fed the prospective seed instead of data/seeds/.
    """
    with _pointed_at_seed(prospective_seed_dir):
        return _validate_full_seed_state()


def _new_tag_ordinal(new_tag, version):
    """The ordinal for a NEW game tag's game_versions row. The maintainer-tool
    resolves the new version client-side (design D15) and passes version=(new_tag,
    new_ordinal) -- use that ordinal directly when version's TAG matches the new tag.
    When it does NOT match (a dll_path caller resolves the DLL's OWN -- older --
    version, which can never be the new tag), derive the ordinal from the new tag's
    last dotted segment: tag == '<major>.<minor>.<build>' and ordinal == build (the
    DOCUMENTED tag<->ordinal relationship -- read_game_version builds the tag exactly
    so, GAME_VERSION_TAG '1.5.1164953' <-> GAME_VERSION_ORDINAL 1164953). This is the
    deterministic tag->ordinal map, NOT a guess. A new tag whose last segment is not an
    integer is a malformed tag -- refuse loudly rather than fabricate an ordinal."""
    vtag, vordinal = version
    if vtag == new_tag:
        return vordinal
    last = str(new_tag).rsplit(".", 1)[-1]
    try:
        return int(last)
    except ValueError:
        raise RuntimeError(
            f"create-version-at-new-tag: cannot derive an ordinal from new tag "
            f"{new_tag!r} (its last dotted segment {last!r} is not an integer build "
            f"number); the tag<->ordinal map needs '<branch>.<build>' form, or pass "
            f"version=(tag, ordinal) with the resolved ordinal")


def _insert_game_version(con, tag, ordinal):
    """INSERT the new game_versions row (tag, ordinal) the create-version-at-a-NEW-tag
    path needs, returning its assigned id. The bridge could NEVER do this -- its
    GAME_VERSION_TAG/VersionRefusal gate refused any tag the DB had no baseline for,
    so a new-tag create-version materialised ZERO rows. The direct path INSERTs the
    tag (so it now EXISTS) before closing the prior interval + inserting the new
    address_versions row. `released` is NULL (the maintainer authors the tag+ordinal;
    the release date is not part of the curated edit). Runs in BOTH DBs (the tag is a
    shared dimension; user + dev both carry the game_versions table)."""
    con.execute(
        "INSERT INTO game_versions (tag, ordinal, released) VALUES (?, ?, NULL)",
        (tag, ordinal))
    row = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                      (tag,)).fetchone()
    return row[0]


def _apply_new_tag_version(con, action, state, which, user_projection, tx,
                           new_tag, new_ordinal):
    """Apply ONE create-version-at-a-NEW-game-tag to one open DB, under `tx` (the
    deferred-commit savepoint seam). The action is the new address_versions row's
    facts (the same shape _seed_action_rows emits, but for a tag _seed_action_rows
    FILTERS OUT because it is not GAME_VERSION_TAG). Steps, all reusing _apply_one_db's
    OWN write helpers so the row shape is byte-identical to any other add:
      1. INSERT the new game_versions row (the tag now EXISTS in this DB).
      2. resolve the FK ids (module, kind, evidence_kind, valid_from=the new gv id,
         last_verified) via the SAME _db_* helpers -- look up, never mint.
      3. function-kind PROMOTE-vs-mint + BaselineRefusal (the SAME gate add uses):
         a function kind needs a DEV bulk baseline at its rva.
      4. close the entity's prior OPEN interval (valid_through := the prior version's
         id) BEFORE the INSERT (the ix_av_open_unique partial-unique constraint).
      5. _projected_insert (USER) / _promote_bulk_in_place|_projected_insert (DEV) the
         new av row + _insert_survival_row the 1:1 survival sibling -- the SAME helpers
         every other add uses, so the 8 load-bearing behaviors all land identically.
    This is the ONE genuinely-new write the bridge never did (the game_versions
    INSERT). Everything after step 1 is _apply_one_db's add-versions-row tail, reused
    verbatim against the freshly-inserted tag."""
    kid = action["kcdx_id"]
    where = (f"{which} DB (kcdx_id={kid}, valid_from={new_tag!r}; new-tag create)")

    tx.begin()
    try:
        # 1. The new tag now EXISTS in this DB.
        vf_id = _insert_game_version(con, new_tag, new_ordinal)

        # 2. FK ids -- look up the EXISTING dict/module ids, never mint (behavior 5).
        #    last_verified resolves against the NOW-present tag set (the new tag was
        #    just inserted, so a row verified AT the new tag resolves too).
        lvv_id = _db_tag_to_id(con, action["lvv_tag"], where) if action["lvv_tag"] else None
        ekn_id = _db_evidence_kind_id(con, action["evidence_kind"], where)
        module_id = _resolve_module_id(con, action["module"], where)
        kind_id = _db_dict_id(con, "address_versions", "kind", action["kind"], where)

        # 3. PROMOTE-vs-mint + BaselineRefusal (behavior 3) -- the SAME kind-class gate
        #    _apply_one_db's add path runs.
        if action["kind"] in ss.FUNCTION_KINDS:
            dev_con = state["dev_con"]
            base_row = (_read_bulk_row(dev_con, action["rva"])
                        if action["rva"] is not None else None)
            if base_row is None:
                raise BaselineRefusal(
                    f"{where}: no bulk baseline at rva "
                    f"{('0x%X' % action['rva']) if action['rva'] is not None else None}; "
                    f"run --rebuild before adding function-kind entities")
            av_id = _next_av_id(con) if user_projection else base_row["id"]
            av_row = ss.build_curated_row(
                av_id, kid, base_row=base_row, module_id=module_id,
                rva=action["rva"], valid_from_id=vf_id, kind_id=kind_id,
                signature=action["signature"], lvv_id=lvv_id,
                verified_by=action["verified_by"], verified_date=action["verified_date"],
                evidence_kind_id=ekn_id, offset=action["offset"],
                vtable_slot=action["vtable_slot"], struct_offset=action["struct_offset"])
        else:
            av_id = _next_av_id(con)
            av_row = ss.build_curated_row(
                av_id, kid, base_row=None, module_id=module_id,
                rva=action["rva"], valid_from_id=vf_id, kind_id=kind_id,
                signature=action["signature"], lvv_id=lvv_id,
                verified_by=action["verified_by"], verified_date=action["verified_date"],
                evidence_kind_id=ekn_id, offset=action["offset"],
                vtable_slot=action["vtable_slot"], struct_offset=action["struct_offset"])

        # 4. Close the prior OPEN interval BEFORE the INSERT (behavior 2). A
        #    create-version is always for an EXISTING entity (the caller checked it),
        #    so there is a prior open row to close. valid_through := the prior
        #    version's id (the close boundary), keeping ix_av_open_unique satisfied.
        prev_rows = con.execute(
            "SELECT valid_from FROM address_versions WHERE kcdx_id = ? "
            "AND valid_through IS NULL", (kid,)).fetchall()
        prev_vf = max(r[0] for r in prev_rows)
        con.execute(
            "UPDATE address_versions SET valid_through = ? "
            "WHERE kcdx_id = ? AND valid_through IS NULL", (prev_vf, kid))

        # 5. INSERT the new av row (behavior 4: per-DB projection) + the 1:1 survival
        #    sibling (behavior 1), via the SAME helpers every add uses.
        if user_projection:
            _projected_insert(con, av_row, user_projection=True)
        elif action["kind"] in ss.FUNCTION_KINDS:
            _promote_bulk_in_place(con, av_row)
        else:
            _projected_insert(con, av_row, user_projection=False)
        derives_from_av_id = _resolve_derives_from_av_id(
            con, action["survival_derives_from_kid"])
        sv_row = ss.build_survival_row(
            av_row["id"], action["kind"],
            survival_aob=action["survival_aob"],
            anchor_string=action["survival_anchor_string"],
            rule=action["survival_rule"],
            slot_count=action["survival_slot_count"],
            expect_unique=action["survival_expect_unique"],
            derives_from_av_id=derives_from_av_id,
            content_hash=av_row.get("content_hash"),
            length=av_row.get("length"))
        _insert_survival_row(con, sv_row, user_projection)
        tx.commit()
    except Exception:
        tx.rollback()
        raise


def _new_tag_action_from_seed(prospective_seed_dir, state, kcdx_id, new_tag):
    """Build the single add-versions-row action for the create-version-at-a-NEW-tag
    path. _seed_action_rows FILTERS to GAME_VERSION_TAG, so it never emits the new-tag
    row; this reads the SAME prospective versions seed and builds the one action for
    the (kcdx_id, new_tag) row using the SAME column derivation _seed_action_rows uses
    (ss.authored_kind + parse_int over the authored cells), so the resulting row is
    identical in shape to any other add action -- only the valid_from tag differs."""
    versions_seed = read_address_versions_seed(
        os.path.join(prospective_seed_dir, "address_versions_seed.csv"))
    for vs in versions_seed:
        if (int(vs["kcdx_id"]) == int(kcdx_id)
                and vs["valid_from_version"].strip() == new_tag):
            srva = (vs.get("rva") or "").strip()
            sdf = (vs.get("survival_derives_from") or "").strip()
            return {
                "kcdx_id": int(kcdx_id),
                "module": vs["module"].strip(),
                "valid_from_tag": new_tag,
                "rva": parse_int(srva) if srva else None,
                "kind": ss.authored_kind(vs),
                "signature": (vs.get("signature") or "").strip(),
                "offset": parse_int(vs.get("offset") or ""),
                "vtable_slot": parse_int(vs.get("vtable_slot") or ""),
                "struct_offset": parse_int(vs.get("struct_offset") or ""),
                "lvv_tag": (vs.get("last_verified_at_version") or "").strip(),
                "verified_by": (vs.get("verified_by") or "").strip() or None,
                "verified_date": (vs.get("verified_date") or "").strip() or None,
                "evidence_kind": (vs.get("evidence_kind") or "").strip() or None,
                "survival_aob": (vs.get("survival_aob") or "").strip() or None,
                "survival_anchor_string": (vs.get("survival_anchor_string") or "").strip() or None,
                "survival_rule": (vs.get("survival_rule") or "").strip() or None,
                "survival_slot_count": parse_int(vs.get("survival_slot_count") or ""),
                "survival_expect_unique": (
                    int((vs.get("survival_expect_unique") or "").strip())
                    if (vs.get("survival_expect_unique") or "").strip() else None),
                "survival_derives_from_kid": int(sdf) if sdf else None,
            }
    raise RuntimeError(
        f"create-version-at-new-tag: no prospective seed row for "
        f"(kcdx_id={kcdx_id}, valid_from_version={new_tag!r}) -- the append did not "
        f"land (an internal db_editor error, not a maintainer edit error)")


def apply_direct_edit(out_dir, prospective_seed_dir, *, version, log=None,
                      defer_commit=False, new_tag=None, new_tag_kcdx_id=None):
    """DIRECT-WRITE drive (design D19): validate the prospective DB state, then write
    the edit DIRECTLY to BOTH DBs via _apply_one_db's write helpers -- NOT through
    apply_seeds' seed-rebuild wrapper. THE maintainer-tool incremental write path; the
    six db_editor write functions drive it.

    Spine:
      1. validate the PROSPECTIVE DB STATE (the prospective seed = export(DB)+edit, a
         faithful serialisation of the post-edit DB rows -- _validate_prospective_db_state
         reuses the single whole-state validator UNCHANGED). A failure raises BEFORE
         any DB open -- NO write, the DB byte-identical.
      2. open BOTH DBs, BEGIN the held outer txn on each (the 4a deferred seam).
      3. apply the CURRENT-tag actions via _apply_one_db DIRECTLY on the held
         connections (user then dev) -- the reuse the convergence probe proved.
      4. if new_tag is set, apply the create-version-at-a-NEW-tag action (the new
         game_versions INSERT + interval-close + new av row) via _apply_new_tag_version
         -- the write the bridge could never do.
      5. return a DeferredCommit handle (defer_commit=True) OR commit+close +return the
         result dict (defer_commit=False). On ANY error both txns roll back + close --
         NO partial write, NO open txn (the same no-write guarantee as apply_seeds).

    Parameters:
      out_dir              -- the directory holding reference.sqlite + reference-dev.sqlite.
      prospective_seed_dir -- the dir holding the prospective seed (export(DB)+edit) the
                              db_editor write function built; the validation + the
                              new-tag action read it. NOTHING under data/seeds/.
      version              -- the (tag, ordinal) the edit targets, ALWAYS pre-resolved
                              (the maintainer-tool resolves it client-side, design D15;
                              no DLL server-side). For a current-tag edit tag ==
                              GAME_VERSION_TAG; for a new-tag create-version it is the
                              NEW (tag, ordinal) and new_tag must equal tag.
      defer_commit         -- True (default for the maintainer-tool): return the
                              DeferredCommit handle the caller commits/rolls back on
                              confirm/cancel. False: commit+close immediately, return
                              the result dict (used by the convergence oracle).
      new_tag/new_tag_kcdx_id -- set together for create-version-at-a-NEW-tag: the new
                              game tag + the entity it adds a version for. None for every
                              current-tag job.

    Returns: a DeferredCommit handle (defer_commit=True) or a result dict
    {"tag","ordinal","n_actions","counts"} (defer_commit=False).

    Raises: RuntimeError (validator), BaselineRefusal (function-kind add, no bulk
    baseline), or a typed reader error -- each BEFORE or WITHOUT a committed write.
    """
    def _emit(msg):
        if log is not None:
            log(msg)

    tag, ordinal = version

    # 1. Validate the PROSPECTIVE DB STATE (reuses the single whole-state validator).
    #    For a NEW-tag create-version the prospective seed carries a row at new_tag;
    #    the whole-state validator format-checks + tuple-uniqueness-checks it and
    #    FK-checks its kcdx_id, while the GAME_VERSION_TAG-scoped coverage check
    #    ignores it (a new tag does not change the baseline-coverage invariant). A
    #    failure raises here -- NO DB open below.
    state = _validate_prospective_db_state(prospective_seed_dir)
    actions = _seed_action_rows(state)   # current-(GAME_VERSION_)tag actions only
    _emit(f"  prospective DB state validated; {len(actions)} current-tag action(s)"
          + (f" + 1 new-tag create at {new_tag}" if new_tag else ""))

    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    # 2/3/4. Open both DBs, BEGIN the held outer txn on each, write DIRECTLY via the
    #        _apply_one_db helpers. On ANY error roll back + close both (no partial
    #        write, no open txn) -- the same guarantee apply_seeds' deferred path gives.
    ucon = _open_rw(user_db, "user (reference.sqlite)")
    try:
        dcon = _open_rw(dev_db, "dev (reference-dev.sqlite)")
    except BaseException:
        _close_quiet(ucon)
        raise
    try:
        state["dev_con"] = dcon
        ucon.execute("BEGIN")
        dcon.execute("BEGIN")
        # 3. Current-tag actions -- the direct _apply_one_db call (convergence proven).
        u = _apply_one_db(ucon, actions, state, "user",
                          user_projection=True, deferred=True)
        d = _apply_one_db(dcon, actions, state, "dev",
                          user_projection=False, deferred=True)
        # 4. The create-version-at-a-NEW-tag write (the game_versions INSERT the
        #    bridge could never do). One action per DB, the SAME write helpers.
        if new_tag is not None:
            new_ordinal = _new_tag_ordinal(new_tag, version)
            na = _new_tag_action_from_seed(
                prospective_seed_dir, state, new_tag_kcdx_id, new_tag)
            _apply_new_tag_version(ucon, na, state, "user", True,
                                   _Tx(ucon, True), new_tag, new_ordinal)
            _apply_new_tag_version(dcon, na, state, "dev", False,
                                   _Tx(dcon, True), new_tag, new_ordinal)
            u["added_versions_row"] += 1
            d["added_versions_row"] += 1
    except BaseException:
        _rollback_quiet(ucon)
        _rollback_quiet(dcon)
        _close_quiet(ucon)
        _close_quiet(dcon)
        raise

    result = {"tag": tag, "ordinal": ordinal,
              "n_actions": len(actions) + (1 if new_tag else 0),
              "counts": {"user": u, "dev": d}}

    if defer_commit:
        # Held, uncommitted -- the handle carries the open connections to the caller
        # (commit(handle)/rollback(handle) resolve the txn; the ROLLBACK discards
        # sqlite_sequence/PK-autoincrement bumps too -- the robust post-failure
        # rollback, design D19).
        return DeferredCommit(ucon, dcon, result, out_dir)

    # Immediate: commit USER-first then DEV (the 4a ordering) + close. Used by the
    # convergence oracle (direct == seed-rebuild) and any non-deferred caller.
    try:
        ucon.execute("COMMIT")
        dcon.execute("COMMIT")
    finally:
        _close_quiet(ucon)
        _close_quiet(dcon)
    return result


def validate_direct_edit(prospective_seed_dir, *, version, log=None):
    """DRY-VALIDATE the prospective DB state for the direct-write path and STOP before
    any DB open -- the Save-PREVIEW seam (maintainer-tool step 4b-rework re-points the
    preview here). Runs the SAME single whole-state validator apply_direct_edit runs at
    its step 1, against the prospective DB state (the prospective seed = export(DB)+edit),
    and returns the validated {"tag","ordinal"} WITHOUT opening or writing any DB -- the
    DB is byte-identical. A validation failure raises the validator's RuntimeError (or a
    typed reader error), exactly as the write path would, with NO write.

    `version` is the pre-resolved (tag, ordinal) the edit targets (current OR new tag --
    the validate path does not write the game_versions row, so a new tag validates the
    same way: the whole-state validator format/uniqueness/FK-checks the new-tag row and
    the baseline-coverage check ignores it)."""
    def _emit(msg):
        if log is not None:
            log(msg)

    tag, ordinal = version
    _validate_prospective_db_state(prospective_seed_dir)
    _emit("  prospective DB state validated (no DB write -- Save preview)")
    return {"tag": tag, "ordinal": ordinal}


def validate_prospective_seeds(out_dir, dll_path=None, *, version=None, log=None):
    """DRY-VALIDATE the prospective seed state — run apply_seeds' validation gate
    and STOP before any DB open or write. The Save-PREVIEW seam (maintainer-tool
    step 4b): the maintainer-tool's Save shows the field-delta + the validator's
    verdict for a prospective edit WITHOUT touching the DB; the write happens only
    on Confirm (step 5). This runs the SAME gate apply_seeds runs at its step 2
    (`_validate_full_seed_state` + the version-tag refusal) so a Save's verdict is
    the data-core's single validator (design D13/law 6), never a backend re-impl.

    WHY a separate entry, not apply_seeds(defer_commit=...): every apply_seeds path
    OPENS both DBs and writes (immediate or held). A Save must leave the DB
    BYTE-IDENTICAL — no open connection, no held txn, nothing to commit/rollback.
    So this entry runs steps 0–2 of apply_seeds' spine and RETURNS at the boundary,
    before step 3 (the DB open). The seed-path module constants name the prospective
    seed (the db_editor.validate_* callers repoint them exactly as the write path
    does); this entry reads them through `_validate_full_seed_state` — identical to
    the write path's gate, so the verdict is the same.

    Parameters mirror apply_seeds' version contract: supply EXACTLY ONE of dll_path
    / version (the maintainer-tool always passes `version=(tag, ordinal)`, no DLL
    server-side — D15). `log` is an optional progress callable.

    Returns {"tag", "ordinal"} — the resolved version the Save validated against —
    on success. Raises the SAME typed errors apply_seeds raises before its DB open,
    and like that path leaves NO DB write (here there is no DB open at all):
      ValueError          -- neither dll_path nor version, or both.
      VersionResolveError -- a DLL .rdata version could not be resolved.
      VersionRefusal      -- the version is one the baseline + seeds don't cover.
      RuntimeError        -- a seed-state validation failure (the shared validator's
                             verdict — a duplicate tuple, partial trio, supersession
                             cycle, missing required column, …).
    """
    def _emit(msg):
        if log is not None:
            log(msg)

    # 0. EXACTLY ONE of dll_path / version — the same caller contract apply_seeds
    #    enforces; a Save with neither/both is a backend bug, surfaced loudly.
    if (dll_path is None) == (version is None):
        raise ValueError(
            "validate_prospective_seeds needs EXACTLY ONE of dll_path or version: "
            "supply a DLL to resolve the version from, OR a pre-resolved (tag, "
            "ordinal) — never both and never neither.")

    # 1. Determine + refuse the target version, identically to apply_seeds step 1.
    #    The maintainer-tool pre-resolves it (version=, no DLL — D15); a DLL path is
    #    accepted for parity with apply_seeds' contract.
    if version is not None:
        tag, ordinal = version
        _emit(f"  target version (pre-resolved by caller): tag={tag} ordinal={ordinal}")
    else:
        tag, ordinal = resolve_version(dll_path)
        _emit(f"  target version (from DLL .rdata): tag={tag} ordinal={ordinal}")
    if tag != GAME_VERSION_TAG:
        raise VersionRefusal(
            f"version {tag!r} is not covered: the baseline + seeds only cover "
            f"{GAME_VERSION_TAG!r}; run --rebuild for {tag!r} first.", tag=tag)

    # 2. Run the FULL seed-state validation gate. Any failure raises (RuntimeError)
    #    — the SAME gate apply_seeds runs at its step 2. NO DB open follows: this
    #    entry returns the validated version, the write is the Confirm step's.
    _validate_full_seed_state()
    _emit("  prospective seed validated (no DB write — Save preview)")
    return {"tag": tag, "ordinal": ordinal}


def run_apply(out_dir, dll_path):
    """APPLY mode, CLI wrapper: thin shell over the library applier apply_seeds.
    Prints the banner + progress + the per-DB summary, and maps the library's
    typed refusals to the historical exit codes + messages so the command-line
    behaviour (and the apply==rebuild oracle tests that call run_apply) is
    byte-for-byte unchanged. The actual classify/validate/apply work is entirely
    in apply_seeds -- this function adds only the print + sys.exit shell the CLI
    needs and the in-process db_editor does not."""
    bar = "=" * 70
    print(bar)
    print("[import_to_sqlite] mode: APPLY (incremental)")
    print(bar)

    try:
        result = apply_seeds(out_dir, dll_path, log=print)
    except VersionResolveError as e:
        print(f"  version resolve FAILED: {e}")
        sys.exit(APPLY_VERSION_REFUSE_EXIT)
    except VersionRefusal as e:
        print(f"  REFUSE: {e}")
        sys.exit(APPLY_VERSION_REFUSE_EXIT)
    except BaselineRefusal as e:
        print(f"  REFUSE: {e}")
        sys.exit(APPLY_BASELINE_REFUSE_EXIT)

    # 7. Report. Nothing silent.
    print(bar)
    print("APPLY SUMMARY")
    for label in ("user", "dev"):
        c = result["counts"][label]
        print(f"  {label:4s} DB : {c['reverified']} re-verified, {c['noop']} no-op, "
              f"{c['added_entity']} added-entity, "
              f"{c['added_versions_row']} added-versions-row, "
              f"{c['deprecated']} deprecated, {c['superseded']} superseded, "
              f"{c['skipped_dep_sup']} skipped (unmodeled names edit)")
    print(bar)


def _usage(code):
    print("usage (default UPDATE mode): "
          "python import_to_sqlite.py <out_dir> <game_dir>")
    print("       (rebuild):            "
          "python import_to_sqlite.py --rebuild <dump_dir> <out_dir>")
    print("       (apply):              "
          "python import_to_sqlite.py apply <out_dir> --dll <path-to-WHGame.dll>")
    sys.exit(code)


def main():
    args = sys.argv[1:]

    # APPLY subcommand: `apply <out_dir> --dll <path>` (db-updator Phase 1).
    # The link cache that would make --dll optional is Phase 2; for the MVP
    # --dll is required.
    if args and args[0] == "apply":
        rest = args[1:]
        dll_path = None
        positional = []
        i = 0
        while i < len(rest):
            if rest[i] == "--dll":
                if i + 1 >= len(rest):
                    print("apply: --dll requires a path argument")
                    _usage(2)
                dll_path = rest[i + 1]
                i += 2
            else:
                positional.append(rest[i])
                i += 1
        if len(positional) < 1 or dll_path is None:
            print("apply: requires <out_dir> and --dll <path-to-WHGame.dll> "
                  "(--dll is required for the MVP; the link cache is Phase 2)")
            _usage(2)
        run_apply(positional[0], dll_path)
        return

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
            _usage(2)
        run_update(args[0], args[1])


if __name__ == "__main__":
    main()
