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

# The write/edit entry points (db_editor) -- the six jobs the later steps wire to
# endpoints. Each takes (out_dir, dll_path, ...); the dll_path adapter (app.adapter)
# owns mapping a version tag to what the data-core needs in place of a DLL.
update_version_row = _seeds_shared.update_version_row
create_version = _seeds_shared.create_version
create_entity = _seeds_shared.create_entity
supersede_entity = _seeds_shared.supersede_entity
deprecate_entity = _seeds_shared.deprecate_entity
DbEditError = _seeds_shared.DbEditError

# The read-for-display surface (read_api) the backend's read endpoints call
# (step 2b). read_curated_set / read_entity_detail / read_version_rows return the
# already-derived rows the frontend binds; DbReadError signals a missing curated DB
# (the read-path counterpart to /health's empty/error state). The backend CALLS
# these and serializes the result -- it derives no status and runs no SQL (D13/R3).
read_curated_set = _seeds_shared.read_curated_set
read_entity_detail = _seeds_shared.read_entity_detail
read_version_rows = _seeds_shared.read_version_rows
DbReadError = _seeds_shared.DbReadError

# The export + round-trip + field-delta surface the save spine uses (later steps).
export_seeds = _seeds_shared.export_seeds
round_trip = _seeds_shared.round_trip
RoundTripError = _seeds_shared.RoundTripError
field_delta = _seeds_shared.field_delta
is_new_version_nothing_changed = _seeds_shared.is_new_version_nothing_changed

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
