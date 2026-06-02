"""seeds_shared.db_editor -- the headless, in-process entry point that drives a
validated, atomic DB edit through the EXISTING seed->DB applier.

WHAT THIS IS (design D13, data/maintainer-tool/design.md S5 + S10 D13)
---------------------------------------------------------------------
db_editor authors NO parallel write/validate path. It is the Qt-free, in-process
entry point the GUI calls to land one prospective edit; the actual write goes
through import_to_sqlite.apply_seeds -- the existing validated atomic applier that
already (a) gates the WHOLE prospective seed state through the single shared
validator (row-level AND cross-row invariants -- audit-trio integrity, the
(kcdx_id, valid_from) uniqueness, supersession acyclicity, the FK closures) and
(b) applies per-DB in BEGIN/COMMIT. Zero rule logic lives here: the validator is
the gate, apply_seeds is the writer, and a validation failure aborts with NO DB
write (apply_seeds validates before it opens or writes any DB).

THE BRIDGE -- prospective seed via export + a one-row edit (the round_trip.py
convention)
-----------------------------------------------------------------------------
apply_seeds is SEED-CSV-driven: it reads the seed CSVs, diffs them against the DB,
and applies the delta. db_editor gives it the PROSPECTIVE state by:

  1. exporting the CURRENT DB to a temp seed dir (csv_exporter.export_seeds -- the
     same export the round-trip oracle drives; writes NOTHING under data/seeds/),
  2. applying the GUI's prospective edit to the relevant temp seed CSV in place,
  3. repointing import_to_sqlite's three seed-path module constants at that temp
     dir (the seed-path-constant pointing convention round_trip.py + the apply
     oracles use) and calling apply_seeds.

This reuses export_seeds + apply_seeds and re-implements neither; it writes only
under a temp dir (never data/seeds/). The export->edit->apply chain means the
diff apply_seeds computes is EXACTLY the GUI's one-row delta (the export of the
unedited DB round-trips to a no-op; only the edited row differs), so the applier's
existing re-verify / full-column UPDATE classification lands the change.

SCOPE -- this module's UPDATE shape (Phase 1 step 3)
---------------------------------------------------
update_version_row: the version-row UPDATE for one existing address_versions row
-- both the audit-trio re-verify (Job 2 / US-3) and the full-column correction
(US-5: module / kind / rva / signature / the trio / the survival columns). The
identity key valid_from_version and the entity identity (kcdx_id, name) are NEVER
mutated -- they are the row's lookup key, not editable fields.

The INSERT shapes (Jobs 1/6 -- new entity / new version) and the lifecycle UPDATE
(Jobs 4/5 -- supersede / deprecate) are LATER steps. They are different
action-classes the SAME apply path already handles (apply_seeds classifies
add-entity / add-versions-row / deprecate / supersede from the prospective seed),
so each adds a thin entry point over the SAME _drive_apply_over_prospective_seed
bridge below -- this module is structured to grow them without touching the bridge
or the applier. They are NOT built here.
"""
import os
import shutil
import tempfile

from .csv_exporter import (
    export_seeds,
    MODULE_SEED_NAME,
    ADDRESS_NAMES_SEED_NAME,
    ADDRESS_VERSIONS_SEED_NAME,
)
from . import seed_csv_edit


# The columns of an address_versions seed row that an UPDATE may set. The identity
# key (valid_from_version) and the entity identity (kcdx_id) are NOT here -- they
# are the lookup key, never mutated (R8, policy.md). `name` lives on the
# address_names seed, not this row, so it cannot be reached from a version-row
# UPDATE at all. An edit naming any non-editable / unknown column is rejected
# (a programming error in the caller, surfaced loudly -- never a silent partial
# write).
EDITABLE_VERSION_COLUMNS = frozenset({
    "module", "rva", "kind", "signature",
    "last_verified_at_version", "verified_by", "verified_date", "evidence_kind",
    "survival_aob", "survival_anchor_string", "survival_derives_from",
    "survival_rule", "survival_slot_count", "survival_expect_unique",
    "value", "offset", "vtable_slot", "struct_offset",
})

# The identity key (lookup, never editable) of an address_versions seed row.
_VERSION_IDENTITY_COLUMNS = ("kcdx_id", "valid_from_version")


class DbEditError(RuntimeError):
    """A caller-facing error from a db_editor entry point that is NOT a seed
    validation failure -- a bad edit shape (an unknown/non-editable column, an
    attempt to mutate the identity key, a row the identity key does not match in
    the exported seed). A seed VALIDATION failure surfaces as the applier's own
    RuntimeError (the single shared gate); this type names the caller-shape errors
    that precede the apply, so the GUI can tell 'your edit is malformed' apart from
    'the validator rejected the resulting state'."""


def _seed_paths(seed_dir):
    return (
        os.path.join(seed_dir, MODULE_SEED_NAME),
        os.path.join(seed_dir, ADDRESS_NAMES_SEED_NAME),
        os.path.join(seed_dir, ADDRESS_VERSIONS_SEED_NAME),
    )


def _drive_apply_over_prospective_seed(out_dir, dll_path, prospective_seed_dir,
                                       *, log=None):
    """Drive import_to_sqlite.apply_seeds over the prospective seeds under
    prospective_seed_dir, repointing the importer's three seed-path module
    constants for the duration (the round_trip.py / apply-oracle convention),
    restoring them after. Returns apply_seeds' result dict; propagates every typed
    error it raises (VersionResolveError / VersionRefusal / BaselineRefusal /
    RuntimeError) so the caller surfaces the precise reason -- a validation failure
    means NO DB write (apply_seeds validates before opening any DB).

    import_to_sqlite is imported lazily so this seeds_shared submodule carries no
    import-time dependency on import_to_sqlite (which imports seeds_shared)."""
    import import_to_sqlite as imp

    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
     imp.ADDRESS_VERSIONS_SEED_CSV) = _seed_paths(prospective_seed_dir)
    try:
        return imp.apply_seeds(out_dir, dll_path, log=log)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def update_version_row(out_dir, dll_path, kcdx_id, valid_from_version, edits,
                       *, log=None, work_dir=None):
    """Validate + atomically apply a one-row UPDATE to the address_versions row
    identified by (kcdx_id, valid_from_version), driving the existing seed->DB
    applier. Covers the audit-trio re-verify (Job 2 / US-3) and the full-column
    correction (US-5). The identity key valid_from_version and the entity identity
    kcdx_id are NEVER mutated.

    Parameters:
      out_dir            -- the directory holding reference.sqlite +
                            reference-dev.sqlite (the DBs the apply amends).
      dll_path           -- the linked WHGame.dll the version resolver reads.
      kcdx_id            -- the entity id (int) identifying the row to edit.
      valid_from_version -- the version tag (str) identifying the row to edit.
      edits              -- {column: new_value} for the columns the GUI changed.
                            Keys must be in EDITABLE_VERSION_COLUMNS; values are the
                            seed-cell STRINGS (the same textual form the seed CSV
                            carries -- '' for a cleared/NULL cell). An empty `edits`
                            is a no-op edit (the applier sees no delta).
      log                -- optional callable(str) for the applier's progress lines.
      work_dir           -- optional scratch dir for the export + edit; a temp dir
                            is created + removed when omitted. NOTHING is written
                            under data/seeds/ -- the prospective seed lives here.

    Returns apply_seeds' result dict on success (the per-DB counts -- for a valid
    edit the edited row shows as 1 re-verified or is folded into the full-column
    delta).

    Raises (no DB write occurs unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError         -- the edit names a non-editable / unknown column, tries
                             to mutate the identity key, or the identity key
                             matches no row in the exported seed (a caller-shape
                             error, surfaced before the apply).
      RuntimeError        -- the shared validator rejected the resulting prospective
                             seed state (the single gate); the DB is byte-identical
                             to before (apply_seeds validates before any DB open).
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds
                             raises them (version/baseline refusals; no DB write).
    """
    _reject_identity_and_unknown_edits(edits)

    user_db = os.path.join(out_dir, "reference.sqlite")
    if not os.path.isfile(user_db):
        raise DbEditError(
            f"no reference.sqlite under {out_dir!r}; update_version_row amends an "
            f"existing DB (run a rebuild to create the baseline first)")

    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="db_editor_update_")
    try:
        # 1. Export the CURRENT DB to a temp seed dir (the prospective-seed base).
        #    The export is the exact inverse of the import (round-trip-verified),
        #    so the exported seed of the unedited DB applies as a no-op; only the
        #    edited row produces a delta. Writes NOTHING under data/seeds/.
        prospective = os.path.join(work_dir, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)

        # 2. Apply the GUI's one-row edit to the prospective address_versions seed,
        #    diff-preserved (only the edited cells change; row order + the other
        #    rows' bytes are untouched). The row is matched by its identity key;
        #    a non-match is a caller error (the GUI passed a stale/unknown key).
        versions_csv = os.path.join(prospective, ADDRESS_VERSIONS_SEED_NAME)
        matched = seed_csv_edit.update_row_in_place(
            versions_csv,
            key_columns=_VERSION_IDENTITY_COLUMNS,
            key_values=(str(kcdx_id), str(valid_from_version)),
            edits=edits)
        if not matched:
            raise DbEditError(
                f"no address_versions seed row for (kcdx_id={kcdx_id}, "
                f"valid_from_version={valid_from_version!r}) in the exported seed; "
                f"the identity key matches no existing row (a stale or wrong key)")

        # 3. Drive the existing validated atomic applier over the prospective seed.
        #    apply_seeds validates the WHOLE prospective state through the single
        #    shared gate BEFORE opening any DB -- a validation failure raises here
        #    with NO DB write; a valid edit lands per-DB in BEGIN/COMMIT.
        return _drive_apply_over_prospective_seed(
            out_dir, dll_path, prospective, log=log)
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)


def _reject_identity_and_unknown_edits(edits):
    """Guard the UPDATE's contract before any work: every edited column must be in
    EDITABLE_VERSION_COLUMNS. An identity-key column (kcdx_id / valid_from_version)
    or an unknown column raises DbEditError -- the identity key is the row's lookup,
    never editable (R8, policy.md), and an unknown column is a caller bug. This is
    a caller-SHAPE check (NOT a seed-content rule -- those are the validator's); it
    keeps a malformed edit from ever reaching the prospective seed."""
    for col in edits:
        if col in _VERSION_IDENTITY_COLUMNS:
            raise DbEditError(
                f"column {col!r} is the row identity key and is never editable by "
                f"a version-row UPDATE (kcdx_id + valid_from_version identify the "
                f"row; an identity change is a new row, not an edit)")
        if col not in EDITABLE_VERSION_COLUMNS:
            raise DbEditError(
                f"column {col!r} is not an editable address_versions column "
                f"(editable: {sorted(EDITABLE_VERSION_COLUMNS)})")
