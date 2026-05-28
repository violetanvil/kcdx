"""import_to_sqlite.py -- the maintainer-side import: CSV-per-table dump dirs +
the curated seed -> two encoded SQLite reference DBs on the LOCKED entity/version
schema.

PRODUCES TWO artifacts from one full-dump dir + the curated seed:
  - USER DB  (<out>/reference.sqlite, ships in every kcdx release): the tables a
    mod USER needs at runtime -- modules, game_versions, entities,
    entity_versions (minus the dev-only columns), kcdx_overlay (minus dev-only
    columns), kcdx_overlay_versions, meta. Powers the per-launch cross-version
    survival check (content_hash) + the ABI floor for hooked functions.
  - DEV DB   (<out>/reference-dev.sqlite, the author/on-demand download): the FULL
    set incl. statements + referenced_vars + call_edges -- the discovery surface.

SCHEMA (LOCKED -- 9 entity/version tables + _dict_* lookups):
  modules, game_versions, entities, entity_versions, kcdx_overlay,
  kcdx_overlay_versions, meta, statements (DEV), referenced_vars (DEV),
  call_edges (DEV). Every table has an autoincrement INTEGER PK `id` EXCEPT
  `entities`, whose PK IS `kcdx_id` (the stable id-authority, NOT a surrogate).

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
    "entities":              ["entity_type"],
    "entity_versions":       ["caller_arg_agreement", "decompile_quality"],
    "kcdx_overlay":          ["kind", "source"],
    "kcdx_overlay_versions": ["status"],
    "statements":            ["kind"],
    "referenced_vars":       ["storage_kind", "data_type"],
}

# USER db column allowlists (per table). Tables NOT listed here are DEV-only.
# A column omitted from the list is dropped from the USER CREATE TABLE + insert.
USER_COLUMNS = {
    "modules":               ["id", "name"],
    "game_versions":         ["id", "tag", "ordinal", "released"],
    "entities":              ["kcdx_id", "entity_type", "module_id"],
    "entity_versions":       ["id", "kcdx_id", "content_hash", "rva", "length",
                              "value", "signature", "observed_arg_slots",
                              "caller_reg_arg_count", "caller_arg_agreement",
                              "valid_from", "valid_through"],
    # excludes auto_name, decompile_quality (DEV-only)
    "kcdx_overlay":          ["id", "kcdx_id", "name", "kind", "is_deprecated",
                              "superseded_by"],
    # excludes source, notes (DEV-only)
    "kcdx_overlay_versions": ["id", "overlay_id", "signature", "offset",
                              "vtable_slot", "status", "valid_from",
                              "valid_through"],
    "meta":                  ["id", "schema_version", "abi_confidence"],
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
    "entities": [
        ("kcdx_id", "INTEGER PRIMARY KEY"),   # NOT autoincrement: it IS the id
        ("entity_type", "INTEGER"),           # dict
        ("module_id", "INTEGER"),
    ],
    "entity_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),
        ("content_hash", "BLOB"),
        ("rva", "INTEGER"),
        ("length", "INTEGER"),
        ("value", "INTEGER"),
        ("signature", "TEXT"),
        ("observed_arg_slots", "INTEGER"),
        ("caller_reg_arg_count", "INTEGER"),
        ("caller_arg_agreement", "INTEGER"),   # dict
        ("auto_name", "TEXT"),                  # DEV-ONLY
        ("decompile_quality", "INTEGER"),       # dict, DEV-ONLY
        ("valid_from", "INTEGER"),
        ("valid_through", "INTEGER"),
    ],
    "kcdx_overlay": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("kcdx_id", "INTEGER"),
        ("name", "TEXT"),
        ("kind", "INTEGER"),                    # dict
        ("is_deprecated", "INTEGER"),
        ("superseded_by", "INTEGER"),
        ("source", "INTEGER"),                  # dict, DEV-ONLY
        ("notes", "TEXT"),                      # DEV-ONLY
    ],
    "kcdx_overlay_versions": [
        ("id", "INTEGER PRIMARY KEY AUTOINCREMENT"),
        ("overlay_id", "INTEGER"),
        ("signature", "TEXT"),
        ("offset", "INTEGER"),
        ("vtable_slot", "INTEGER"),
        ("status", "INTEGER"),                  # dict
        ("valid_from", "INTEGER"),
        ("valid_through", "INTEGER"),
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
DEV_TABLES = ["modules", "game_versions", "entities", "entity_versions",
              "kcdx_overlay", "kcdx_overlay_versions", "meta",
              "statements", "referenced_vars", "call_edges"]
USER_TABLES = ["modules", "game_versions", "entities", "entity_versions",
               "kcdx_overlay", "kcdx_overlay_versions", "meta"]

# The 9 overlay kinds the inference covers.
OVERLAY_KINDS = ("function", "function_variadic", "function_no_sig", "callsite",
                 "vtable_index", "vtable_base", "data_slot", "string_anchor",
                 "instruction_anchor")

# The pairing trigger, applied to BOTH dbs. See header comment in build_db.
# Semantics: fires ONLY on a non-baseline entity_versions insert (an open
# interval, valid_through IS NULL) for a kcdx_id that ALREADY has a row in
# kcdx_overlay. At baseline (this import) overlay rows are inserted AFTER all
# entity_versions, so the WHEN clause is always false -> the trigger NEVER fires
# during baseline. It exists for the future v1.6+ version-update path: when a new
# game version's facts arrive, inserting the new open entity_versions row closes
# the entity's current open overlay-version interval and forks a fresh
# (unverified) one carrying the prior signature/offset/vtable_slot forward.
TRIGGER_SQL = """
CREATE TRIGGER trg_pair_overlay_version
AFTER INSERT ON entity_versions
WHEN NEW.valid_through IS NULL
 AND EXISTS (SELECT 1 FROM kcdx_overlay WHERE kcdx_id = NEW.kcdx_id)
BEGIN
  -- (a) close the entity's current OPEN overlay-version interval. Using
  --     NEW.valid_from as the close boundary is the agreed stub semantics: the
  --     prior verified facts are marked last-valid at the version where the new
  --     form begins (a later refinement may close at NEW.valid_from - 1).
  UPDATE kcdx_overlay_versions
     SET valid_through = NEW.valid_from
   WHERE valid_through IS NULL
     AND overlay_id IN (SELECT id FROM kcdx_overlay WHERE kcdx_id = NEW.kcdx_id);
  -- (b) fork a fresh open interval per affected overlay, copying the prior
  --     signature/offset/vtable_slot forward, status = 'unverified'.
  INSERT INTO kcdx_overlay_versions
        (overlay_id, signature, offset, vtable_slot, status, valid_from, valid_through)
  SELECT ov.overlay_id, ov.signature, ov.offset, ov.vtable_slot,
         (SELECT id FROM _dict_kcdx_overlay_versions_status WHERE val = 'unverified'),
         NEW.valid_from, NULL
    FROM kcdx_overlay_versions ov
    JOIN kcdx_overlay o ON o.id = ov.overlay_id
   WHERE o.kcdx_id = NEW.kcdx_id
     AND ov.valid_through = NEW.valid_from;
END;
"""


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


def overlay_offset_and_slot(kind, notes):
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

    # --- 2./3. entities + entity_versions for each function ---
    for i, r in enumerate(functions, start=1):
        rv = parse_int(r.get("rva", ""))
        rows["entities"].append({
            "kcdx_id": i,
            "entity_type": dicts.encode("entities", "entity_type", "function"),
            "module_id": MODULE_ID,
        })
        sig = sig_by_rva.get(rv)
        cra = cra_by_rva.get(rv)
        rows["entity_versions"].append({
            "kcdx_id": i,
            "content_hash": hash_blob(r.get("content_hash", "")),
            "rva": rv,
            "length": parse_int(r.get("length", "")),
            "value": None,
            "signature": (sig.get("signature") if sig else None) or None,
            "observed_arg_slots": parse_int(sig.get("observed_arg_slots", "")) if sig else None,
            "caller_reg_arg_count": parse_int(cra.get("caller_reg_arg_count", "")) if cra else None,
            "caller_arg_agreement": dicts.encode("entity_versions", "caller_arg_agreement",
                                                 cra.get("agreement", "")) if cra else None,
            "auto_name": (r.get("auto_name") or None),
            "decompile_quality": dicts.encode("entity_versions", "decompile_quality",
                                              r.get("decompile_quality", "")),
            "valid_from": GAME_VERSION_ID,
            "valid_through": None,
        })

    # --- 2. curated-only vtable_index entities (seed ids 3000-3005, empty rva) ---
    seed = read_seed(SEED_CSV)
    print(f"  seed.csv: {len(seed)} curated rows", flush=True)
    next_kcdx = n_functions + 1
    seed_id_to_kcdx = {}   # seed.id -> kcdx_id (for the curated rows)
    for s in seed:
        srva = (s.get("rva") or "").strip()
        if srva:
            continue   # code rows map through rva_to_kcdx below
        kid = next_kcdx
        next_kcdx += 1
        seed_id_to_kcdx[s.get("id")] = kid
        rows["entities"].append({
            "kcdx_id": kid,
            "entity_type": dicts.encode("entities", "entity_type", "vtable_slot"),
            "module_id": MODULE_ID,
        })
        # curated entity_version: value = the slot int (parsed from notes), no
        # rva/length/hash.
        _, slot = overlay_offset_and_slot("vtable_index", (s.get("notes") or "").lower())
        rows["entity_versions"].append({
            "kcdx_id": kid,
            "content_hash": None,
            "rva": None,
            "length": None,
            "value": slot,
            "signature": (s.get("signature") or None) or None,
            "observed_arg_slots": None,
            "caller_reg_arg_count": None,
            "caller_arg_agreement": None,
            "auto_name": None,
            "decompile_quality": None,
            "valid_from": GAME_VERSION_ID,
            "valid_through": None,
        })

    n_curated = next_kcdx - 1 - n_functions
    print(f"  curated vtable_index entities: {n_curated}", flush=True)

    # --- 6./7. kcdx_overlay + paired kcdx_overlay_versions (seed the 139) ---
    # Pre-register dict values the trigger needs.
    dicts.ensure("kcdx_overlay_versions", "status", "unverified")
    overlay_id = 1
    n_seed_mapped = 0
    n_seed_unmapped = 0
    for s in seed:
        srva = (s.get("rva") or "").strip()
        if srva:
            rv = parse_int(srva)
            kid = rva_to_kcdx.get(rv)
            if kid is None:
                # seed code row whose rva is not in the dump's functions/. The
                # overlay must still be seeded; mint an entity for it so the FK
                # resolves (it is a real curated address the dump didn't cover).
                kid = next_kcdx
                next_kcdx += 1
                rows["entities"].append({
                    "kcdx_id": kid,
                    "entity_type": dicts.encode("entities", "entity_type", "function"),
                    "module_id": MODULE_ID,
                })
                rows["entity_versions"].append({
                    "kcdx_id": kid, "content_hash": None, "rva": rv, "length": None,
                    "value": None, "signature": (s.get("signature") or None) or None,
                    "observed_arg_slots": None, "caller_reg_arg_count": None,
                    "caller_arg_agreement": None, "auto_name": None,
                    "decompile_quality": None,
                    "valid_from": GAME_VERSION_ID, "valid_through": None,
                })
                n_seed_unmapped += 1
            else:
                n_seed_mapped += 1
        else:
            kid = seed_id_to_kcdx.get(s.get("id"))
            if kid is None:
                continue   # should not happen; vtable rows minted above
        kind = infer_kind(s)
        offset, vslot = overlay_offset_and_slot(kind, (s.get("notes") or "").lower())
        rows["kcdx_overlay"].append({
            "id": overlay_id,
            "kcdx_id": kid,
            "name": (s.get("name") or None),
            "kind": dicts.encode("kcdx_overlay", "kind", kind),
            "is_deprecated": 0,
            "superseded_by": None,
            "source": dicts.encode("kcdx_overlay", "source", s.get("source", "")),
            "notes": (s.get("notes") or None),
        })
        rows["kcdx_overlay_versions"].append({
            "id": overlay_id,
            "overlay_id": overlay_id,
            "signature": (s.get("signature") or None) or None,
            "offset": offset,
            "vtable_slot": vslot,
            "status": dicts.encode("kcdx_overlay_versions", "status", s.get("status", "")),
            "valid_from": GAME_VERSION_ID,
            "valid_through": None,
        })
        overlay_id += 1
    print(f"  kcdx_overlay: {overlay_id - 1} rows "
          f"(seed code mapped={n_seed_mapped}, code unmapped+minted={n_seed_unmapped}, "
          f"curated={len(seed_id_to_kcdx)})", flush=True)

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
        "curated_vtable": n_curated,
        "entities": len(rows["entities"]),
        "entity_versions": len(rows["entity_versions"]),
        "kcdx_overlay": len(rows["kcdx_overlay"]),
        "kcdx_overlay_versions": len(rows["kcdx_overlay_versions"]),
    }
    return rows, counts


# ---------------------------------------------------------------------------
# Write one db (USER or DEV) from the shared row sets.
# ---------------------------------------------------------------------------
def write_db(db_path, rows, dicts, tables, user_projection):
    if os.path.exists(db_path):
        os.remove(db_path)
    con = sqlite3.connect(db_path)
    con.executescript("PRAGMA journal_mode=OFF; PRAGMA synchronous=OFF; PRAGMA page_size=4096;")

    # Materialize dict lookup tables FIRST (the trigger references one by name).
    dict_entries = dicts.materialize(con)

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
        for row in rows[t]:
            batch.append([row.get(c) for c in colnames])
            n += 1
            if len(batch) >= 20000:
                con.executemany(ins, batch)
                batch = []
        if batch:
            con.executemany(ins, batch)
        print(f"    {t}: {n} rows", flush=True)

    # Indexes for the engine's lookup paths.
    con.execute('CREATE INDEX ix_ev_kcdx ON entity_versions(kcdx_id)')
    con.execute('CREATE INDEX ix_ev_rva ON entity_versions(rva)')
    con.execute('CREATE INDEX ix_ov_kcdx ON kcdx_overlay(kcdx_id)')
    con.execute('CREATE INDEX ix_ovv_overlay ON kcdx_overlay_versions(overlay_id)')
    if "statements" in tables:
        con.execute('CREATE INDEX ix_st_kcdx ON statements(kcdx_id, idx)')
    if "referenced_vars" in tables:
        con.execute('CREATE INDEX ix_rv_kcdx ON referenced_vars(kcdx_id)')
    if "call_edges" in tables:
        con.execute('CREATE INDEX ix_ce_caller ON call_edges(caller_kcdx_id)')
        con.execute('CREATE INDEX ix_ce_callee ON call_edges(callee_kcdx_id)')

    # The pairing trigger (BOTH dbs). Created AFTER baseline inserts -> never
    # fires for this import (the overlay rows that satisfy the WHEN clause were
    # inserted after their entity_versions counterparts).
    con.execute(TRIGGER_SQL)

    con.commit()
    con.execute("VACUUM")
    con.close()
    return dict_entries


def main():
    if len(sys.argv) < 3:
        print("usage: python import_to_sqlite.py <dump_dir> <out_dir>")
        sys.exit(2)
    dump_dir, out_dir = sys.argv[1], sys.argv[2]
    os.makedirs(out_dir, exist_ok=True)

    user_db = os.path.join(out_dir, "reference.sqlite")
    dev_db = os.path.join(out_dir, "reference-dev.sqlite")

    bar = "=" * 70
    print(bar)
    print(f"[import_to_sqlite] dump: {dump_dir}")
    print(f"[import_to_sqlite] seed: {SEED_CSV}")
    print(bar)

    # Build the row sets ONCE (shared by both dbs). The dict encoder is shared.
    dicts = Dicts()
    t0 = time.time()
    print("\n== TRANSFORM (dump + seed -> schema rows)")
    rows, counts = build_rows(dump_dir, dicts)
    print(f"  transform done in {time.time()-t0:.0f}s")
    print(f"  entities={counts['entities']} entity_versions={counts['entity_versions']} "
          f"overlay={counts['kcdx_overlay']} overlay_versions={counts['kcdx_overlay_versions']}")

    print(f"\n== DEV DB -> {dev_db}")
    t0 = time.time()
    dd = write_db(dev_db, rows, dicts, DEV_TABLES, user_projection=False)
    dsz = os.path.getsize(dev_db)
    print(f"  built in {time.time()-t0:.0f}s; size {dsz/1e6:.1f} MB; dict entries {dd}")

    print(f"\n== USER DB -> {user_db}")
    t0 = time.time()
    ud = write_db(user_db, rows, dicts, USER_TABLES, user_projection=True)
    usz = os.path.getsize(user_db)
    print(f"  built in {time.time()-t0:.0f}s; size {usz/1e6:.1f} MB; dict entries {ud}")

    print(bar)
    print("SUMMARY")
    print(f"  USER reference.sqlite     : {usz/1e6:8.1f} MB")
    print(f"  DEV  reference-dev.sqlite : {dsz/1e6:8.1f} MB")
    print(f"  functions={counts['functions']} curated_vtable={counts['curated_vtable']} "
          f"entities={counts['entities']}")
    print(bar)


if __name__ == "__main__":
    main()
