"""seeds_shared.csv_exporter -- the DB->CSV half of the round-trip.

Given a reference DB, produce the three seed CSVs (module_seed.csv,
address_names_seed.csv, address_versions_seed.csv) deterministically and
diff-preserved -- the inverse of import_to_sqlite's seed-read side. The exporter
owns the diff-preservation contract (data/maintainer-tool/design.md S4, S5):

  - row order stable                (sorted by the row identity key the seed uses)
  - `#`-comment lines preserved verbatim in position
  - QUOTE_MINIMAL per cell          (the csv module default; policy.md S"File-format")
  - trailing-newline convention preserved
  - header-literal column order     (policy.md S"File-format": "Column order in the
                                     file MUST match the header literally")

DB<->CSV INFORMATION-EQUIVALENCE (design S4): the export emits every AUTHORED
column the CSV schema carries and invents no column the CSV does not. The CSV
column set is NOT the DB column set -- the importer reshapes on read (kcdx_id ==
address_names.id; the seed `module` name -> modules.id FK; the seed
`valid_from_version` tag -> game_versions.id FK; the `kind`/`evidence_kind`
strings -> _dict_* ids; the six survival_* cells -> the FOLDED address_versions
columns aob/anchor_string/rule/slot_count/expect_unique/derives_from -- D22 /
design §11.2, the former `survival` sibling table folded onto av columns).
This exporter inverts each reshape:

  module_seed.csv            <- modules                  (id, name, path)
  address_names_seed.csv     <- address_names            (id == kcdx_id; the
                                supersession/deprecation FK ids -> names/tags)
  address_versions_seed.csv  <- address_versions JOIN modules JOIN game_versions
                                (the curated rows; the survival_* CSV cells come
                                from the av row's FOLDED columns -- aob/anchor_string/
                                rule/slot_count/expect_unique/derives_from -- the
                                flat source of truth, the former `survival` sibling
                                table having been folded onto address_versions and
                                deleted. D22 / design S11.2.)

REUSE (no duplicated column knowledge):
  - seeds_shared.schema      -- USER_COLUMNS (the per-table column projection) +
                                DICT_COLS (which DB columns are dict-encoded).
  - seeds_shared.dict_codec  -- the inverse of the dict/value encode policy: the
                                _dict_<table>_<col> tables map id -> text, a BLOB
                                content_hash -> 64-hex, an INTEGER rva -> 0x-hex.

This module is HEADLESS and Qt-free (design S5: the data-core has no GUI
dependency). import_to_sqlite reuses it for any DB->CSV need.

The `value` address_versions column is intentionally NOT emitted to the CSV's
`value` cell: in the live importer the DB `value` is a derived mirror of
`vtable_slot` (row_builder.build_curated_row), and the seed `value` cell is empty
on every committed row -- so the faithful, round-trip-preserving export leaves the
CSV `value` cell as the seed authored it (the seed carries no independent `value`
datum today). See data/maintainer-tool/design.md S4 (information-equivalence:
a derived/cache column is forbidden on the authored surface) + the value mirror in
row_builder.build_curated_row.
"""
import csv
import os
import sqlite3

from .schema import USER_COLUMNS, DICT_COLS


# ---------------------------------------------------------------------------
# Canonical CSV header order per file (policy.md S"Required columns" + the
# committed seed headers). The export writes the header literally in this order;
# when an existing CSV is present its OWN header is honoured (diff-preservation:
# never reorder a maintainer's columns). These are the fallback when no prior
# file exists (a fresh export).
# ---------------------------------------------------------------------------
MODULE_CSV_HEADER = ["id", "name", "path"]

ADDRESS_NAMES_CSV_HEADER = [
    "id", "name",
    "superseded_by", "superseded_at_version",
    "is_deprecated", "deprecated_at_version", "deprecation_replacement",
    "notes",
]

ADDRESS_VERSIONS_CSV_HEADER = [
    "kcdx_id", "valid_from_version", "module", "rva", "kind", "signature",
    "last_verified_at_version", "verified_by", "verified_date", "evidence_kind",
    "survival_aob", "survival_anchor_string", "survival_derives_from",
    "survival_rule", "survival_slot_count", "survival_expect_unique",
    "value", "offset", "vtable_slot", "struct_offset",
    # valid_through_version: the interval-CLOSE column, now AUTHORED (D40 -- moved
    # off the bulk derived overlay onto the curated seed so an interval edit
    # round-trips via the human-reviewable seed). Appended at the END (append-only
    # discipline; existing positional column expectations are not disturbed). The
    # exporter emits the game_versions.tag of the row's valid_through FK; '' (NULL)
    # for an OPEN interval (the common case -- every baseline row is open).
    "valid_through_version",
]

# The three seed file names (the export targets under a seed dir).
MODULE_SEED_NAME = "module_seed.csv"
ADDRESS_NAMES_SEED_NAME = "address_names_seed.csv"
ADDRESS_VERSIONS_SEED_NAME = "address_versions_seed.csv"


# ---------------------------------------------------------------------------
# Inverse value codecs -- the read-side mirror of dict_codec's encode policy.
# (dict_codec encodes on the way IN; these decode on the way OUT. Kept here, not
# in dict_codec, because they read the DB's materialized _dict_* tables + apply
# the seed's textual conventions -- a DB-reading concern, not a pure codec.)
# ---------------------------------------------------------------------------
def _dict_id_to_val(con, table, col):
    """Map a _dict_<table>_<col> id -> its TEXT value (the inverse of
    Dicts.encode). Empty dict (table never materialized) -> {} so a NULL id
    decodes to ''."""
    tbl = '_dict_%s_%s' % (table, col)
    has = con.execute(
        "SELECT name FROM sqlite_master WHERE type='table' AND name=?",
        (tbl,)).fetchone()
    if not has:
        return {}
    return {row[0]: row[1] for row in con.execute('SELECT id, val FROM "%s"' % tbl)}


def _rva_to_text(v):
    """INTEGER rva -> the seed's `0x`-prefixed uppercase-hex, zero-padded to 8
    digits (policy.md S"File-format": rva SHOULD be hex with 0x prefix; the
    committed seeds use 0x%08X). NULL -> ''."""
    if v is None:
        return ""
    return "0x%08X" % v


def _hash_to_text(v):
    """BLOB content_hash -> 64-hex TEXT (the inverse of dict_codec.hash_blob).
    NULL -> ''. (No content_hash column appears in any seed CSV today; provided
    for completeness so a future authored-fingerprint column round-trips.)"""
    if v is None:
        return ""
    return bytes(v).hex()


def _int_to_text(v):
    """INTEGER -> its decimal text; NULL -> ''. The seed stores plain decimals
    for offset / vtable_slot / struct_offset / survival_slot_count /
    survival_expect_unique / is_deprecated."""
    return "" if v is None else str(v)


def _text(v):
    """TEXT cell -> str; NULL -> ''."""
    return "" if v is None else str(v)


# ---------------------------------------------------------------------------
# Diff-preserving CSV write: header-literal column order, #-comments preserved in
# position, QUOTE_MINIMAL, trailing-newline + line-terminator matched to the
# existing file.
# ---------------------------------------------------------------------------
def _existing_format(path):
    """Inspect an existing CSV to preserve its diff-shape on rewrite. Returns
    (header_order, comment_positions, line_terminator, trailing_newline):

      header_order        -- the file's literal header column list (None if the
                             file is absent -> the caller uses the canonical order)
      comment_positions   -- list of (data_row_index_before_which, raw_comment_line)
                             so each `#`-line is re-emitted verbatim in position.
                             A comment before the header sits at index -1 (emitted
                             before the header); index 0 = before the first data
                             row; index k = before the k-th data row.
      line_terminator     -- '\\r\\n' or '\\n' (matched to the existing file)
      trailing_newline    -- whether the file ends with its line terminator
    """
    if not os.path.isfile(path):
        return None, [], "\r\n", True
    with open(path, "rb") as f:
        raw = f.read()
    crlf = b"\r\n" in raw
    line_terminator = "\r\n" if crlf else "\n"
    trailing_newline = raw.endswith(b"\n")
    text = raw.decode("utf-8")
    # Split into lines WITHOUT their terminators, preserving content.
    lines = text.split("\n")
    if lines and lines[-1] == "":
        lines = lines[:-1]   # drop the empty element a trailing newline produces
    # Strip a trailing '\r' each line carries under CRLF.
    lines = [ln[:-1] if ln.endswith("\r") else ln for ln in lines]

    comments = []
    header_order = None
    data_idx = 0
    seen_header = False
    for ln in lines:
        if ln.lstrip().startswith("#"):
            comments.append(((data_idx if seen_header else -1), ln))
            continue
        if not seen_header:
            header_order = next(csv.reader([ln]))
            seen_header = True
            continue
        data_idx += 1
    return header_order, comments, line_terminator, trailing_newline


def _write_csv(path, header, rows, *, line_terminator="\r\n",
               comments=None, trailing_newline=True):
    """Write `rows` (list of dicts keyed by `header` column names) to `path` as a
    QUOTE_MINIMAL CSV with the given header order, preserving `#`-comment lines in
    position and the line-terminator + trailing-newline convention.

    rows are already in the deterministic export order (the caller sorts). Each
    cell is a str (the inverse codecs above produced ''-for-NULL); csv writes them
    QUOTE_MINIMAL (the default), so a cell containing a comma/quote/newline is
    quoted, everything else bare -- matching the seed's authored quoting."""
    comments = comments or []
    # Bucket comments by the data-row index they precede (-1 == before header).
    pre_header = [c for (idx, c) in comments if idx == -1]
    by_idx = {}
    for (idx, c) in comments:
        if idx >= 0:
            by_idx.setdefault(idx, []).append(c)

    # Render every data line to a string via csv.writer (so quoting matches the
    # csv module's QUOTE_MINIMAL exactly), then assemble with comments + the
    # chosen terminator. Using a manual assembly (not writer.writerows directly)
    # lets us interleave the verbatim comment lines.
    import io

    def render(fields):
        buf = io.StringIO()
        w = csv.writer(buf, lineterminator="")   # we add the terminator ourselves
        w.writerow(fields)
        return buf.getvalue()

    out_lines = []
    out_lines.extend(pre_header)
    out_lines.append(render(header))
    for i, row in enumerate(rows):
        for c in by_idx.get(i, []):
            out_lines.append(c)
        out_lines.append(render([row.get(col, "") for col in header]))
    # Any comments positioned after the last data row (idx == len(rows)).
    for c in by_idx.get(len(rows), []):
        out_lines.append(c)

    body = line_terminator.join(out_lines)
    if trailing_newline:
        body += line_terminator
    # newline="" so the csv-rendered cells keep their content untouched and our
    # explicit terminator is written verbatim (no platform translation).
    with open(path, "w", encoding="utf-8", newline="") as f:
        f.write(body)


# ---------------------------------------------------------------------------
# Per-table DB readers -> CSV row dicts. Each reads the curated table(s), inverts
# the importer's reshape, and emits a dict keyed by the CSV header columns.
# ---------------------------------------------------------------------------
def _export_modules(con):
    """modules -> module_seed.csv rows. Direct 1:1 (id, name, path), sorted by
    id (the seed's row identity)."""
    cols = USER_COLUMNS["modules"]   # ["id", "name", "path"]
    out = []
    for row in con.execute(
            'SELECT %s FROM modules ORDER BY id'
            % ",".join('"%s"' % c for c in cols)):
        r = dict(zip(cols, row))
        out.append({
            "id": _int_to_text(r["id"]),
            "name": _text(r["name"]),
            "path": _text(r["path"]),
        })
    return out


def _export_address_names(con):
    """address_names -> address_names_seed.csv rows. The DB stores the
    supersession/deprecation references as FK ids (superseded_by/
    deprecation_replacement -> address_names.id; superseded_at_version/
    deprecated_at_version -> game_versions.id); the seed carries the NAME / TAG
    strings (resolve_and_check_name_refs resolved them on import). Invert: id ->
    name, version-id -> tag. is_deprecated is 0/1; the seed mirrors it.

    Sorted by id (== kcdx_id), the seed's stable row identity."""
    name_by_id = {row[0]: row[1] for row in con.execute(
        "SELECT id, name FROM address_names")}
    tag_by_id = {row[0]: row[1] for row in con.execute(
        "SELECT id, tag FROM game_versions")}

    cols = USER_COLUMNS["address_names"]
    out = []
    for row in con.execute(
            'SELECT %s FROM address_names ORDER BY id'
            % ",".join('"%s"' % c for c in cols)):
        r = dict(zip(cols, row))
        out.append({
            "id": _int_to_text(r["id"]),
            "name": _text(r["name"]),
            # FK id -> the seed's name string (None -> '').
            "superseded_by": _text(name_by_id.get(r["superseded_by"])
                                   if r["superseded_by"] is not None else None),
            "superseded_at_version": _text(
                tag_by_id.get(r["superseded_at_version"])
                if r["superseded_at_version"] is not None else None),
            # is_deprecated: the seed writes '1' when set, '' when 0/NULL (the
            # importer reads '1'/'true'/'yes' -> 1; the committed seed uses ''
            # for not-deprecated, matching a 0/NULL DB value).
            "is_deprecated": ("1" if r["is_deprecated"] else ""),
            "deprecated_at_version": _text(
                tag_by_id.get(r["deprecated_at_version"])
                if r["deprecated_at_version"] is not None else None),
            "deprecation_replacement": _text(
                name_by_id.get(r["deprecation_replacement"])
                if r["deprecation_replacement"] is not None else None),
            "notes": _text(r["notes"]),
        })
    return out


def _export_address_versions(con):
    """address_versions (curated only) JOIN modules/game_versions ->
    address_versions_seed.csv rows. Inverts every importer reshape:

      kcdx_id            <- address_versions.kcdx_id (curated rows only)
      valid_from_version <- game_versions.tag via valid_from FK
      module             <- modules.name via module_id FK
      rva                <- INTEGER -> 0x%08X hex
      kind               <- _dict_address_versions_kind id -> text
      signature          <- TEXT direct
      last_verified_at_version <- game_versions.tag via the FK
      verified_by/_date  <- TEXT direct
      evidence_kind      <- _dict_address_versions_evidence_kind id -> text
      survival_*         <- the FOLDED av-row columns (D22 / design S11.2 -- the
                            survival sibling table folded INTO address_versions; the
                            av row is the flat source of truth). The CSV columns keep
                            their `survival_`-prefixed names (the existing export
                            contract; the importer reads them into the same av-row
                            folded columns) but their SOURCE is now the av row's
                            aob/anchor_string/rule/slot_count/expect_unique/
                            derives_from columns -- NOT the survival table. This is
                            the fold-forward source: when the survival table is
                            deleted (Phase 3 step 6) this export is unchanged. The av
                            folded cells equal the survival row's cells row-for-row by
                            construction (the 157/157 equivalence -- one per-kind
                            dispatch, two write targets; survival_builder.
                            folded_av_cells), so the byte-identical re-export proves
                            the source switch is lossless. survival_derives_from <-
                            reverse-map the av row's derives_from av_id -> its kcdx_id
                            (the av `derives_from` column carries the SAME resolved
                            av_id the survival table's `derives_from` did -- the CSV
                            carries the kcdx_id, so the export must invert it back).
      offset/vtable_slot/struct_offset <- INTEGER -> decimal text
      valid_through_version <- game_versions.tag via the valid_through FK (D40 --
                            the interval-CLOSE column is now AUTHORED, so the
                            exporter emits it onto the seed; NULL valid_through =
                            an OPEN interval -> '' in the CSV, the common case).
      value              <- the seed's authored `value` cell, which is empty on
                            every committed row; DB `value` is a derived mirror of
                            vtable_slot (row_builder), NOT an independent datum, so
                            it is NOT exported to the CSV `value` cell (else the
                            round-trip would diverge -- the committed cell is ''
                            while DB value == vtable_slot for vtable_index rows).
                            See the module docstring + design.md S4.

    Curated == address_versions.kcdx_id IS NOT NULL (the USER DB already filters
    to these; the WHERE makes the exporter correct against the DEV DB too).
    Sorted by (kcdx_id, valid_from_version-tag) -- the seed's row identity."""
    kind_decode = _dict_id_to_val(con, "address_versions", "kind")
    ek_decode = _dict_id_to_val(con, "address_versions", "evidence_kind")
    module_name = {row[0]: row[1] for row in con.execute(
        "SELECT id, name FROM modules")}
    gv_tag = {row[0]: row[1] for row in con.execute(
        "SELECT id, tag FROM game_versions")}
    av_to_kcdx = {row[0]: row[1] for row in con.execute(
        "SELECT id, kcdx_id FROM address_versions")}

    rows = []
    for r in con.execute(
            "SELECT id, kcdx_id, kind, module_id, rva, signature, "
            "last_verified_at_version, verified_by, verified_date, evidence_kind, "
            "offset, vtable_slot, struct_offset, valid_from, "
            "aob, anchor_string, rule, slot_count, expect_unique, derives_from, "
            "valid_through "
            "FROM address_versions WHERE kcdx_id IS NOT NULL"):
        (av_id, kcdx_id, kind_id, module_id, rva, signature, lvv_id, vby, vdt,
         ek_id, offset, vslot, struct_offset, valid_from_id,
         fold_aob, fold_anchor, fold_rule, fold_slot, fold_eu, fold_df_av,
         valid_through_id) = r

        # survival_derives_from CSV cell: the av row's derives_from is the resolved
        # av_id (same value the survival table carried); the CSV carries the
        # dependency entity's kcdx_id, so invert av_id -> kcdx_id.
        df_kid = av_to_kcdx.get(fold_df_av) if fold_df_av is not None else None

        rows.append({
            "_sort": (kcdx_id, gv_tag.get(valid_from_id) or ""),
            "kcdx_id": _int_to_text(kcdx_id),
            "valid_from_version": _text(gv_tag.get(valid_from_id)),
            "module": _text(module_name.get(module_id)),
            "rva": _rva_to_text(rva),
            "kind": _text(kind_decode.get(kind_id)),
            "signature": _text(signature),
            "last_verified_at_version": _text(gv_tag.get(lvv_id)
                                              if lvv_id is not None else None),
            "verified_by": _text(vby),
            "verified_date": _text(vdt),
            "evidence_kind": _text(ek_decode.get(ek_id)
                                   if ek_id is not None else None),
            # The `survival_*` CSV columns now source the FOLDED av-row columns
            # (D22 / design S11.2 -- the av row is the flat source). Names unchanged
            # (the export contract); source switched off the survival table so the
            # export survives that table's deletion (Phase 3 step 6).
            "survival_aob": _text(fold_aob),
            "survival_anchor_string": _text(fold_anchor),
            "survival_derives_from": _int_to_text(df_kid),
            "survival_rule": _text(fold_rule),
            "survival_slot_count": _int_to_text(fold_slot),
            "survival_expect_unique": _int_to_text(fold_eu),
            # `value`: NOT emitted from the DB (derived mirror; see docstring).
            "value": "",
            "offset": _int_to_text(offset),
            "vtable_slot": _int_to_text(vslot),
            "struct_offset": _int_to_text(struct_offset),
            # valid_through_version: the interval-CLOSE FK -> its game_versions.tag;
            # NULL (an OPEN interval) -> '' (D40, the authored interval column).
            "valid_through_version": _text(gv_tag.get(valid_through_id)
                                           if valid_through_id is not None else None),
        })

    rows.sort(key=lambda x: x["_sort"])
    for x in rows:
        del x["_sort"]
    return rows


# ---------------------------------------------------------------------------
# Public API.
# ---------------------------------------------------------------------------
def export_seeds(db_path, seed_dir):
    """Export the three seed CSVs from `db_path` into `seed_dir`, diff-preserved.

    `db_path` -- a reference DB (the USER reference.sqlite carries the full
                 curated set; the DEV reference-dev.sqlite works too -- the
                 curated WHERE filters bulk rows). The three curated tables read
                 are modules / address_names / address_versions (+ game_versions
                 and the _dict_* lookups for decoding; the survival/re-find cells
                 are folded columns ON address_versions -- D22 / design §11.2).
    `seed_dir` -- the directory the three CSVs are written into. When a target CSV
                 already exists, its header order + `#`-comment positions + line
                 terminator + trailing-newline are preserved (diff-preservation);
                 otherwise the canonical header order is used.

    Returns the dict {filename: row_count} written.
    """
    con = sqlite3.connect(db_path)
    try:
        return _export_all(con, seed_dir)
    finally:
        con.close()


def _export_all(con, seed_dir):
    os.makedirs(seed_dir, exist_ok=True)
    written = {}
    for name, canonical_header, reader in (
            (MODULE_SEED_NAME, MODULE_CSV_HEADER, _export_modules),
            (ADDRESS_NAMES_SEED_NAME, ADDRESS_NAMES_CSV_HEADER,
             _export_address_names),
            (ADDRESS_VERSIONS_SEED_NAME, ADDRESS_VERSIONS_CSV_HEADER,
             _export_address_versions)):
        path = os.path.join(seed_dir, name)
        header_order, comments, terminator, trailing = _existing_format(path)
        header = header_order or canonical_header
        rows = reader(con)
        _write_csv(path, header, rows, line_terminator=terminator,
                   comments=comments, trailing_newline=trailing)
        written[name] = len(rows)
    return written
