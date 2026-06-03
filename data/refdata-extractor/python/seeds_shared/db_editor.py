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

SCOPE -- this module's edit shapes
----------------------------------
update_version_row (step 3): the version-row UPDATE for one existing
address_versions row -- both the audit-trio re-verify (Job 2 / US-3) and the
full-column correction (US-5). The identity key valid_from_version and the entity
identity (kcdx_id, name) are NEVER mutated -- they are the row's lookup key.

create_version (step 4, Job 6 / US-6): append a NEW address_versions row for an
EXISTING entity. The prospective-seed edit appends a versions-seed row; apply_seeds
classifies it as add-versions-row (or add-entity if the entity had no row yet -- it
cannot here, the entity already exists). The (kcdx_id, valid_from_version)
tuple-uniqueness is the validator's HARD ERROR; this entry surfaces it cleanly, it
does not reimplement it.

create_entity (step 4, Job 1 / US-7): append a NEW address_names row (next free
kcdx_id, append-only) + its first address_versions row. apply_seeds classifies it
as add-entity. The next-free-id helper finds the highest existing names-seed id + 1.

Both INSERT entries SURFACE the AP18 new-row approval flag (a returned marker the
GUI gates on -- D11/policy.md; db_editor does NOT self-approve the DB addition) and
drive the SAME _drive_apply_over_prospective_seed bridge over a prospective seed
that has a NEW row appended -- the bridge + the applier are untouched.

supersede_entity / deprecate_entity (step 5, Jobs 4/5 / US-8): the entity-level
lifecycle UPDATE on an EXISTING address_names row. supersede sets superseded_by +
superseded_at_version together; deprecate sets is_deprecated + deprecated_at_version
together (with deprecation_replacement allowed only when deprecated). Each is a
names-side UPDATE the SAME apply path already classifies (apply_seeds classifies
deprecate / supersede from the prospective names seed and gates pair-integrity,
no-self-supersede, supersession acyclicity, and replacement-requires-deprecated via
validators.py) -- so each is a thin entry over the SAME bridge: update the
prospective address_names seed's lifecycle cells in place (via
seed_csv_edit.update_row_in_place, keyed on the names id) and drive apply_seeds. The
entity identity (id, name) is never mutated. These are UPDATEs to an already-approved
entity -- NOT AP18-gated (AP18 gates new ROWS only), so no ap18_new_row flag.
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

# The full authored column set of an address_versions seed row a CREATE may set --
# the identity key (kcdx_id, valid_from_version) PLUS every editable column. A
# create_version / create_entity authors a whole NEW row, so unlike an UPDATE it
# DOES set the identity key (that is the point: a new (kcdx_id, valid_from) tuple).
# A cell naming any column outside this set is a caller bug (DbEditError); the
# REQUIRED-column / enum / FK / tuple-uniqueness rules are the validator's, not
# checked here. (`name` lives on the address_names seed; it is create_entity's
# argument, never a versions-row column.)
_VERSION_AUTHORED_COLUMNS = frozenset(_VERSION_IDENTITY_COLUMNS) | EDITABLE_VERSION_COLUMNS

# The columns policy.md S"Required columns" requires non-empty on EVERY
# address_versions row. The validator (apply_seeds' gate) is the enforcer -- a HARD
# ERROR on any empty required cell. create_version inherits them from the source row
# (prefill); create_entity's caller supplies them on the first row. Named here only
# for the create_entity argument contract (the GUI/caller passes first_version_columns
# carrying at least these); a missing one is surfaced by the validator as a clean
# error, NOT reimplemented as a db_editor-side check.
_VERSION_REQUIRED_COLUMNS = ("valid_from_version", "module", "kind")

# The AP18 "nothing changed" signal (D12) -- whether a new version row is identical
# to its source on every authored column except valid_from_version -- is computed by
# the SHARED pure primitive field_delta.is_new_version_nothing_changed (the single
# source of that verdict; _new_version_nothing_changed below delegates to it per
# existing source row). The comparison column set lives there (the authored version
# columns minus valid_from_version, derived from the seed header), not duplicated here.

# The identity key (lookup, never editable) of an address_names seed row -- the id
# (== kcdx_id). A lifecycle UPDATE (supersede / deprecate, Jobs 4/5) matches the row
# by this key and never mutates it OR the entity's name. The lifecycle cells a
# supersede sets (superseded_by + superseded_at_version) and a deprecate sets
# (is_deprecated + deprecated_at_version + optional deprecation_replacement) are
# written by the entry points below; the pair-integrity / no-self-supersede /
# acyclicity / replacement-requires-deprecated rules are the validator's
# (apply_seeds' gate: resolve_and_check_name_refs + check_supersession_acyclic over
# the FULL prospective names seed), NOT reimplemented here.
_NAMES_IDENTITY_COLUMNS = ("id",)


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
                                       *, version=None, log=None,
                                       defer_commit=False, validate_only=False):
    """Drive import_to_sqlite over the prospective seeds under prospective_seed_dir,
    repointing the importer's three seed-path module constants for the duration (the
    round_trip.py / apply-oracle convention), restoring them after. Returns the
    importer's result; propagates every typed error it raises (ValueError /
    VersionResolveError / VersionRefusal / BaselineRefusal / RuntimeError) so the
    caller surfaces the precise reason -- a validation failure means NO DB write
    (the importer validates before opening any DB).

    THREE drive modes (the caller picks one; the prospective-seed build above is
    identical for all three -- only what is done with the validated state differs):
      validate_only=True -- run import_to_sqlite.validate_prospective_seeds: the
          dry-validate gate that STOPS before any DB open (the Save-PREVIEW seam,
          step 4b). Returns {"tag", "ordinal"}; NO DB open, NO write, NO handle --
          the DB is byte-identical. The save endpoints (app.routes_save) drive this.
      defer_commit=True  -- run apply_seeds under one held outer txn per DB and
          RETURN a DeferredCommit handle (the two open uncommitted connections);
          Confirm (step 5) commits/rolls back. (validate_only takes precedence -- a
          caller asking to validate never writes; the two are not combined.)
      both False (default) -- run apply_seeds immediate: write + commit + close, a
          result dict (the historical path; every landed oracle exercises it).

    `version` is a pre-resolved (tag, ordinal); when given, the DLL is not read --
    the caller already resolved the version, e.g. the web backend per
    data/maintainer-tool/design.md D15. It threads straight through, which requires
    EXACTLY ONE of dll_path / version.

    The export of the CURRENT DB that BUILT the prospective seed already happened in
    the caller (before this drive) -- it read the COMMITTED pre-edit state, so the
    prospective seed is correct regardless of mode; the mode changes only whether
    (and how) the resulting delta commits, not the seed it diffs against. In
    validate_only mode nothing commits at all -- the seed is validated and discarded.

    The seed-path-constant repointing is restored in `finally` -- it runs in every
    mode (the constants are global importer state, not the held txn), so neither a
    held-open handle nor the validate path leaves the importer's seed paths pointed
    at a temp dir.

    import_to_sqlite is imported lazily so this seeds_shared submodule carries no
    import-time dependency on import_to_sqlite (which imports seeds_shared)."""
    import import_to_sqlite as imp

    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
     imp.ADDRESS_VERSIONS_SEED_CSV) = _seed_paths(prospective_seed_dir)
    try:
        if validate_only:
            # The Save-PREVIEW gate: validate the prospective state, NO DB write.
            return imp.validate_prospective_seeds(out_dir, dll_path, version=version,
                                                  log=log)
        return imp.apply_seeds(out_dir, dll_path, version=version, log=log,
                               defer_commit=defer_commit)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def update_version_row(out_dir, dll_path, kcdx_id, valid_from_version, edits,
                       *, version=None, log=None, work_dir=None,
                       defer_commit=False, validate_only=False):
    """Validate + atomically apply a one-row UPDATE to the address_versions row
    identified by (kcdx_id, valid_from_version), driving the existing seed->DB
    applier. Covers the audit-trio re-verify (Job 2 / US-3) and the full-column
    correction (US-5). The identity key valid_from_version and the entity identity
    kcdx_id are NEVER mutated.

    Parameters:
      out_dir            -- the directory holding reference.sqlite +
                            reference-dev.sqlite (the DBs the apply amends).
      dll_path           -- the linked WHGame.dll the version resolver reads.
      version            -- a pre-resolved (tag, ordinal); when given, the DLL is
                            not read -- the caller already resolved the version,
                            e.g. the web backend per
                            data/maintainer-tool/design.md D15. Supply EXACTLY ONE
                            of dll_path / version (apply_seeds raises ValueError on
                            neither or both).
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
                            (Safe to remove even in deferred mode: the held
                            connections point at out_dir's DBs, NOT this temp seed --
                            apply_seeds read the seed CSVs before returning.)
      defer_commit       -- DEFAULT False (immediate -- the historical behaviour;
                            returns the apply result dict). When True (step 4a):
                            the edit lands under a HELD outer txn that does NOT
                            commit; the return is a DeferredCommit HANDLE (carrying
                            the two open uncommitted connections + the result) the
                            caller commits via import_to_sqlite.commit(handle) on
                            confirm / discards via .rollback(handle) on cancel.
      validate_only      -- DEFAULT False. When True (the Save-PREVIEW seam, step
                            4b): the prospective edit is VALIDATED through the
                            data-core's gate and the call STOPS before any DB open --
                            NO write, NO held txn, the DB byte-identical. Returns
                            {"tag","ordinal"} on a valid edit; raises the validator's
                            error on an invalid one (the same verdict apply_seeds
                            would give). Takes precedence over defer_commit.

    Returns:
      validate_only=True -- {"tag","ordinal"} (the version the edit validated
                            against); the DB is untouched (Save preview).
      defer_commit=False -- apply_seeds' result dict (the per-DB counts -- for a
                            valid edit the edited row shows as 1 re-verified or is
                            folded into the full-column delta).
      defer_commit=True  -- a DeferredCommit handle (the held, uncommitted edit).

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
                             In deferred mode a refusal still leaves NO held txn and
                             NO write (apply_seeds rolls back + closes on error).
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

        # 3. Drive the existing validated gate over the prospective seed.
        #    validate_only=True (the Save-PREVIEW seam, step 4b) runs the data-core's
        #    validation gate and STOPS before any DB open -- a validation failure
        #    raises here with NO DB write, a valid edit returns {"tag","ordinal"} +
        #    the DB is byte-identical. Otherwise apply_seeds validates the WHOLE
        #    prospective state BEFORE opening any DB and a valid edit lands per-DB in
        #    BEGIN/COMMIT (immediate) or under a held outer txn returned as a handle
        #    (defer_commit=True).
        return _drive_apply_over_prospective_seed(
            out_dir, dll_path, prospective, version=version, log=log,
            defer_commit=defer_commit, validate_only=validate_only)
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


# ---------------------------------------------------------------------------
# The INSERT shapes (step 4) -- new version (Job 6 / US-6) + new entity (Job 1 /
# US-7). Both append a NEW row to the prospective seed and drive the SAME bridge;
# both SURFACE the AP18 new-row flag (D11) the GUI gates on.
# ---------------------------------------------------------------------------
def _read_seed_rows(csv_path):
    """The data rows of a seed CSV as a list of dicts (skipping `#`-comment lines
    the same way the seed readers do). Used to read the exported prospective seed
    for the next-free-id scan + the nothing-changed source-row lookup -- a plain
    read, no format mutation."""
    import csv
    with open(csv_path, newline="", encoding="utf-8") as f:
        lines = [ln for ln in f if not ln.lstrip().startswith("#")]
    return [dict(r) for r in csv.DictReader(lines)]


def _next_free_kcdx_id(names_csv_path):
    """The next free kcdx_id = highest existing address_names_seed.id + 1
    (policy.md S"ID assignment": maintainer-supplied integers, NO autoincrement,
    APPEND-ONLY, never recycle, no bands -- the next free integer is the next id).
    An empty names seed (no rows) starts at 1. Reads the exported PROSPECTIVE seed
    (the current DB's full curated set), so the assigned id never collides with an
    existing entity; the validator's duplicate-id HARD ERROR is the backstop."""
    rows = _read_seed_rows(names_csv_path)
    ids = [int(r["id"]) for r in rows if (r.get("id") or "").strip()]
    return (max(ids) + 1) if ids else 1


def _reject_unknown_version_cells(cells, *, where):
    """Guard a CREATE's versions-row cells: every column must be an authored
    address_versions seed column (the identity key OR an editable column). An
    unknown column is a caller bug (DbEditError) -- surfaced before the row is ever
    written to the prospective seed. This is a caller-SHAPE check ONLY; the
    REQUIRED-column / enum / FK / tuple-uniqueness rules are the validator's."""
    for col in cells:
        if col not in _VERSION_AUTHORED_COLUMNS:
            raise DbEditError(
                f"{where}: column {col!r} is not an authored address_versions seed "
                f"column (authored: {sorted(_VERSION_AUTHORED_COLUMNS)})")


def _cell(value):
    """A seed cell string from a caller value: None -> '' (an empty/NULL cell),
    else str(value). Mirrors seed_csv_edit's cell convention so the comparison the
    nothing-changed signal does is string-vs-string against the exported seed."""
    return "" if value is None else str(value)


def create_version(out_dir, dll_path, kcdx_id, valid_from_version, columns,
                   *, version=None, log=None, work_dir=None, defer_commit=False,
                   validate_only=False):
    """Validate + atomically APPEND a new address_versions row (a new game-version
    row, Job 6 / US-6) for the EXISTING entity `kcdx_id`, driving the existing
    seed->DB applier. The new row's identity key is (kcdx_id, valid_from_version);
    its other cells come from `columns` (the GUI prefills them from a source row).
    apply_seeds classifies the appended row as add-versions-row.

    Parameters:
      out_dir            -- the directory holding reference.sqlite +
                            reference-dev.sqlite (the DBs the apply amends).
      dll_path           -- the linked WHGame.dll the version resolver reads.
      version            -- a pre-resolved (tag, ordinal); when given, the DLL is
                            not read -- the caller already resolved the version,
                            e.g. the web backend per
                            data/maintainer-tool/design.md D15. Supply EXACTLY ONE
                            of dll_path / version (apply_seeds raises ValueError on
                            neither or both).
      kcdx_id            -- the EXISTING entity id (int) the new version belongs to.
      valid_from_version -- the new row's version tag (str) -- its identity key half
                            (prefilled from the linked DLL when linked, US-6).
      columns            -- {column: value} for the new row's other authored cells
                            (module / kind / rva / signature / the trio / survival /
                            ...). Keys must be authored address_versions columns
                            (the identity key may appear too but is taken from the
                            kcdx_id / valid_from_version args); values are seed-cell
                            values (None -> '' = NULL). The REQUIRED columns
                            (module, kind -- valid_from_version is the arg) must be
                            present + non-empty or the validator rejects the row.
      log                -- optional callable(str) for the applier's progress lines.
      work_dir           -- optional scratch dir; a temp dir is created + removed
                            when omitted. NOTHING is written under data/seeds/.

    `defer_commit` (DEFAULT False): when True (step 4a), the new-version append lands
    under a HELD outer txn that does NOT commit -- the returned dict's "result" field
    is the DeferredCommit HANDLE (the two open uncommitted connections + the apply
    result) instead of the plain apply result dict; the AP18 + nothing_changed flags
    still compute (they read the prospective seed, not the committed DB) and are
    returned alongside. The caller gates on ap18_new_row / nothing_changed, then
    commits via import_to_sqlite.commit(result["result"]) on confirm or discards via
    .rollback(result["result"]) on cancel.

    `validate_only` (DEFAULT False): when True (the Save-PREVIEW seam, step 4b), the
    prospective append is VALIDATED through the data-core's gate and the call STOPS
    before any DB open -- the returned dict's "result" is the validator's
    {"tag","ordinal"} verdict, NO write, NO held txn, the DB byte-identical. The
    ap18_new_row / nothing_changed flags still surface (they read the prospective
    seed) so the Save preview shows them (D11/D12) before any write. An invalid
    prospective row raises the validator's error. Takes precedence over defer_commit.

    Returns a dict on success (no exception == the row landed -- committed in
    immediate mode, HELD uncommitted in deferred mode):
      {"result": <apply result>,   # immediate: apply_seeds' result dict.
                                    # deferred: a DeferredCommit handle (.result
                                    # carries that same apply dict; .ucon/.dcon are
                                    # the held connections to commit/rollback later).
       "ap18_new_row": True,        # SURFACE the AP18 flag -- a new DB row landed;
                                    # the GUI confirm step GATES on this (D11). This
                                    # is a MARKER, not a db_editor-side approval.
       "addition_kind": "version", # what was added (a new version of an entity).
       "kcdx_id": <int>, "valid_from_version": <str>,
       "nothing_changed": <bool>}   # D12: True IFF the new row equals its source
                                    # row on every authored column except
                                    # valid_from_version (the GUI steers to
                                    # re-verify instead of minting a duplicate).

    Raises (no DB write unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError  -- `columns` names a non-authored column, or `kcdx_id` matches no
                      existing entity in the exported seed (a caller-shape error).
      RuntimeError -- the shared validator rejected the prospective seed state (the
                      single gate -- a DUPLICATE (kcdx_id, valid_from_version) tuple,
                      a missing REQUIRED column, an out-of-enum kind/evidence_kind,
                      an unresolvable module/survival FK, a malformed value). The DB
                      is byte-identical to before (apply_seeds validates before any
                      DB open).
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds
                      raises them (a function-kind row needs a bulk baseline; no DB
                      write).
    """
    _reject_unknown_version_cells(columns, where="create_version")

    user_db = os.path.join(out_dir, "reference.sqlite")
    if not os.path.isfile(user_db):
        raise DbEditError(
            f"no reference.sqlite under {out_dir!r}; create_version amends an "
            f"existing DB (run a rebuild to create the baseline first)")

    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="db_editor_create_version_")
    try:
        prospective = os.path.join(work_dir, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)

        names_csv = os.path.join(prospective, ADDRESS_NAMES_SEED_NAME)
        versions_csv = os.path.join(prospective, ADDRESS_VERSIONS_SEED_NAME)

        # The entity must already exist (this is a NEW VERSION of an EXISTING
        # entity, not a new entity). A missing entity is a caller-shape error
        # surfaced before the apply -- distinct from the validator's later FK check,
        # so the GUI can tell 'you picked a non-existent entity' apart from 'the
        # validator rejected the row'.
        names_ids = {(r.get("id") or "").strip()
                     for r in _read_seed_rows(names_csv)}
        if str(kcdx_id) not in names_ids:
            raise DbEditError(
                f"create_version: kcdx_id={kcdx_id} matches no existing entity in "
                f"the exported seed; a new VERSION needs an existing entity (use "
                f"create_entity to mint a brand-new entity)")

        # D12 "nothing changed": the new row's source is the entity's existing row
        # whose authored cells the new row prefills from. Compute the signal BEFORE
        # the append by comparing the prospective new row against every existing row
        # for this entity on the non-valid_from authored columns; the signal fires
        # when the new row is identical to a source row except valid_from_version.
        new_cells = {col: _cell(val) for col, val in columns.items()}
        new_cells["kcdx_id"] = str(kcdx_id)
        new_cells["valid_from_version"] = str(valid_from_version)
        nothing_changed = _new_version_nothing_changed(
            versions_csv, kcdx_id, new_cells)

        # Append the new versions row to the prospective seed (diff-preserved). The
        # row carries the identity key + the prefilled cells; the validator gates
        # tuple-uniqueness + required + enum + FK.
        seed_csv_edit.append_row(versions_csv, new_cells)

        # `result` rides the "result" slot regardless of drive mode: in validate_only
        # mode (the Save-PREVIEW seam) it is the validator's {"tag","ordinal"} verdict
        # and NO DB write happened; in deferred mode a DeferredCommit handle (held,
        # uncommitted); in immediate mode apply_seeds' result dict. The AP18 +
        # nothing_changed flags computed above read the PROSPECTIVE SEED (not the DB),
        # so they are correct in every mode -- the Save preview surfaces them (D11/D12)
        # before any write, exactly as the confirm gate would.
        result = _drive_apply_over_prospective_seed(
            out_dir, dll_path, prospective, version=version, log=log,
            defer_commit=defer_commit, validate_only=validate_only)
        return {
            "result": result,
            "ap18_new_row": True,
            "addition_kind": "version",
            "kcdx_id": int(kcdx_id),
            "valid_from_version": str(valid_from_version),
            "nothing_changed": nothing_changed,
        }
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)


def _new_version_nothing_changed(versions_csv, kcdx_id, new_cells):
    """D12: True IFF the prospective new version row (`new_cells`) equals SOME
    existing row of the same entity on every authored column except
    valid_from_version. Reads the exported prospective versions seed for the
    entity's existing rows; the per-row dict-vs-dict equality is the SHARED
    primitive field_delta.is_new_version_nothing_changed (the single source of the
    "identical except valid_from_version" verdict -- this function owns finding the
    entity's existing rows, field_delta owns the pure comparison). An entity with no
    existing row (cannot happen via create_version -- the caller checked the entity
    exists, and an existing entity has >=1 baseline row) -> False (nothing to be
    identical to)."""
    from .field_delta import is_new_version_nothing_changed
    existing = [r for r in _read_seed_rows(versions_csv)
                if (r.get("kcdx_id") or "").strip() == str(kcdx_id)]
    return any(is_new_version_nothing_changed(src, new_cells)
               for src in existing)


def create_entity(out_dir, dll_path, name, first_version_columns,
                  *, version=None, log=None, work_dir=None, defer_commit=False,
                  validate_only=False):
    """Validate + atomically APPEND a brand-NEW entity (Job 1 / US-7): a new
    address_names row (assigned the next free kcdx_id, append-only) + its first
    address_versions row, driving the existing seed->DB applier. apply_seeds
    classifies the appended versions row as add-entity (its kcdx_id is unknown to
    the DB) and INSERTs the names row alongside it.

    Parameters:
      out_dir               -- the directory holding the two reference DBs.
      dll_path              -- the linked WHGame.dll the version resolver reads.
      version               -- a pre-resolved (tag, ordinal); when given, the DLL
                               is not read -- the caller already resolved the
                               version, e.g. the web backend per
                               data/maintainer-tool/design.md D15. Supply EXACTLY
                               ONE of dll_path / version (apply_seeds raises
                               ValueError on neither or both).
      name                  -- the new entity's canonical name (str) -- the
                               address_names row's `name` cell.
      first_version_columns -- {column: value} for the first address_versions row's
                               authored cells. MUST carry the REQUIRED columns
                               (policy.md): valid_from_version, module, kind. The
                               audit trio is all-set-or-all-null (a brand-new
                               unverified row leaves it all-null); the validator
                               gates the trio integrity + the required columns + the
                               kind enum + the FKs. Keys must be authored
                               address_versions columns; values are seed cells
                               (None -> '' = NULL).
      log                   -- optional callable(str) for progress lines.
      work_dir              -- optional scratch dir; a temp dir is created + removed
                               when omitted. NOTHING is written under data/seeds/.

    `defer_commit` (DEFAULT False): when True (step 4a), the new-entity append lands
    under a HELD outer txn that does NOT commit -- the returned dict's "result" field
    is the DeferredCommit HANDLE instead of the plain apply result dict; ap18_new_row
    still surfaces. The caller commits via import_to_sqlite.commit(result["result"])
    on confirm / discards via .rollback(result["result"]) on cancel.

    `validate_only` (DEFAULT False): when True (the Save-PREVIEW seam, step 4b), the
    prospective new entity is VALIDATED through the data-core's gate and the call
    STOPS before any DB open -- the returned dict's "result" is the validator's
    {"tag","ordinal"} verdict, NO write, NO held txn, the DB byte-identical;
    ap18_new_row still surfaces (read from the prospective seed) for the Save preview
    (D11). An invalid prospective entity raises the validator's error. Takes
    precedence over defer_commit.

    Returns a dict on success (committed immediate, HELD uncommitted deferred):
      {"result": <apply result>,   # immediate: apply_seeds' result dict.
                                    # deferred: a DeferredCommit handle.
       "ap18_new_row": True,        # SURFACE the AP18 flag (a new entity landed);
                                    # the GUI confirm GATES on it (D11). A MARKER.
       "addition_kind": "entity",
       "kcdx_id": <int>,            # the next-free id assigned (append-only).
       "name": <str>}

    Raises (no DB write unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError  -- first_version_columns names a non-authored column, or `name`
                      is empty (a caller-shape error surfaced before the apply).
      RuntimeError -- the shared validator rejected the prospective seed state (a
                      missing REQUIRED column -- no module/kind/valid_from_version --
                      a duplicate id/name, an out-of-enum kind, a partial trio, an
                      unresolvable FK). The DB is byte-identical to before.
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds
                      raises them (a function-kind first row needs a bulk baseline).
    """
    _reject_unknown_version_cells(first_version_columns, where="create_entity")
    if not (name or "").strip():
        raise DbEditError(
            "create_entity: a new entity needs a non-empty name (the address_names "
            "row's `name` cell)")

    user_db = os.path.join(out_dir, "reference.sqlite")
    if not os.path.isfile(user_db):
        raise DbEditError(
            f"no reference.sqlite under {out_dir!r}; create_entity amends an "
            f"existing DB (run a rebuild to create the baseline first)")

    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix="db_editor_create_entity_")
    try:
        prospective = os.path.join(work_dir, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)

        names_csv = os.path.join(prospective, ADDRESS_NAMES_SEED_NAME)
        versions_csv = os.path.join(prospective, ADDRESS_VERSIONS_SEED_NAME)

        # Assign the next free kcdx_id from the exported (current-DB) names seed --
        # append-only, highest existing + 1; the maintainer never types it (US-7).
        kid = _next_free_kcdx_id(names_csv)

        # Append the address_names row (id + name; the lifecycle FK cells default to
        # '' -- a brand-new entity is neither superseded nor deprecated). Then the
        # first address_versions row carrying the new id + the caller's first-row
        # cells. Both appends are diff-preserved; the validator gates the whole
        # resulting state (required columns, trio integrity, kind enum, the FKs, the
        # every-entity-covered baseline rule).
        seed_csv_edit.append_row(names_csv, {"id": str(kid), "name": name})

        version_cells = {col: _cell(val)
                         for col, val in first_version_columns.items()}
        version_cells["kcdx_id"] = str(kid)
        seed_csv_edit.append_row(versions_csv, version_cells)

        # `result` rides the "result" slot regardless of drive mode: the validator's
        # {"tag","ordinal"} (validate_only -- NO DB write), a DeferredCommit handle
        # (deferred), or apply_seeds' dict (immediate). ap18_new_row surfaces in every
        # mode (read from the prospective seed, not the DB).
        result = _drive_apply_over_prospective_seed(
            out_dir, dll_path, prospective, version=version, log=log,
            defer_commit=defer_commit, validate_only=validate_only)
        return {
            "result": result,
            "ap18_new_row": True,
            "addition_kind": "entity",
            "kcdx_id": kid,
            "name": name,
        }
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)


# ---------------------------------------------------------------------------
# The lifecycle UPDATE shapes (step 5) -- supersede (Job 4 / US-8) + deprecate
# (Job 5 / US-8). Both edit an EXISTING address_names row's lifecycle columns and
# drive the SAME bridge; apply_seeds classifies the resulting deprecate/supersede
# from the prospective names seed and gates the pair-integrity / no-self-supersede /
# acyclicity / replacement-requires-deprecated rules (validators.py:
# resolve_and_check_name_refs + check_supersession_acyclic). These are UPDATEs to an
# already-approved entity, NOT new rows -- NOT AP18-gated (AP18 gates ADDITIONS only;
# the returned dict carries no ap18_new_row flag). The entity identity (id, name) is
# never mutated.
# ---------------------------------------------------------------------------
def _drive_names_lifecycle_edit(out_dir, dll_path, kcdx_id, edits, *,
                                action, log, work_dir, version=None,
                                defer_commit=False, validate_only=False):
    """Shared driver for the lifecycle UPDATEs: export the current DB to a temp
    seed, fold `edits` into the address_names row keyed on `id == kcdx_id` (via
    seed_csv_edit.update_row_in_place, diff-preserved), and drive the existing
    validated atomic applier over the prospective seed. A validation failure
    (pair-integrity / self-supersede / cycle / replacement-without-deprecated) is
    the validator's RuntimeError out of apply_seeds -- NO DB write occurs (apply_seeds
    validates the full names seed before opening any DB). `action` is the addition
    label for the returned dict ('supersede' / 'deprecate'). `version` is a
    pre-resolved (tag, ordinal) threaded to apply_seeds; when given, the DLL is not
    read (the caller already resolved the version, e.g. the web backend per
    data/maintainer-tool/design.md D15 -- supply EXACTLY ONE of dll_path / version).

    `defer_commit` threads to apply_seeds (step 4a): when True the lifecycle edit
    lands under a HELD outer txn that does NOT commit -- the returned dict's "result"
    is the DeferredCommit HANDLE the caller commits/rolls back later.

    `validate_only` (the Save-PREVIEW seam, step 4b): when True the prospective
    lifecycle edit is VALIDATED through the data-core's gate and the drive STOPS
    before any DB open -- the returned dict's "result" is the validator's
    {"tag","ordinal"} verdict, NO write, NO held txn, the DB byte-identical. Takes
    precedence over defer_commit.

    Returns a dict on success (validated no-write validate_only; committed immediate;
    HELD uncommitted deferred):
      {"result": <apply result>,  # validate_only: {"tag","ordinal"} (no DB write).
                                   # immediate: apply_seeds' result dict.
                                   # deferred: a DeferredCommit handle.
       "action": <str>,           # 'supersede' or 'deprecate'
       "kcdx_id": <int>}          # the entity edited (identity unchanged)

    Raises (no DB write unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError  -- `kcdx_id` matches no existing address_names row in the exported
                      seed (a stale/wrong key -- a caller-shape error before the apply).
      RuntimeError -- the shared validator rejected the resulting prospective seed
                      state (the single gate -- a half-set lifecycle pair, a self-
                      supersede, a supersession cycle, a deprecation_replacement set
                      without is_deprecated, or an unresolvable successor/replacement
                      name). The DB is byte-identical to before.
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds raises
                      them (no DB write)."""
    user_db = os.path.join(out_dir, "reference.sqlite")
    if not os.path.isfile(user_db):
        raise DbEditError(
            f"no reference.sqlite under {out_dir!r}; {action} amends an existing DB "
            f"(run a rebuild to create the baseline first)")

    owns_work_dir = work_dir is None
    work_dir = work_dir or tempfile.mkdtemp(prefix=f"db_editor_{action}_")
    try:
        # 1. Export the CURRENT DB to a temp seed dir (the prospective-seed base);
        #    the export round-trips the unedited DB to a no-op, so only the lifecycle
        #    cells produce a delta. Writes NOTHING under data/seeds/.
        prospective = os.path.join(work_dir, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)

        # 2. Fold the lifecycle edit into the prospective address_names seed,
        #    diff-preserved, matched on the row's id (== kcdx_id). A non-match is a
        #    caller error (a stale/unknown id) surfaced before the apply -- distinct
        #    from the validator's later rejection of the lifecycle content.
        names_csv = os.path.join(prospective, ADDRESS_NAMES_SEED_NAME)
        matched = seed_csv_edit.update_row_in_place(
            names_csv,
            key_columns=_NAMES_IDENTITY_COLUMNS,
            key_values=(str(kcdx_id),),
            edits=edits)
        if not matched:
            raise DbEditError(
                f"no address_names seed row for id={kcdx_id} in the exported seed; "
                f"the identity key matches no existing entity (a stale or wrong id)")

        # 3. Drive the existing validated gate. The data-core validates the WHOLE
        #    prospective names seed (pair-integrity, no-self-supersede, acyclicity,
        #    replacement-requires-deprecated) BEFORE opening any DB -- a violation
        #    raises here with NO DB write. validate_only STOPS there (Save preview, no
        #    write); otherwise a valid edit lands per-DB in BEGIN/COMMIT (immediate)
        #    or under a held outer txn (deferred).
        result = _drive_apply_over_prospective_seed(
            out_dir, dll_path, prospective, version=version, log=log,
            defer_commit=defer_commit, validate_only=validate_only)
        return {"result": result, "action": action, "kcdx_id": int(kcdx_id)}
    finally:
        if owns_work_dir:
            shutil.rmtree(work_dir, ignore_errors=True)


def supersede_entity(out_dir, dll_path, kcdx_id, superseded_by,
                     superseded_at_version, *, version=None, log=None,
                     work_dir=None, defer_commit=False, validate_only=False):
    """Validate + atomically set the SUPERSESSION edge on the existing address_names
    row `kcdx_id` (Job 4 / US-8): superseded_by + superseded_at_version TOGETHER,
    driving the existing seed->DB applier. The successor name + the version are
    written into the prospective names seed; apply_seeds classifies + gates the
    supersession (pair-integrity both-or-neither, no self-supersede, no cycle in the
    supersede graph -- validators.py, NOT reimplemented here). This is an UPDATE to an
    approved entity, NOT a new row -- NOT AP18-gated. The entity identity (id, name)
    is never mutated.

    Parameters:
      out_dir               -- the directory holding reference.sqlite +
                               reference-dev.sqlite (the DBs the apply amends).
      dll_path              -- the linked WHGame.dll the version resolver reads.
      version               -- a pre-resolved (tag, ordinal); when given, the DLL is
                               not read -- the caller already resolved the version,
                               e.g. the web backend per
                               data/maintainer-tool/design.md D15. Supply EXACTLY
                               ONE of dll_path / version (apply_seeds raises
                               ValueError on neither or both).
      kcdx_id               -- the entity id (int) of the row being superseded (X).
      superseded_by         -- the SUCCESSOR entity's canonical name (str), as the
                               seed carries it (the validator resolves it to an id).
                               An empty/None value with a set version is a half-set
                               pair the validator rejects.
      superseded_at_version -- the game-version TAG (str) the supersession takes
                               effect at. Half-set the same way.
      log                   -- optional callable(str) for the applier's progress lines.
      work_dir              -- optional scratch dir for the export + edit; a temp dir
                               is created + removed when omitted. NOTHING is written
                               under data/seeds/.

    `defer_commit` (DEFAULT False): when True (step 4a) the edge lands under a HELD
    outer txn -- the returned "result" is a DeferredCommit handle the caller
    commits/rolls back later (import_to_sqlite.commit / .rollback).

    Returns {"result": <apply result>, "action": "supersede", "kcdx_id": <int>} on
    success (committed in immediate mode, HELD uncommitted in deferred mode --
    "result" is then a DeferredCommit handle).

    Raises (no DB write occurs unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError  -- `kcdx_id` matches no existing entity in the exported seed.
      RuntimeError -- the shared validator rejected the prospective seed state (the
                      single gate -- a half-set superseded_by/superseded_at_version
                      pair, a row superseding ITSELF, a supersession CYCLE, or an
                      unresolvable successor name). The DB is byte-identical to before.
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds raises."""
    edits = {
        "superseded_by": superseded_by,
        "superseded_at_version": superseded_at_version,
    }
    return _drive_names_lifecycle_edit(
        out_dir, dll_path, kcdx_id, edits,
        action="supersede", log=log, work_dir=work_dir, version=version,
        defer_commit=defer_commit, validate_only=validate_only)


def deprecate_entity(out_dir, dll_path, kcdx_id, *, is_deprecated=True,
                     deprecated_at_version=None, deprecation_replacement=None,
                     version=None, log=None, work_dir=None, defer_commit=False,
                     validate_only=False):
    """Validate + atomically set the DEPRECATION flags on the existing address_names
    row `kcdx_id` (Job 5 / US-8): is_deprecated + deprecated_at_version TOGETHER,
    with deprecation_replacement allowed ONLY when deprecated, driving the existing
    seed->DB applier. The flags are written into the prospective names seed;
    apply_seeds gates the pair-integrity (both-or-neither) + the
    replacement-requires-deprecated rule (validators.py, NOT reimplemented here). This
    is an UPDATE to an approved entity, NOT a new row -- NOT AP18-gated. The entity
    identity (id, name) is never mutated.

    Parameters:
      out_dir                 -- the directory holding the two reference DBs.
      dll_path                -- the linked WHGame.dll the version resolver reads.
      version                 -- a pre-resolved (tag, ordinal); when given, the DLL
                                 is not read -- the caller already resolved the
                                 version, e.g. the web backend per
                                 data/maintainer-tool/design.md D15. Supply EXACTLY
                                 ONE of dll_path / version (apply_seeds raises
                                 ValueError on neither or both).
      kcdx_id                 -- the entity id (int) to deprecate.
      is_deprecated           -- True to set the flag (seed cell '1'); False/None to
                                 CLEAR it (seed cell '' -- un-deprecate). The
                                 deprecated_at_version must agree (both-or-neither):
                                 set the version when deprecating, leave it None when
                                 clearing, or the validator rejects the half-set pair.
      deprecated_at_version   -- the game-version TAG (str) the deprecation takes
                                 effect at; None when clearing.
      deprecation_replacement -- optional successor/replacement entity NAME (str) the
                                 engine surfaces as advisory (does NOT auto-follow).
                                 Allowed ONLY when is_deprecated is set; setting it
                                 while not deprecated is a validator HARD ERROR.
      log                     -- optional callable(str) for the applier's progress.
      work_dir                -- optional scratch dir; a temp dir is created + removed
                                 when omitted. NOTHING is written under data/seeds/.

    `defer_commit` (DEFAULT False): when True (step 4a) the flags land under a HELD
    outer txn -- the returned "result" is a DeferredCommit handle the caller
    commits/rolls back later (import_to_sqlite.commit / .rollback).

    Returns {"result": <apply result>, "action": "deprecate", "kcdx_id": <int>} on
    success (committed in immediate mode, HELD uncommitted in deferred mode --
    "result" is then a DeferredCommit handle).

    Raises (no DB write occurs unless the apply reaches the per-DB BEGIN/COMMIT):
      DbEditError  -- `kcdx_id` matches no existing entity in the exported seed.
      RuntimeError -- the shared validator rejected the prospective seed state (the
                      single gate -- a half-set is_deprecated/deprecated_at_version
                      pair, or a deprecation_replacement set without is_deprecated, or
                      an unresolvable replacement name). The DB is byte-identical to
                      before.
      VersionResolveError / VersionRefusal / BaselineRefusal -- as apply_seeds raises."""
    # The seed cell conventions (policy.md / the export): is_deprecated is '1' when
    # set, '' when cleared; deprecation_replacement is written only as the caller
    # supplies it -- a None replacement clears the cell. The validator owns the
    # both-or-neither + replacement-requires-deprecated rules; db_editor only writes
    # the cells the caller asked for.
    edits = {
        "is_deprecated": "1" if is_deprecated else "",
        "deprecated_at_version": deprecated_at_version,
        "deprecation_replacement": deprecation_replacement,
    }
    return _drive_names_lifecycle_edit(
        out_dir, dll_path, kcdx_id, edits,
        action="deprecate", log=log, work_dir=work_dir, version=version,
        defer_commit=defer_commit, validate_only=validate_only)
