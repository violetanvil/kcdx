"""seeds_shared.field_delta -- the saved-vs-prospective field delta (design D8).

WHAT THIS IS (design data/maintainer-tool/design.md S10 D8 + S7 + S6 US-4;
ui/screens/s06-save-confirm.md)
-----------------------------------------------------------------------------
The headless, PURE computation behind the GUI's save-confirm surface (D8). Given a
SAVED record and the prospective EDITED record -- the data-core's existing seed-row
dict shape ({column: cell_string}, '' for a NULL/empty cell, the form csv_exporter
emits and db_editor reads + the GUI passes) -- it produces the set of CHANGED
fields as `field -> (old, new)`. That set IS the human's acceptance signal: the
maintainer reads `last_verified_at_version: 1.4 -> 1.5`, never a CSV diff (the
literal CSV diff is oracle-verified and lands in the commit for a reviewer, invisible
here -- design S7 + D8). An UNCHANGED field is ABSENT from the delta.

It covers every edit shape over two record dicts -- a version-row UPDATE (US-3/US-5),
an INSERT (the saved side empty -> an all-fields delta), a lifecycle UPDATE (US-8) --
because a delta over two dicts is shape-agnostic. It also computes the D12
"nothing changed" verdict for a NEW version (the prospective new row is identical to
its source except valid_from_version -> the GUI steers the maintainer to re-verify
the existing row instead of minting a duplicate).

PURE -- no I/O, no DB, no Qt, no validation. It DESCRIBES a change; the shared
validator GATES it elsewhere (db_editor -> import_to_sqlite.apply_seeds). A delta is
not an assertion that the change is legal, only a statement of what differs.

DETERMINISM (the load-bearing property -- design S7 layout stability; the GUI lists
the rows in a fixed order)
-----------------------------------------------------------------------------
The delta is an OrderedDict in a STABLE field order: the authored column order the
seed CSV header carries (csv_exporter.ADDRESS_VERSIONS_CSV_HEADER /
ADDRESS_NAMES_CSV_HEADER), then any remaining key sorted -- never Python dict
insertion order of the caller's two dicts (which would make the same change render
its fields in different orders on different calls). Cell values render as the dicts
carry them (the seed-cell strings: '' for empty/NULL, the value text otherwise), so
None and '' are treated as the SAME empty cell (an edit that only swaps None<->'' is
NOT a change -- both are an empty cell).

NOTHING-CHANGED SINGLE-SOURCE (no duplication with db_editor)
-------------------------------------------------------------
db_editor.create_version needs the SAME "identical except valid_from_version"
verdict for its own AP18 D12 signal. The pure two-dict equality primitive lives
HERE (is_new_version_nothing_changed); db_editor._new_version_nothing_changed
DELEGATES to it per existing source row (db_editor still owns reading the exported
seed for the entity's rows -- an I/O concern -- and the version-row authored column
set; field_delta owns the pure dict-vs-dict comparison both share). The verdict is
defined as: the field delta between the two records, IGNORING valid_from_version, is
EMPTY.
"""
from collections import OrderedDict

from .csv_exporter import (
    ADDRESS_VERSIONS_CSV_HEADER,
    ADDRESS_NAMES_CSV_HEADER,
)

# The identity-key column of a version row that distinguishes a NEW version from
# its source by construction -- the one column the D12 "nothing changed" verdict
# ignores (a new version is a copy at a new valid_from_version; the question is
# whether ANYTHING ELSE differs).
_VERSION_KEY_COLUMN = "valid_from_version"


def _cell(value):
    """A record value as its comparable/renderable cell string: None -> ''
    (an empty/NULL cell), else str(value). Mirrors the seed-cell convention
    (seed_csv_edit / csv_exporter: '' is a NULL cell) so a field whose value is
    None in one record and '' in the other is the SAME empty cell, not a change."""
    return "" if value is None else str(value)


def _ordered_fields(saved, prospective, field_order):
    """The fields to compare, in the deterministic order: the columns named in
    `field_order` (the authored column order) that appear in either record FIRST,
    then any remaining key present in either record, sorted. This makes the delta's
    field order a stable function of the data, never of caller dict-insertion
    order."""
    keys = set(saved) | set(prospective)
    ordered = [c for c in field_order if c in keys]
    seen = set(ordered)
    ordered.extend(sorted(k for k in keys if k not in seen))
    return ordered


def field_delta(saved, prospective, *, field_order=ADDRESS_VERSIONS_CSV_HEADER,
                ignore=()):
    """The changed-field delta between a SAVED record dict and the prospective
    EDITED record dict (design D8). Returns an OrderedDict {field: (old, new)} in
    deterministic field order, holding ONLY the fields whose cell value differs; an
    unchanged field (including a None-vs-'' no-op) is ABSENT.

    Parameters:
      saved        -- the record as currently saved ({column: value}); the seed-row
                      dict shape (cell strings, '' for NULL) the data-core uses. For
                      an INSERT the saved side is empty ({} or a dict of empty cells)
                      -> the delta is all-fields-from-empty.
      prospective  -- the record after the maintainer's edit, same dict shape.
      field_order  -- the authored column order for deterministic field ordering;
                      defaults to the address_versions seed header (the version-row
                      shape this surface most often renders). Pass
                      ADDRESS_NAMES_CSV_HEADER for a lifecycle (names-row) edit. A
                      key not in `field_order` is ordered after the named columns,
                      sorted.
      ignore       -- field names to exclude from the delta entirely (e.g. an
                      identity key the surface never shows as a change). Empty by
                      default.

    `old`/`new` are the cell STRINGS (the seed-cell rendering: '' for empty/NULL,
    the value text otherwise) -- so the GUI renders `field  <old> -> <new>` directly
    and the rendering of empty-vs-value is stable. This is a pure description; it
    performs no validation (the validator gates the change elsewhere).
    """
    ignore = set(ignore)
    delta = OrderedDict()
    for field in _ordered_fields(saved, prospective, field_order):
        if field in ignore:
            continue
        old = _cell(saved.get(field))
        new = _cell(prospective.get(field))
        if old != new:
            delta[field] = (old, new)
    return delta


def is_new_version_nothing_changed(saved, prospective):
    """D12: True IFF the prospective NEW version record is identical to the SAVED
    source record on every authored column EXCEPT valid_from_version -- i.e. the
    field delta between them, ignoring valid_from_version, is EMPTY. A new version
    that changes nothing carries no new information; the GUI steers the maintainer
    to re-verify the existing row instead of minting a duplicate (design D12).

    Pure two-dict comparison over the version-row authored columns; the
    single-source primitive db_editor._new_version_nothing_changed delegates to
    (db_editor owns reading the entity's existing rows from the exported seed; this
    owns the dict-vs-dict equality). Both records are the seed-row dict shape (cell
    strings, '' for NULL); a None-vs-'' difference is NOT a change.
    """
    return not field_delta(
        saved, prospective,
        field_order=ADDRESS_VERSIONS_CSV_HEADER,
        ignore=(_VERSION_KEY_COLUMN,))
