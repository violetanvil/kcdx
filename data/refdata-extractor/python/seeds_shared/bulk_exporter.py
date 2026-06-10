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
  - address_versions  -- the kcdx_id IS NULL subset ONLY (the ~321k bulk discovery
    rows). The curated kcdx_id-NOT-NULL rows are exported by the EXISTING curated
    path (csv_exporter.export_seeds -> data/db-export/address_versions_seed.csv).
    The two halves PARTITION the table by the kcdx_id seam, so each av row is
    exported exactly once and run_rebuild reconstructs the full table as the union
    (curated-CSV reshape + bulk-CSV verbatim) with no dedupe.

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

# address_versions is exported as its kcdx_id-NULL subset (the bulk discovery
# rows); the curated subset is the existing curated path's job (see module
# docstring). Named separately because it carries a WHERE filter the DEV-only
# tables do not.
BULK_AV_TABLE = "address_versions"

# The CSV file names under data/db-export-bulk/ (one per captured table).
BULK_CSV_NAMES = {t: f"{t}.csv" for t in BULK_DEV_ONLY_TABLES + [BULK_AV_TABLE]}

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


# ---------------------------------------------------------------------------
# Public API.
# ---------------------------------------------------------------------------
def export_bulk(dev_db_path, out_dir):
    """Export the DEV DB's BULK half (D38) from `dev_db_path` into `out_dir`,
    losslessly.

    Writes one CSV per captured table under `out_dir`:
      - statements.csv / referenced_vars.csv / call_edges.csv  -- the DEV-only
        bulk tables, every row.
      - address_versions.csv  -- the kcdx_id IS NULL subset ONLY (the bulk
        discovery rows; the curated rows are the curated export's job).

    Each CSV captures every SCHEMA column verbatim (the id PK, the FK integers, the
    dict-encoded ids as stored, the BLOB content_hash) with NULL distinguished from
    '' -- so run_rebuild (P1.3) reinserts them byte/value-identical. SEPARATE from
    csv_exporter.export_seeds (the curated half) -- this does not touch the curated
    export, and the two PARTITION address_versions by the kcdx_id seam.

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

    # address_versions: the kcdx_id-NULL (bulk discovery) subset ONLY. The curated
    # kcdx_id-NOT-NULL rows are exported by csv_exporter.export_seeds; the two
    # halves partition the table, so each av row is exported exactly once.
    av_name = BULK_CSV_NAMES[BULK_AV_TABLE]
    av_path = os.path.join(out_dir, av_name)
    written[av_name] = _export_one_table(
        con, BULK_AV_TABLE, av_path, where="kcdx_id IS NULL")

    return written
