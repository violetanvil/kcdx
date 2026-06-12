"""seeds_shared.bulk_exporter -- the DB->CSV half of the BULK round-trip (D38).

The complement to csv_exporter (the CURATED half). Where csv_exporter inverts the
importer's seed-schema reshape to produce the three human-reviewable curated seed
CSVs, this module captures the DEV DB's BULK half AS-IS -- every row, every column,
verbatim -- for a lossless DB->CSV->DB round-trip. Two distinct responsibilities,
two files (structure-by-responsibility): the curated exporter is a reshape inverse;
the bulk exporter is a raw lossless table dump. They share NOTHING but the schema
column declarations they both read from seeds_shared.schema.

WHY raw, not reshaped: the bulk CSVs are NOT for human review (the curated seed
CSVs are). They exist so run_rebuild (P1.3) can reconstruct the DEV DB's bulk rows
by reinserting them VERBATIM -- the dict-encoded integer ids as stored, the FK
integers as stored, the id PKs as stored, the BLOB content_hash intact. Capturing
the raw stored column values (no dict-decode, no reshape) is exactly what makes the
round-trip lossless: a dict-decode-then-reencode would risk losing fidelity, and a
reshape would not round-trip at all. (Proven by the P0.1 round-trip probe -- the
~17.7M-row DEV bulk round-trips byte/value-identical with this raw-capture +
verbatim-reinsert recipe.)

WHAT it captures (D38's bulk half):
  - statements        (DEV-only)  -- all rows
  - referenced_vars   (DEV-only)  -- all rows
  - call_edges        (DEV-only)  -- all rows
  - address_versions  -- the kcdx_id IS NULL subset, FULL ROW verbatim (the ~321k
    bulk discovery rows). They are all-derived (no authored half), so the bulk CSV
    carries every column and the rebuild reinserts them byte-for-byte.
  - address_versions_derived  -- the kcdx_id IS NOT NULL (CURATED) subset, the
    DERIVED COLUMNS ONLY (keyed by id + kcdx_id). The curated av rows have BOTH an
    authored half (-> the curated seed CSV, csv_exporter.export_seeds) AND a derived
    half. Their authored columns (kind/rva/signature/offset/.../survival_*) come
    from the curated seed CSV; their DUMP-DERIVED columns (length, content_hash,
    observed_arg_slots, caller_reg_arg_count, caller_arg_agreement, auto_name,
    decompile_quality, valid_through) plus the PROMOTED id are carried HERE. The
    curated seed CSV forbids a derived column on the authored surface
    (csv_exporter:15), so without this overlay a CSV-genesis rebuild could not
    reconstruct the curated function fingerprint (D38's lossless-round-trip gap).
    The rebuild MERGES this overlay onto the curated rows it builds from the seed
    CSV (by kcdx_id key), restoring the full curated av row byte-identical to the
    dump build -- including the original promoted av_id, so the dependent curated
    statements/referenced_vars subset (filtered by the curated av_id set) is not
    perturbed.

WHY a SEPARATE address_versions_derived.csv (not the derived columns folded into
address_versions.csv): the kcdx_id-NULL bulk rows are read VERBATIM by the rebuild
+ the bulk round-trip oracle (every column reinserted as-is). Keeping
address_versions.csv as the pristine all-columns verbatim dump of ONLY the
kcdx_id-NULL rows leaves that proven round-trip untouched; the curated derived data
is a small, clearly-named OVERLAY (id + kcdx_id + the derived subset) the rebuild
merges onto the seed-built curated rows. One file per responsibility: one verbatim
bulk dump, one curated-derived overlay -- not a mixed file with NULLed authored
columns for the curated rows.

The lossless encoding (lifted verbatim from the P0.1 probe -- the verified-correct
encoding, reused, not re-invented):
  - NULL -> the sentinel ``\\N`` (so an empty TEXT '' round-trips as '', distinct
    from a NULL).
  - BLOB -> ``blob:`` + lowercase hex (round-trips content_hash byte-identically).
  - int/float/str -> str() (the raw stored value; the decode is type-aware on the
    rebuild side, P1.3).

The column sets come from seeds_shared.schema.SCHEMA (one source of truth -- no
duplicated column knowledge; the curated exporter's discipline). A column added to
a bulk table in SCHEMA is captured automatically.

This module is HEADLESS and Qt-free (the data-core has no GUI dependency).

Private package (lives under the already-private data/refdata-extractor/ tree).
"""
import csv
import os
import sqlite3

from .schema import SCHEMA, DEV_TABLES


# ---------------------------------------------------------------------------
# The bulk table/column sets. The TABLE set is D38's named bulk-half list; the
# COLUMN sets come from schema.py (no duplicated column knowledge).
# ---------------------------------------------------------------------------
# D38's bulk half is these three DEV-only-data tables exported WHOLE, plus the
# kcdx_id-NULL address_versions subset (named separately below). NOTE the set is
# NOT `DEV_TABLES - USER_TABLES`: statements + referenced_vars ARE in USER_TABLES
# (for the curated row-filtered subset the USER projection ships -- see schema.py),
# yet D38 sends the WHOLE of both, plus all of call_edges, to the bulk export. So
# the bulk set is D38's explicit list, not the table-set difference. Each name is
# asserted to exist in SCHEMA below so a typo or a renamed table fails loud.
BULK_DEV_ONLY_TABLES = ["statements", "referenced_vars", "call_edges"]
assert all(t in SCHEMA and t in DEV_TABLES for t in BULK_DEV_ONLY_TABLES), (
    "bulk_exporter: a BULK_DEV_ONLY_TABLES entry is not a DEV-DB table in schema.py")

# address_versions: the kcdx_id-NULL subset is exported FULL-ROW (the bulk
# discovery rows; all-derived). Named separately because it carries a WHERE filter
# the DEV-only tables do not.
BULK_AV_TABLE = "address_versions"

# The CURATED av rows (kcdx_id IS NOT NULL) carry a DERIVED half the curated seed
# CSV cannot (a derived column is forbidden on the authored surface,
# csv_exporter:15). That derived half is exported HERE, keyed by the row's id +
# kcdx_id, as a small overlay the rebuild merges onto the seed-built curated rows.
#
# The AUTHORED set (-> curated seed CSV, reconstructed by the seeds_shared row
# builders): kcdx_id, valid_from, module_id, rva, kind, signature,
# last_verified_at_version, verified_by, verified_date, evidence_kind, offset,
# vtable_slot, struct_offset, aob, anchor_string, rule, slot_count, expect_unique,
# derives_from. (`value` is NOT carried by either file -- the curated builder
# synthesizes value=vtable_slot, so it is reconstructed from the authored
# vtable_slot, not stored.)
#
# The DERIVED set is an EXPLICIT allowlist (_AV_DERIVED_COLS below) -- the 8
# dump-derived av columns the overlay carries. It is NOT computed as (all_cols -
# authored - keys): a computed complement makes the coverage assert a tautology (a
# NEW SCHEMA av column would be swept silently into the complement instead of being
# flagged). With three EXPLICIT sets (authored / key / derived), the coverage assert
# in _curated_av_derived_cols is REAL -- a new SCHEMA column in NONE of the three
# leaves (authored | key | derived) != all_cols and FIRES at import time (AP14: a
# lossy/misclassified export is loud, not quiet). The KEYS (id, kcdx_id) lead the
# overlay row so the merge joins by kcdx_id and restores the promoted id.
AV_DERIVED_TABLE = "address_versions_derived"

# The authored av columns the curated SEED CSV round-trips (its DB-column targets).
# `value` is excluded from the OVERLAY but listed here: it is builder-synthesized
# (value=vtable_slot), carried by neither CSV, so for the av-column PARTITION it sits
# on the authored side (reconstructed from authored vtable_slot). One of the three
# EXPLICIT, DISJOINT partition sets (authored / key / derived) the coverage assert
# checks -- so it carries NO key column (kcdx_id is the KEY partition's, below): the
# seed CSV does author a kcdx_id cell, but for the av-column PARTITION kcdx_id belongs
# to exactly one set, and that set is the keys (it is the overlay's join key).
_AV_AUTHORED_COLS = frozenset({
    "valid_from", "module_id", "rva", "kind", "signature",
    "last_verified_at_version", "verified_by", "verified_date", "evidence_kind",
    "offset", "vtable_slot", "struct_offset",
    "aob", "anchor_string", "rule", "slot_count", "expect_unique", "derives_from",
    "value",   # builder-synthesized from vtable_slot; in neither CSV, reconstructed
    # valid_through: the interval-CLOSE column, now AUTHORED (D40 -- moved off this
    # derived overlay onto the curated seed CSV so an interval edit round-trips via
    # the human-reviewable authored surface). The curated seed CSV carries
    # valid_through_version (its game_versions tag); build_rows_from_csv resolves the
    # tag -> the valid_through FK. So it is the seed CSV's now, NOT this overlay's --
    # the overlay payload (_AV_DERIVED_COLS) no longer carries it.
    "valid_through",
})
# The two key columns (carried in the derived overlay as the join keys, NOT part of
# the derived payload). One of the three EXPLICIT partition sets.
_AV_KEY_COLS = ("id", "kcdx_id")
# The DERIVED av columns (in SCHEMA order) the curated-derived overlay carries as its
# PAYLOAD -- an EXPLICIT allowlist, NOT a computed complement. These are the 7
# dump-derived columns: the function fingerprint (content_hash / length / arg-slot
# observations / caller-agreement) and the DEV-only discovery fields (auto_name /
# decompile_quality). Explicit so the coverage assert in _curated_av_derived_cols can
# FIRE on a new unclassified SCHEMA column instead of silently sweeping it in (the
# third partition set). valid_through was MOVED to _AV_AUTHORED_COLS (D40 -- the
# interval-CLOSE column is now authored on the curated seed CSV, no longer carried by
# this derived overlay).
_AV_DERIVED_COLS = frozenset({
    "length", "content_hash", "observed_arg_slots", "caller_reg_arg_count",
    "caller_arg_agreement", "auto_name", "decompile_quality",
})


def _curated_av_derived_cols():
    """The DERIVED av column list (in SCHEMA order) the curated-derived overlay
    carries as its PAYLOAD: the explicit _AV_DERIVED_COLS allowlist, returned in
    SCHEMA column order.

    The three sets (authored / key / derived) are each EXPLICIT, so the partition
    asserts below are REAL, not tautological:
      - COVERAGE: authored | key | derived == every SCHEMA av column. A NEW SCHEMA
        column classified into NONE of the three is reported by name and FIRES --
        the future-gap guard (AP14: a lossy/misclassified export is loud, not quiet).
        (A computed `derived = all - authored - keys` would make this true by
        construction and the new column would be silently swept into the overlay.)
      - DISJOINT: authored, key, derived do not overlap -- a column claimed by two
        sets (a mis-edit) FIRES rather than producing a double-counted/ambiguous
        export."""
    all_cols = [name for (name, _t) in SCHEMA[BULK_AV_TABLE]]
    all_set = set(all_cols)
    authored = set(_AV_AUTHORED_COLS)
    keys = set(_AV_KEY_COLS)
    derived = set(_AV_DERIVED_COLS)

    # DISJOINT: no column is claimed by two partition sets.
    ak = authored & keys
    ad = authored & derived
    kd = keys & derived
    assert not (ak or ad or kd), (
        f"bulk_exporter: address_versions partition OVERLAP -- "
        f"authored&key={sorted(ak)}, authored&derived={sorted(ad)}, "
        f"key&derived={sorted(kd)}; each av column belongs to exactly ONE of "
        f"authored / key / derived.")

    # COVERAGE: every SCHEMA av column is in exactly one of the three explicit sets.
    # A NEW unclassified column fires here (the guard the comment promises); a set
    # naming a column that is NOT in the SCHEMA (a typo / stale entry) fires too.
    covered = authored | keys | derived
    unclassified = all_set - covered  # in SCHEMA, in no set -> the future-gap
    stale = covered - all_set         # in a set, not in SCHEMA -> a typo/stale entry
    assert not unclassified and not stale, (
        f"bulk_exporter: address_versions column partition gap/mismatch -- "
        f"unclassified (in SCHEMA, in no authored/key/derived set): "
        f"{sorted(unclassified)}; stale (in a set, not in SCHEMA): {sorted(stale)}. "
        f"Classify every SCHEMA av column into exactly one set.")

    # Return the derived payload in SCHEMA column order (the overlay's column order).
    return [c for c in all_cols if c in derived]


# The curated-derived overlay column order: the two keys, then the derived payload.
AV_DERIVED_CSV_COLS = list(_AV_KEY_COLS) + _curated_av_derived_cols()

# The CSV file names under data/db-export-bulk/ (one per captured table + the
# curated-derived overlay).
BULK_CSV_NAMES = {t: f"{t}.csv" for t in BULK_DEV_ONLY_TABLES + [BULK_AV_TABLE]}
BULK_CSV_NAMES[AV_DERIVED_TABLE] = f"{AV_DERIVED_TABLE}.csv"

# The default output dir, relative to the repo root (D38: the bulk bundle at
# data/db-export-bulk/). Callers pass an explicit out_dir; this names the canonical
# location for the few callers (and tests) that want it.
DEFAULT_BULK_DIR = os.path.join("data", "db-export-bulk")

NULL_SENTINEL = r"\N"
BLOB_PREFIX = "blob:"


# ---------------------------------------------------------------------------
# The lossless cell encoder (lifted from the P0.1 probe -- the verified encoding).
# ---------------------------------------------------------------------------
def cell_to_csv(v):
    """Encode one stored DB cell for the bulk CSV, distinguishing NULL from '' and
    round-tripping a BLOB. The inverse (csv_to_cell, on the rebuild side P1.3) reads
    these back verbatim.

    NULL -> ``\\N`` (so an empty TEXT '' is preserved distinct from a NULL).
    BLOB -> ``blob:`` + lowercase hex (content_hash round-trips byte-identically).
    int/float/str -> str() (the raw stored value; rebuild decodes by column type)."""
    if v is None:
        return NULL_SENTINEL
    if isinstance(v, (bytes, bytearray, memoryview)):
        return BLOB_PREFIX + bytes(v).hex()
    return str(v)


# ---------------------------------------------------------------------------
# Schema-driven column reads (no duplicated column knowledge).
# ---------------------------------------------------------------------------
def _schema_columns(table):
    """The table's column NAMES in SCHEMA declaration order -- the SAME order the
    CSV header is written in and the rebuild reinserts in. SCHEMA is the single
    source of truth; the export never hardcodes a column list."""
    return [name for (name, _sqltype) in SCHEMA[table]]


def _assert_db_readable(con, table):
    """Fail LOUD on a malformed/unreadable DEV DB (AP14): the bulk table must exist
    and its live columns must match the SCHEMA declaration. A silent empty/partial
    bulk CSV from a missing table or a drifted schema is the defect this prevents --
    a structured error names the exact mismatch instead."""
    present = con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        (table,)).fetchone()
    if present is None:
        raise BulkExportError(
            f"bulk export: table {table!r} absent from the DEV DB -- the export "
            f"would be silently incomplete. Is this the DEV reference-dev.sqlite?")
    live_cols = [r[1] for r in con.execute(f'PRAGMA table_info("{table}")')]
    decl_cols = _schema_columns(table)
    if live_cols != decl_cols:
        raise BulkExportError(
            f"bulk export: table {table!r} columns drifted from schema.py "
            f"declaration.\n  live: {live_cols}\n  schema: {decl_cols}\n"
            f"The export captures SCHEMA's columns; a drift would silently drop or "
            f"misorder a column -- fix the schema or the DB before exporting.")


class BulkExportError(RuntimeError):
    """A malformed/unreadable DEV DB or a schema drift caught at export time --
    raised LOUD so a bulk export never lands a silent empty/partial CSV (AP14)."""


# ---------------------------------------------------------------------------
# Per-table raw lossless CSV writer.
# ---------------------------------------------------------------------------
def _export_one_table(con, table, out_path, *, where=None):
    """Write `table`'s rows (optionally filtered by `where`) to out_path as a raw
    lossless CSV: a header of the SCHEMA column names, then one row per DB row with
    every cell encoded by cell_to_csv. Streams via the cursor (no full-table
    materialization -- statements is millions of rows). Returns the row count.

    QUOTE_MINIMAL (the csv default) so a cell containing a comma/quote/newline is
    quoted and read back identically; the \\N / blob: encodings are plain tokens
    that need no quoting."""
    cols = _schema_columns(table)
    col_list = ", ".join(f'"{c}"' for c in cols)
    sql = f'SELECT {col_list} FROM "{table}"'
    if where:
        sql += f" WHERE {where}"
    n = 0
    # newline="" per the csv module contract (it manages line terminators itself).
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for row in con.execute(sql):
            w.writerow([cell_to_csv(v) for v in row])
            n += 1
    return n


def _export_curated_av_derived(con, out_path):
    """Write the CURATED av rows' (kcdx_id IS NOT NULL) DERIVED columns to out_path
    as a raw lossless CSV, keyed by id + kcdx_id (AV_DERIVED_CSV_COLS): the two keys
    then the derived payload (length / content_hash / observed_arg_slots / ... /
    valid_through), each cell cell_to_csv-encoded. A PROJECTED-column write (not the
    full SCHEMA row _export_one_table does), so it carries ONLY the derived half --
    the authored half lives in the curated seed CSV; the rebuild merges the two.
    Returns the row count. Streams via the cursor (curated set is small, but the
    pattern matches the rest of the module)."""
    cols = AV_DERIVED_CSV_COLS
    col_list = ", ".join(f'"{c}"' for c in cols)
    sql = (f'SELECT {col_list} FROM "{BULK_AV_TABLE}" '
           f"WHERE kcdx_id IS NOT NULL ORDER BY id")
    n = 0
    with open(out_path, "w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(cols)
        for row in con.execute(sql):
            w.writerow([cell_to_csv(v) for v in row])
            n += 1
    return n


# ---------------------------------------------------------------------------
# Public API.
# ---------------------------------------------------------------------------
def export_bulk(dev_db_path, out_dir):
    """Export the DEV DB's BULK half (D38) from `dev_db_path` into `out_dir`,
    losslessly.

    Writes one CSV per captured table under `out_dir`:
      - statements.csv / referenced_vars.csv / call_edges.csv  -- the DEV-only
        bulk tables, every row.
      - address_versions.csv  -- the kcdx_id IS NULL subset, FULL ROW verbatim
        (the bulk discovery rows; all-derived).
      - address_versions_derived.csv  -- the kcdx_id IS NOT NULL (curated) subset,
        the DERIVED columns ONLY (keyed by id + kcdx_id). The curated rows' authored
        half is the curated seed CSV's job (csv_exporter.export_seeds); the rebuild
        merges this derived overlay onto the seed-built curated rows so the full
        curated av row round-trips byte-identical (incl. the promoted id).

    The full-row CSVs capture every SCHEMA column verbatim (the id PK, the FK
    integers, the dict-encoded ids as stored, the BLOB content_hash) with NULL
    distinguished from '' -- so run_rebuild (P1.3) reinserts them byte/value-
    identical. The derived overlay carries only the curated rows' derived half +
    keys. Together with csv_exporter.export_seeds (the curated authored half) every
    av row -- bulk and curated -- round-trips with no column lost: bulk = full
    verbatim; curated = authored (seed CSV) merged with derived (this overlay).

    `dev_db_path` -- the DEV reference-dev.sqlite (carries the bulk tables + the
                     kcdx_id-NULL av rows). A USER DB lacks call_edges + the bulk
                     rows -> export_bulk fails loud (AP14) rather than write empties.
    `out_dir`     -- the directory the bulk CSVs are written into (created if
                     absent). D38's canonical location is data/db-export-bulk/.

    Returns the dict {filename: row_count} written.

    Raises BulkExportError on a missing table or a schema drift (AP14 -- never a
    silent empty/partial bulk CSV)."""
    con = sqlite3.connect(dev_db_path)
    try:
        return _export_all(con, out_dir)
    finally:
        con.close()


def _export_all(con, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    written = {}

    # Validate EVERY captured table up front (fail loud before writing anything --
    # AP14: a partial write that errors midway is worse than a clean refusal).
    for table in BULK_DEV_ONLY_TABLES + [BULK_AV_TABLE]:
        _assert_db_readable(con, table)

    # The DEV-only bulk tables: every row.
    for table in BULK_DEV_ONLY_TABLES:
        name = BULK_CSV_NAMES[table]
        out_path = os.path.join(out_dir, name)
        written[name] = _export_one_table(con, table, out_path)

    # address_versions: the kcdx_id-NULL (bulk discovery) subset, FULL ROW verbatim
    # (all-derived rows; read back verbatim by the rebuild). The curated
    # kcdx_id-NOT-NULL rows' DERIVED half goes to the overlay below; their authored
    # half is csv_exporter.export_seeds' job.
    av_name = BULK_CSV_NAMES[BULK_AV_TABLE]
    av_path = os.path.join(out_dir, av_name)
    written[av_name] = _export_one_table(
        con, BULK_AV_TABLE, av_path, where="kcdx_id IS NULL")

    # address_versions_derived: the CURATED av rows' DERIVED columns + keys (the
    # overlay the rebuild merges onto the seed-built curated rows). This is the
    # column the production export previously DROPPED -- the curated function
    # fingerprint (content_hash/length/...) fell through both CSV halves, breaking
    # D38's lossless round-trip. Carrying it here closes the gap.
    avd_name = BULK_CSV_NAMES[AV_DERIVED_TABLE]
    avd_path = os.path.join(out_dir, avd_name)
    written[avd_name] = _export_curated_av_derived(con, avd_path)

    return written
