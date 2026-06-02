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
    SURVIVAL_KIND_FORMS,
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

__all__ = [
    "SCHEMA", "USER_COLUMNS", "DEV_TABLES", "USER_TABLES", "DICT_COLS",
    "EVIDENCE_KIND_ENUM", "ADDRESS_KINDS", "FUNCTION_KINDS", "SURVIVAL_KIND_FORMS",
    "parse_int", "hash_blob", "Dicts",
    "read_module_seed", "read_address_names_seed", "read_address_versions_seed",
    "authored_kind",
    "resolve_and_check_name_refs", "check_supersession_acyclic",
    "check_kcdx_id_known", "check_every_entity_covered",
    "check_survival_derives_from_known",
    "build_bulk_row", "build_curated_row",
    "build_survival_row", "survival_kind_form",
    "resolve_version", "VersionResolveError",
    "export_seeds",
    "round_trip", "hash_curated_tables", "RoundTripError",
]
