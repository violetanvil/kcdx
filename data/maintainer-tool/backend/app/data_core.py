"""app.data_core -- the data-core import seam (design D13 / S5).

The ONE place the backend reaches into the headless data-core. The backend wraps
the LANDED `seeds_shared` package in-process (the data-core is Python -> a direct
import, no subprocess; D14). Every validation / SQL / export / authoring RULE is
the data-core's (D13 / R3 / S5 law: "NO validation, SQL, or export logic in the
backend -- it calls the data-core"). This module:

  1. puts the data-core's python dir on sys.path (the data-core is a private
     package under data/refdata-extractor/python -- not pip-installed), and
  2. re-exports the public surface the backend calls.

It holds NO rule logic of its own -- it is a thin import shim so the rest of the
backend imports the data-core from one seam (and a later step can swap how the
data-core is located without touching every call site).
"""
import os
import sys

# The data-core lives at <repo>/data/refdata-extractor/python (a private package,
# not pip-installed). From backend/app/data_core.py: up 4 = repo root, then the
# python dir. Putting it on sys.path is the in-process import seam (D14).
_HERE = os.path.dirname(os.path.abspath(__file__))
_REPO_ROOT = os.path.normpath(os.path.join(_HERE, "..", "..", "..", ".."))
_DATA_CORE_PYDIR = os.path.join(_REPO_ROOT, "data", "refdata-extractor", "python")

if _DATA_CORE_PYDIR not in sys.path:
    sys.path.insert(0, _DATA_CORE_PYDIR)

# Re-export the data-core public surface the backend uses. The backend CALLS
# these; it never reimplements any of them (D13/R3). Listed explicitly so the
# seam's surface is auditable -- a backend that needs a new data-core function
# adds it here, in one place.
import seeds_shared as _seeds_shared  # noqa: E402
from seeds_shared import csv_exporter as _csv_exporter  # noqa: E402

# The write/edit entry points (db_editor) -- the six jobs the later steps wire to
# endpoints. Each takes (out_dir, dll_path, ...); the dll_path adapter (app.adapter)
# owns mapping a version tag to what the data-core needs in place of a DLL.
update_version_row = _seeds_shared.update_version_row
# The D32 batch save-spine: N validated UPDATEs as ONE atomic transaction (all-or-
# nothing -- one row failing rolls back the whole batch, D21). Composes the single-edit
# primitive (one fold of all N edits onto ONE prospective seed -> ONE held DeferredCommit
# -> ONE commit); the /confirm/batch endpoint drives it.
update_version_rows_batch = _seeds_shared.update_version_rows_batch
create_version = _seeds_shared.create_version
create_entity = _seeds_shared.create_entity
supersede_entity = _seeds_shared.supersede_entity
deprecate_entity = _seeds_shared.deprecate_entity
edit_notes = _seeds_shared.edit_notes
DbEditError = _seeds_shared.DbEditError

# The read-for-display surface (read_api) the backend's read endpoints call
# (step 2b). read_curated_set / read_entity_detail / read_version_rows return the
# already-derived rows the frontend binds; read_modules returns the module registry
# (the s04 `module` Select source); DbReadError signals a missing curated DB (the
# read-path counterpart to /health's empty/error state). The backend CALLS these and
# serializes the result -- it derives no status and runs no SQL (D13/R3).
read_curated_set = _seeds_shared.read_curated_set
read_entity_detail = _seeds_shared.read_entity_detail
read_version_rows = _seeds_shared.read_version_rows
read_modules = _seeds_shared.read_modules
DbReadError = _seeds_shared.DbReadError

# The bulk re-verify RESOLVE seam (D39): the data-core computes the per-row re-verify
# edit-specs FROM the v3 report ({kcdx_id, valid_from_version, edits} -- the shape
# /confirm/batch consumes), reading the matched row by matched_address_version_id
# (verify-all) or the interval-containing row of kcdx_id+version (close-intervals). NO
# WRITE -- it reads READ-ONLY + computes; the batch write path transacts (D19/law 6).
# The /save/reverify-batch PREVIEW endpoint calls it; ReverifyResolveError signals a
# structural report-vs-DB mismatch (a stale matched id, a missing close-target).
resolve_reverify_batch = _seeds_shared.resolve_reverify_batch
ReverifyResolveError = _seeds_shared.ReverifyResolveError

# The export + round-trip + field-delta surface the save spine uses (later steps).
export_seeds = _seeds_shared.export_seeds
round_trip = _seeds_shared.round_trip
RoundTripError = _seeds_shared.RoundTripError
field_delta = _seeds_shared.field_delta
is_new_version_nothing_changed = _seeds_shared.is_new_version_nothing_changed

# The deferred-commit surface (step 4a -- THE maintainer-tool write mechanism) the
# step-5 CONFIRM endpoint drives. Confirm calls a db_editor write with
# defer_commit=True and gets back a DeferredCommit HANDLE (the two open, uncommitted
# connections + the apply result), then commits via commit(handle) on success /
# discards via rollback(handle) on failure -- the WHOLE transaction inside the one
# Confirm request (Save-previews/Confirm-transacts; nothing held across think-time).
# Step 4b's Save endpoints do NOT use this -- a Save is preview-only (validate_only,
# NO write); these names exist on the seam for step 5. They live in import_to_sqlite
# (they operate on apply_seeds' open connections) and are re-exported from seeds_shared
# LAZILY (PEP 562 __getattr__) to avoid the import cycle -- accessed as attributes here
# so the lazy resolution fires on first use, not at import time.
DeferredCommit = _seeds_shared.DeferredCommit
DeferredCommitError = _seeds_shared.DeferredCommitError
commit = _seeds_shared.commit
rollback = _seeds_shared.rollback

# The D21 SCOPED restore-point (step 4d -- the POST-commit half of the robust
# rollback). commit(handle) is one-way (it COMMITs + closes both connections, so the
# deferred rollback above is gone after it) AND the export runs post-commit (it reads
# the committed DB on a fresh connection). A failure THERE (export/integrity/git) is
# undone by restore(handle): it RE-OPENS both DBs, restores the touched rows + each DB's
# sqlite_sequence from the capture the handle carries (apply_direct_edit captured it
# before the commit), byte-identical incl. PK. It restores DB ROWS + sequence ONLY -- the
# data/db-export/ CSVs are a backend FILE artifact (D20) the backend reverts itself (the
# CSV-revert split, D13/law 6). Lazily re-exported from seeds_shared (PEP 562) for the
# same no-import-cycle reason as commit/rollback.
restore = _seeds_shared.restore

# The two authored-column-order headers field_delta takes as its `field_order` arg
# (csv_exporter owns them; seeds_shared.__init__ does not re-export them). The
# field-delta endpoint (routes_delta) re-orders the delta deterministically by these
# (design §7 layout stability) -- version-row vs lifecycle(names)-row edit. Re-exported
# here so the whole data-core surface the backend touches lives on this one seam.
ADDRESS_VERSIONS_CSV_HEADER = _csv_exporter.ADDRESS_VERSIONS_CSV_HEADER
ADDRESS_NAMES_CSV_HEADER = _csv_exporter.ADDRESS_NAMES_CSV_HEADER

# The version resolver (the data-core's DLL .rdata scan). The backend does NOT
# call this server-side (no DLL); it is re-exported so the version-tag adapter +
# the cross-implementation test-of-record (a later P4 step) reference the SAME
# resolver the data-core uses, not a backend copy (D15).
resolve_version = _seeds_shared.resolve_version
VersionResolveError = _seeds_shared.VersionResolveError


def data_core_pydir():
    """The data-core python dir this seam put on sys.path (for diagnostics /
    the health endpoint to confirm the seam resolved)."""
    return _DATA_CORE_PYDIR
