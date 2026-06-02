"""seeds_shared.seed_csv_edit -- a diff-preserving one-row edit of a seed CSV.

ONE job: overwrite a set of cells on the single row of a seed CSV identified by an
identity key, preserving the file's diff shape (row order, the other rows' bytes,
`#`-comment positions, header-literal column order, the line-terminator +
trailing-newline convention). It is the in-place complement to csv_exporter's
DB->CSV write: db_editor exports the current DB to a temp seed, then calls this to
fold the GUI's prospective one-row edit into that temp seed before driving the
applier.

It reuses csv_exporter's diff-preserving primitives (_existing_format +
_write_csv) -- it re-implements NO CSV-format knowledge. The only logic here is
finding the keyed row and overwriting its edited cells; everything about HOW the
file is shaped on disk is csv_exporter's, shared.

Writes only the file it is handed (db_editor hands it a temp-dir path -- never a
file under data/seeds/).
"""
import csv

from .csv_exporter import _existing_format, _write_csv


def update_row_in_place(csv_path, key_columns, key_values, edits):
    """Overwrite the cells named in `edits` on the ONE row of the seed CSV at
    `csv_path` whose `key_columns` cells equal `key_values`, rewriting the file
    diff-preserved. Returns True if a row matched (and was edited), False if no row
    matched the key.

    Parameters:
      csv_path    -- the seed CSV to edit in place (a temp-dir path).
      key_columns -- tuple of column names forming the row's identity key
                     (e.g. ("kcdx_id", "valid_from_version")).
      key_values  -- tuple of the key cell STRINGS to match (same order as
                     key_columns); compared after .strip() on both sides so a
                     key's surrounding whitespace does not defeat the match.
      edits       -- {column: new_value_string} to set on the matched row. Every
                     column must already exist in the file header (the caller --
                     db_editor -- restricts edits to known seed columns); a column
                     absent from the header raises KeyError (a caller bug, surfaced
                     loudly rather than silently dropping the edit).

    Exactly-one-match contract: the seed's identity keys are unique (the importer
    fails loud on a duplicate (kcdx_id, valid_from_version)), so at most one row
    matches; this edits the first match and returns True. A multi-match would mean
    an already-invalid seed -- not this function's job to detect (the validator
    owns uniqueness); it edits the first and lets the downstream validator catch
    the duplicate.
    """
    header_order, comments, terminator, trailing = _existing_format(csv_path)
    if header_order is None:
        raise FileNotFoundError(
            f"{csv_path}: no existing seed CSV to edit (export must run first)")

    for col in edits:
        if col not in header_order:
            raise KeyError(
                f"{csv_path}: edit names column {col!r} absent from the file "
                f"header {header_order}")

    # Read the data rows (comments + header are reconstructed by _write_csv from
    # the format probe above; csv.DictReader skips '#'-comment lines the same way
    # the seed readers do, so the data-row set here matches what _write_csv emits).
    with open(csv_path, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rows = [dict(r) for r in csv.DictReader(lines)]

    key_target = tuple(str(v).strip() for v in key_values)
    matched = False
    for r in rows:
        rkey = tuple((r.get(c) or "").strip() for c in key_columns)
        if rkey == key_target:
            for col, val in edits.items():
                r[col] = "" if val is None else str(val)
            matched = True
            break

    if not matched:
        return False

    _write_csv(csv_path, header_order, rows, line_terminator=terminator,
               comments=comments, trailing_newline=trailing)
    return True


def append_row(csv_path, cells):
    """Append ONE new data row to the seed CSV at `csv_path`, rewriting the file
    diff-preserved (the existing rows' bytes, `#`-comment positions, header-literal
    column order, line-terminator + trailing-newline convention all untouched; the
    new row is the only added line, after the last existing data row). Returns the
    appended row dict (the header-keyed cells as written).

    The in-place complement to update_row_in_place for the INSERT shapes (a new
    entity / new version row): db_editor exports the current DB to a temp seed, then
    calls this to fold the prospective NEW row into that temp seed before driving
    the applier. It re-implements NO CSV-format knowledge -- it reuses csv_exporter's
    _existing_format + _write_csv, the same diff-preserving primitives the export and
    the update share.

    Parameters:
      csv_path -- the seed CSV to append to (a temp-dir path; never under
                  data/seeds/).
      cells    -- {column: value} for the new row. Every key must already exist in
                  the file header (the caller -- db_editor -- restricts cells to
                  known seed columns); a key absent from the header raises KeyError
                  (a caller bug, surfaced loudly). A column the file has but `cells`
                  omits is written as '' (an empty/NULL cell), matching how the
                  importer reads a blank cell. A None value is written as ''.

    Row-content validity (a duplicate identity key, a missing REQUIRED column, an
    unresolvable FK) is NOT this function's concern -- it appends the row verbatim
    and lets the downstream shared validator (apply_seeds' gate) reject an invalid
    resulting seed state. This function only knows how to place a row on disk
    diff-preserved; the rules are the validator's.
    """
    header_order, comments, terminator, trailing = _existing_format(csv_path)
    if header_order is None:
        raise FileNotFoundError(
            f"{csv_path}: no existing seed CSV to append to (export must run first)")

    for col in cells:
        if col not in header_order:
            raise KeyError(
                f"{csv_path}: append names column {col!r} absent from the file "
                f"header {header_order}")

    with open(csv_path, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    rows = [dict(r) for r in csv.DictReader(lines)]

    new_row = {c: "" for c in header_order}
    for col, val in cells.items():
        new_row[col] = "" if val is None else str(val)
    rows.append(new_row)

    _write_csv(csv_path, header_order, rows, line_terminator=terminator,
               comments=comments, trailing_newline=trailing)
    return new_row
