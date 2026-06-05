"""seeds_shared -- the shared seed->DB core for import_to_sqlite.py.

One definition of the schema, the seed validators, the value/dict codec, and the
address_versions row-builder, shared by the full REBUILD path and the future
incremental `apply` path so the two writers cannot drift (db-updator Phase 1).

Private package (lives under the already-private data/refdata-extractor/ tree).

Re-exports the submodule surfaces so callers can `from seeds_shared import SCHEMA,
Dicts, read_module_seed, build_curated_row, ...` without reaching into submodules.
"""
from .schema import (
    SCHEMA,
    USER_COLUMNS,
    DEV_TABLES,
    USER_TABLES,
    DICT_COLS,
    EVIDENCE_KIND_ENUM,
    ADDRESS_KINDS,
    FUNCTION_KINDS,
)
from .dict_codec import (
    parse_int,
    hash_blob,
    Dicts,
)
from .validators import (
    read_module_seed,
    read_address_names_seed,
    read_address_versions_seed,
    authored_kind,
    resolve_and_check_name_refs,
    check_supersession_acyclic,
    check_kcdx_id_known,
    check_every_entity_covered,
    check_survival_derives_from_known,
)
from .row_builder import (
    build_bulk_row,
    build_curated_row,
)
from .survival_builder import (
    build_survival_row,
    survival_kind_form,
    folded_av_cells,
    FOLDED_SURVIVAL_COLS,
)
from .version_resolver import (
    resolve_version,
    VersionResolveError,
)
from .csv_exporter import (
    export_seeds,
)
from .round_trip import (
    round_trip,
    hash_curated_tables,
    RoundTripError,
)
from .seed_csv_edit import (
    update_row_in_place,
    append_row,
)
from .db_editor import (
    update_version_row,
    create_version,
    create_entity,
    supersede_entity,
    deprecate_entity,
    edit_notes,
    EDITABLE_VERSION_COLUMNS,
    DbEditError,
)
from .field_delta import (
    field_delta,
    is_new_version_nothing_changed,
)
from .read_api import (
    derive_status,
    read_curated_set,
    read_entity_detail,
    read_version_rows,
    read_modules,
    DbReadError,
)

# The deferred-commit surface (step 4a -- THE maintainer-tool write mechanism) lives
# in import_to_sqlite (it operates on apply_seeds' open connections), NOT in a
# seeds_shared submodule. seeds_shared MUST NOT import import_to_sqlite at import
# time (import_to_sqlite imports seeds_shared -- a top-level import here would cycle;
# see db_editor's lazy-import note). So these four names are re-exported LAZILY via
# __getattr__ (PEP 562): `from seeds_shared import DeferredCommit, commit, rollback,
# DeferredCommitError` resolves them from import_to_sqlite on FIRST access, after the
# module graph is built -- the backend (step 4b) imports them through this surface
# without a circular import. Additive: no existing name's binding changes.
_DEFERRED_COMMIT_NAMES = frozenset({
    "DeferredCommit", "DeferredCommitError", "commit", "rollback",
    # The D21 scoped restore-point (post-commit undo): the capture type + the entry
    # the backend (step 5) calls on a post-commit failure. Re-exported lazily for the
    # same no-cycle reason as commit/rollback (they live in import_to_sqlite).
    "RestorePoint", "restore",
})


def __getattr__(name):
    """PEP 562 lazy attribute resolution for the deferred-commit surface only. Any
    other unknown attribute is a real AttributeError (the normal behaviour)."""
    if name in _DEFERRED_COMMIT_NAMES:
        import import_to_sqlite as _imp
        return getattr(_imp, name)
    raise AttributeError(f"module {__name__!r} has no attribute {name!r}")

__all__ = [
    "SCHEMA", "USER_COLUMNS", "DEV_TABLES", "USER_TABLES", "DICT_COLS",
    "EVIDENCE_KIND_ENUM", "ADDRESS_KINDS", "FUNCTION_KINDS",
    "parse_int", "hash_blob", "Dicts",
    "read_module_seed", "read_address_names_seed", "read_address_versions_seed",
    "authored_kind",
    "resolve_and_check_name_refs", "check_supersession_acyclic",
    "check_kcdx_id_known", "check_every_entity_covered",
    "check_survival_derives_from_known",
    "build_bulk_row", "build_curated_row",
    "build_survival_row", "survival_kind_form",
    "folded_av_cells", "FOLDED_SURVIVAL_COLS",
    "resolve_version", "VersionResolveError",
    "export_seeds",
    "round_trip", "hash_curated_tables", "RoundTripError",
    "update_row_in_place", "append_row",
    "update_version_row", "create_version", "create_entity",
    "supersede_entity", "deprecate_entity", "edit_notes",
    "EDITABLE_VERSION_COLUMNS", "DbEditError",
    "field_delta", "is_new_version_nothing_changed",
    "derive_status", "read_curated_set", "read_entity_detail",
    "read_version_rows", "read_modules", "DbReadError",
    # Lazily re-exported from import_to_sqlite via __getattr__ (the deferred-commit
    # write mechanism -- step 4a): the handle type, its misuse error, and the
    # commit/rollback the maintainer-tool backend drives on confirm/cancel.
    "DeferredCommit", "DeferredCommitError", "commit", "rollback",
    # The D21 scoped restore-point (step 4d): the capture type + the post-commit-undo
    # entry the backend (step 5) calls when export/integrity/git fails after commit.
    "RestorePoint", "restore",
]
