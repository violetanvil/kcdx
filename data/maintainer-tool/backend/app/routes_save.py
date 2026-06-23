"""app.routes_save -- the save (PREVIEW) endpoints (the six job shapes), the preview
half of the Save-previews / Confirm-transacts flow.

WHY PREVIEW-ONLY (the Save-previews / Confirm-transacts model)
--------------------------------------------------------------
Save is a PREVIEW: it VALIDATES the prospective edit through the data-core's gate +
returns the field-delta (`field: old -> new`) for the maintainer to review, and
writes NOTHING -- no DB write, no transaction, no held state. The maintainer reviews
the diff, then hits Confirm, which runs the WHOLE atomic transaction synchronously in
one request. Nothing touches the DB until Confirm; a Save (valid or invalid) leaves
the DB byte-identical. (This is the Save-previews/Confirm-transacts model; it replaces
an earlier held-transaction model -- there is no registry, no executor-per-save, no
reaper.)

THIN CALLER: the backend computes NOTHING. It deserializes the body, resolves the
chosen version tag via the adapter (`version=(tag, ordinal)`, NO DLL server-side -- the
version-tag seam), calls the data-core's DRY-VALIDATE path (`validate_only=True` -- the
validator runs, no DB write), computes the field-delta via data_core.field_delta, and
SURFACES the verdict. The VALIDATION is the data-core's single gate (`validate_only=True`
routes through db_editor to import_to_sqlite.validate_direct_edit, which validates the
PROSPECTIVE DB STATE: the prospective DB rows -- the DB as it would be after the direct
write -- run through the same whole-state validator the direct write runs, before any DB
write). The backend reimplements no validation / SQL / delta / rule logic. An invalid
edit (the data-core's verdict) maps to `valid: false` + the validator's error, with NO
write (a preview never writes).

THE VERSION PASSING (the version-tag seam, no DLL -- the settled fork)
---------------------------------------------------------------------
The data-core write/validate functions accept EXACTLY ONE of `dll_path` / `version`.
The backend has no DLL server-side, so every Save passes `version=(ctx.tag, ctx.ordinal)`
(from the adapter's `resolve_tag`) with no dll_path -- the data-core skips the `.rdata`
scan. An unknown tag is the adapter's VersionTagError -> an HTTP 422 before the data-core
is reached.

THE SIX JOB SHAPES -> THE DATA-CORE VALIDATE
--------------------------------------------
  re-verify / full-column UPDATE -> update_version_row  (one endpoint; both are an
      `edits` dict over an existing version row -- ONE endpoint covers both, the
      validator gates whichever cells the maintainer changed.)
  create version       -> create_version    (new row, needs approval + nothing-changed)
  create entity        -> create_entity      (new row, needs approval)
  supersede            -> supersede_entity   (UPDATE, no new-row approval required)
  deprecate            -> deprecate_entity   (UPDATE, no new-row approval required)

Each call passes `validate_only=True`: the data-core validates the prospective edit
and returns the validator's verdict (+ the create flags read from the prospective
seed) WITHOUT opening or writing any DB.
"""
import logging
from collections import OrderedDict
from typing import List, Optional

from fastapi import APIRouter, Header, HTTPException
from pydantic import BaseModel

from . import data_core
from .adapter import resolve_tag, VersionTagError
from .config import load_config

# One module logger, event-driven: a log fires on a save-preview rejection branch (an
# unknown tag, the data-core's validation reject) before the response returns -- never
# per request.
log = logging.getLogger(__name__)

router = APIRouter()

# WHY two record_kind tokens -> two field_order constants for the response delta:
# picking the constant is PARAMETER SELECTION, not computation (mirrors
# routes_delta). A version-row save orders the returned delta by the
# address_versions header; a lifecycle (names-row) save by the address_names header.
_FIELD_ORDER_BY_KIND = {
    "version": data_core.ADDRESS_VERSIONS_CSV_HEADER,
    "names": data_core.ADDRESS_NAMES_CSV_HEADER,
}


# ---------------------------------------------------------------------------
# Request bodies -- the record/edit fields + the chosen version_tag, one per shape.
# Each carries `saved` + `prospective` seed-row dicts ({column: cell}, '' for NULL,
# the form's was/current values the client already holds) so the response can echo
# the field delta the data-core computes -- the confirm gate's acceptance signal. The
# backend assembles nothing: it forwards the two dicts to data_core.field_delta.
# ---------------------------------------------------------------------------
class _SaveBody(BaseModel):
    """The fields every save body shares: the version tag the edit targets (the
    adapter resolves it; NO DLL) + the saved/prospective record dicts for the
    response field delta."""
    version_tag: str
    saved: dict = {}
    prospective: dict = {}


class UpdateVersionSave(_SaveBody):
    """Re-verify / full-column UPDATE: an `edits` dict over the existing version row
    identified by (kcdx_id, valid_from_version). `edits` keys must be editable version
    columns (the data-core's EDITABLE_VERSION_COLUMNS gate -- a non-editable /
    identity-key column is a DbEditError -> 422)."""
    kcdx_id: int
    valid_from_version: str
    edits: dict


class CreateVersionSave(_SaveBody):
    """Create a new version: a new (kcdx_id, valid_from_version) row for an EXISTING
    entity, its cells in `columns` (prefilled from a source row). Adding a new row
    requires explicit maintainer approval + carries the nothing-changed verdict."""
    kcdx_id: int
    valid_from_version: str
    columns: dict


class CreateEntitySave(_SaveBody):
    """Create a brand-new entity: a `name` + the first version row's authored cells
    (`first_version_columns`). The data-core assigns the next free kcdx_id (append-only).
    Adding a new entity requires explicit maintainer approval."""
    name: str
    first_version_columns: dict


class SupersedeSave(_SaveBody):
    """Supersede an entity: set superseded_by + superseded_at_version on the existing
    names row `kcdx_id`. An UPDATE -- no new-row approval required."""
    kcdx_id: int
    superseded_by: str
    superseded_at_version: str


class DeprecateSave(_SaveBody):
    """Deprecate an entity: set is_deprecated + deprecated_at_version (+ optional
    deprecation_replacement) on the names row `kcdx_id`. An UPDATE -- no new-row approval
    required. is_deprecated False + the versions cleared un-deprecates."""
    kcdx_id: int
    is_deprecated: bool = True
    deprecated_at_version: Optional[str] = None
    deprecation_replacement: Optional[str] = None


class EditNotesSave(_SaveBody):
    """Edit an entity's curated `notes` prose (the names row `kcdx_id`): a standalone
    free-text column, no pair-integrity rule. An UPDATE -- no new-row approval required.
    `notes` '' clears the cell."""
    kcdx_id: int
    notes: str


# ---------------------------------------------------------------------------
# The shared drive: resolve the tag, run the chosen data-core DRY-VALIDATE (no DB
# write), and assemble the PREVIEW response. EVERY endpoint is a thin wrapper that
# supplies its data-core call as a closure + its body's record_kind.
# ---------------------------------------------------------------------------
def _run_preview(body, *, kind, record_kind, validate):
    """Validate one prospective edit (NO write) and return the preview response.

    `validate(version)` is a closure invoking the chosen db_editor function with
    `validate_only=True`, `dll_path=None`, `version=`, and the body's edit fields --
    the data-core's DRY-VALIDATE path: it builds the prospective seed (export the
    current DB + apply the edit) and runs the SAME validation gate apply_seeds runs,
    WITHOUT opening or writing any DB. A valid edit returns the validator's verdict
    (the update/lifecycle shapes) OR a flag dict whose "result" is that verdict (the
    create shapes); an invalid edit RAISES the validator's error. `record_kind`
    selects the field-delta order; `kind` is the save's job label.

    WHY this is preview-only: nothing here writes, opens a transaction, or holds
    state. A Save shows "here is what will change, and it validates"; the write is
    the Confirm step's. So a rejection is surfaced as `valid: false` + the error (NOT
    an HTTP error -- an invalid edit is a normal preview outcome the edit surface
    renders), while a malformed edit shape or an unknown tag IS an HTTP error (a caller
    bug / bad input, not a validation verdict to show in the diff).

    Failure modes (NONE writes -- a preview never touches the DB):
      VersionTagError      -> HTTP 422 (the maintainer picked an unknown tag -- bad
                              input, surfaced before the data-core is reached).
      DbEditError          -> HTTP 422 (a malformed edit shape -- the caller's bug:
                              an unknown/non-editable column, a stale identity key).
      RuntimeError / etc.  -> `valid: false` + the error (the data-core's validation
                              VERDICT -- a duplicate tuple, a partial trio, a
                              supersession cycle, a missing required column). This is
                              the preview's job: show the maintainer why the edit is
                              invalid, in the same response as the field-delta.
    """
    config = load_config()

    # Resolve the chosen version tag to (tag, ordinal) -- the data-core's `version=`
    # param, NO DLL (the version-tag seam). An unknown tag is the maintainer's,
    # surfaced as 422.
    try:
        ctx = resolve_tag(config, body.version_tag)
    except VersionTagError as exc:
        log.warning("save preview rejected -- unknown version tag (kind=%s, tag=%s): %s",
                    kind, body.version_tag, exc)
        raise HTTPException(status_code=422, detail=str(exc))

    version = (ctx.tag, ctx.ordinal)

    # Run the data-core DRY-VALIDATE. A malformed edit SHAPE (DbEditError) is the
    # caller's bug -> 422 before the validator. The validator's own verdict on the
    # prospective state (RuntimeError / VersionRefusal / VersionResolveError --
    # VersionRefusal/BaselineRefusal subclass RuntimeError) is surfaced as
    # valid: false in the preview, NOT an HTTP error. The backend computes NOTHING --
    # the closure just selects params; the data-core's gate is the single validator.
    flags = {}
    try:
        result = validate(version)
    except data_core.DbEditError as exc:
        log.warning("save preview rejected -- malformed edit (kind=%s): %s", kind, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except data_core.VersionResolveError as exc:
        # A version refusal on the supplied (tag, ordinal). The tag resolved through
        # the adapter, so this is the data-core's own version verdict -> show it as
        # an invalid preview, not a caller bug.
        log.warning("save preview invalid -- version refusal (kind=%s): %s", kind, exc)
        return _invalid_response(body, record_kind, str(exc))
    except (ValueError, RuntimeError) as exc:
        # The shared validator's verdict (a duplicate tuple, a partial trio, a
        # supersession cycle, a missing required column, ...) -- the data-core's gate,
        # surfaced verbatim. NO DB write happened (validate_only never opens a DB).
        log.warning("save preview invalid -- validator (kind=%s): %s", kind, exc)
        return _invalid_response(body, record_kind, str(exc))

    # A create shape returns a flag dict ({"result": verdict, "ap18_new_row", ...});
    # the update/lifecycle shapes return the verdict directly. Capture the flags (if
    # any) for the response -- read from the prospective seed, valid at preview time.
    if isinstance(result, dict) and "ap18_new_row" in result:
        flags = result

    return _valid_response(body, record_kind, flags)


def _field_delta_list(body, record_kind):
    """The field delta as an ordered LIST -- the data-core's `field_delta` over the
    body's saved/prospective dicts (the SAME shaping routes_delta serves), in the
    data-core's deterministic header order (a JSON array preserves it, an object's
    key order does not -- the same boundary as routes_delta)."""
    field_order = _FIELD_ORDER_BY_KIND[record_kind]
    delta: "OrderedDict" = data_core.field_delta(
        body.saved, body.prospective, field_order=field_order)
    return [{"field": field, "old": old, "new": new}
            for field, (old, new) in delta.items()]


def _valid_response(body, record_kind, flags):
    """Assemble the VALID preview response: the field delta (the maintainer's acceptance
    signal), `valid: true`, no errors, and any new-row / nothing_changed / addition_kind
    markers the create shapes surfaced (read from the prospective seed at preview time, so
    the confirm gate has them before any write). The update/lifecycle shapes carry no
    flags."""
    response = {
        "field_delta": _field_delta_list(body, record_kind),
        "valid": True,
        "errors": [],
    }
    # The create shapes surface the new-row / nothing_changed / addition_kind markers /
    # the new row's identity; key on what the flag dict carries, not the job -- so a new
    # create-shape's flags surface with no change here.
    for flag in ("ap18_new_row", "nothing_changed", "addition_kind",
                 "kcdx_id", "valid_from_version", "name"):
        if flag in flags:
            response[flag] = flags[flag]
    return response


def _invalid_response(body, record_kind, error):
    """Assemble the INVALID preview response: the field delta (still shown -- the
    maintainer sees WHAT they tried to change alongside WHY it is invalid), `valid:
    false`, and the validator's error. No flags (the create-shape flags only surface
    on a valid prospective row -- the data-core raises before returning them)."""
    return {
        "field_delta": _field_delta_list(body, record_kind),
        "valid": False,
        "errors": [error],
    }


# ---------------------------------------------------------------------------
# The six endpoints. Each deserializes its body, supplies its data-core DRY-VALIDATE
# as a closure (validate_only=True, dll_path=None, version=), and returns the preview.
# ---------------------------------------------------------------------------
@router.post("/save/update-version")
def save_update_version(body: UpdateVersionSave):
    """Re-verify / full-column UPDATE -- PREVIEW. Validates the edit + returns the field
    delta for the confirm gate; writes NOTHING. No new-row approval required (an UPDATE to
    an existing row)."""
    return _run_preview(
        body, kind="update-version", record_kind="version",
        validate=lambda version: data_core.update_version_row(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.edits, version=version, validate_only=True))


@router.post("/save/create-version")
def save_create_version(body: CreateVersionSave):
    """Create a new version for an existing entity -- PREVIEW. Adding a new row requires
    explicit maintainer approval + the nothing-changed verdict -- both surface in the
    response for the confirm gate; writes NOTHING."""
    return _run_preview(
        body, kind="create-version", record_kind="version",
        validate=lambda version: data_core.create_version(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.columns, version=version, validate_only=True))


@router.post("/save/create-entity")
def save_create_entity(body: CreateEntitySave):
    """Create a brand-new entity -- PREVIEW. Adding a new entity requires explicit
    maintainer approval; the data-core assigns the next free kcdx_id (returned in the
    response). Writes NOTHING."""
    return _run_preview(
        body, kind="create-entity", record_kind="version",
        validate=lambda version: data_core.create_entity(
            load_config().out_dir, None, body.name, body.first_version_columns,
            version=version, validate_only=True))


@router.post("/save/supersede")
def save_supersede(body: SupersedeSave):
    """Supersede an entity -- PREVIEW: superseded_by + superseded_at_version set together.
    An UPDATE -- no new-row approval required. record_kind is "names" (the field delta
    orders by the address_names header); writes NOTHING."""
    return _run_preview(
        body, kind="supersede", record_kind="names",
        validate=lambda version: data_core.supersede_entity(
            load_config().out_dir, None, body.kcdx_id, body.superseded_by,
            body.superseded_at_version, version=version, validate_only=True))


@router.post("/save/deprecate")
def save_deprecate(body: DeprecateSave):
    """Deprecate an entity -- PREVIEW: is_deprecated + deprecated_at_version set together
    (+ optional deprecation_replacement). An UPDATE -- no new-row approval required.
    record_kind is "names"; writes NOTHING."""
    return _run_preview(
        body, kind="deprecate", record_kind="names",
        validate=lambda version: data_core.deprecate_entity(
            load_config().out_dir, None, body.kcdx_id,
            is_deprecated=body.is_deprecated,
            deprecated_at_version=body.deprecated_at_version,
            deprecation_replacement=body.deprecation_replacement,
            version=version, validate_only=True))


@router.post("/save/edit-notes")
def save_edit_notes(body: EditNotesSave):
    """Edit an entity's curated `notes` (the names row) -- PREVIEW: validate the prospective
    notes edit + return the field delta; writes NOTHING. An UPDATE -- no new-row approval
    required. record_kind is "names" (the field delta orders by the address_names header).
    notes has no pair-integrity rule, so a valid preview needs only the entity to exist + the
    prospective DB state to validate (the data-core's gate)."""
    return _run_preview(
        body, kind="edit-notes", record_kind="names",
        validate=lambda version: data_core.edit_notes(
            load_config().out_dir, None, body.kcdx_id, body.notes,
            version=version, validate_only=True))


# ---------------------------------------------------------------------------
# The bulk re-verify PREVIEW endpoint (the "Batch mutation" surface). The FE sends the
# v3 report's actionable rows + the batch action (verify-all | close-intervals) + the
# auth-ready identity context; this endpoint routes them through the data-core
# `reverify_resolver` (which reads the DB + computes the per-row edit-specs), then returns
# the per-row FIELD-DELTAS (`field: old -> new`) for the batched-confirm UI. PREVIEW-ONLY:
# it READS the DB (the resolver opens it READ-ONLY) + computes; it WRITES NOTHING, opens
# NO transaction (the same Save-previews / Confirm-transacts contract the six single-edit
# /save/* previews honor -- git is invisible to the maintainer). The FE then POSTs the
# SAME computed edits to /confirm/batch to transact (the write mechanism is unchanged;
# only the edit-spec SOURCE moved to the resolver).
# ---------------------------------------------------------------------------
class ReportRow(BaseModel):
    """One actionable v3-report row the resolver reads (the subset it needs -- the report
    schema's full row carries more). The FE forwards these from the imported report; the
    resolver attributes each to its matched/target address_versions row and computes the
    edit-spec. matched_address_version_id is an int on a verified-block row, None on a
    failed row (the report's attribution invariant)."""
    kcdx_id: int
    version: str
    verdict: str
    method_rank: int
    matched_address_version_id: Optional[int] = None


class ReverifyBatchPreview(BaseModel):
    """The /save/reverify-batch request: the batch ACTION + the report rows for that
    block + the auth-ready identity context (the verified_by the verify-all trio writes is
    the resolved author, the same identity that authors the eventual git commit).
    `version_tag` is unused by the resolve (each row carries its own resolved version);
    kept off the body -- the row's `version` is the swept tag."""
    action: str                       # "verify-all" | "close-intervals"
    rows: List[ReportRow]
    author_name: Optional[str] = None
    author_email: Optional[str] = None


@router.post("/save/reverify-batch")
def save_reverify_batch(
        body: ReverifyBatchPreview,
        x_kcdx_author_name: Optional[str] = Header(default=None),
        x_kcdx_author_email: Optional[str] = Header(default=None)):
    """Bulk re-verify PREVIEW: resolve the report rows for ONE batch action into per-row
    edit-specs (the data-core `reverify_resolver`), and return the per-row field-deltas
    for the batched confirm. Writes NOTHING (the resolver reads READ-ONLY + computes; no
    DB write, no transaction). The FE posts the returned edits to /confirm/batch to
    transact.

    The response shape (the batch field-delta list the FE renders -- one UNIFIED `rows`
    list, each row carrying a `status` classification):
      {"action": str,
       "rows": [
         # an ACTIONABLE row (the resolver produced an edit-spec):
         {"status": "actionable", "kcdx_id": int, "valid_from_version": str,
          "field_delta": [{"field", "old", "new"}, ...], "edits": {col: new, ...}},
         # an ALREADY-ACTED row (the resolver produced NO spec -- the recommended
         # action is already reflected in the DB, so there is nothing to confirm):
         {"status": "already_acted", "kcdx_id": int, "version": str,
          "reason": "interval already closed" | "already current"}, ...]}
    An `actionable` row's `edits` is the BatchRowSpec `edits` the FE re-posts to
    /confirm/batch UNCHANGED; `field_delta` is the human acceptance signal. An
    `already_acted` row carries its identity + a human marker, NO `field_delta`/`edits`
    (nothing to confirm, never in any batch) -- the worklist moves it to its "no further
    action" state by READING this classification, never re-deriving it client-side (the FE
    reads the classification, never computes it). The already-acted rows are the two
    resolver silent-skips surfaced explicitly: verify-all's already-covered skip
    (last_verified >= swept -- "already current") and close-intervals' already-closed skip
    (valid_through == last_verified -- "interval already closed"); EVERY other no-spec path
    is a ReverifyResolveError (-> 422), so a no-spec input row is unambiguously
    already-acted.

    Failure modes (NONE writes):
      bad action          -> HTTP 422 (a caller bug -- an action the resolver does not
                             know).
      ReverifyResolveError -> HTTP 422 (a structural report-vs-DB mismatch: a stale
                             matched id, an unknown report version, a missing
                             close-target -- the caller surfaces it; the report is
                             stale/foreign, a maintainer-visible problem, not a silent
                             skip).
      DbReadError         -> HTTP 422 (no curated DB resolved -- the empty-state
                             read-path counterpart)."""
    config = load_config()
    # The injected commit identity -- the SAME resolution the /confirm endpoints use
    # (body field > header > configured maintainer identity), so the verify-all trio's
    # verified_by is the identity that will author the commit. Imported lazily to avoid
    # the routes_confirm <-> routes_save import cycle (routes_confirm imports the save
    # bodies from here).
    from .routes_confirm import _resolve_author
    author_name, _author_email = _resolve_author(
        body.author_name, body.author_email, x_kcdx_author_name, x_kcdx_author_email)

    # The report rows as plain dicts the resolver reads (the BaseModel -> dict; the
    # resolver indexes by key, not attribute -- the data-core takes no Pydantic dep).
    report_rows = [r.model_dump() for r in body.rows]

    try:
        specs = data_core.resolve_reverify_batch(
            config.out_dir, report_rows, action=body.action,
            verified_by=author_name)
    except data_core.ReverifyResolveError as exc:
        # A structural report-vs-DB mismatch (a stale matched id, an unknown version, a
        # missing close-target) OR a bad action -- a caller/input problem the FE surfaces.
        # NO write happened (the resolver only reads). Log before returning.
        log.warning("reverify-batch preview rejected (action=%s): %s", body.action, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except data_core.DbReadError as exc:
        log.warning("reverify-batch preview rejected -- no curated DB (action=%s): %s",
                    body.action, exc)
        raise HTTPException(status_code=422, detail=str(exc))

    # Shape each resolved edit-spec into the FE's batch field-delta row: the field-delta
    # (field_delta over the resolver-returned saved/edits, ordered by the version header)
    # + the edits to re-post + the row identity. PURE -- field_delta is a no-I/O
    # description (the data-core's, reused); this endpoint computes nothing else.
    rows_out = []
    acted_keys = set()
    for spec in specs:
        delta = data_core.field_delta(
            spec["saved"], spec["edits"],
            field_order=data_core.ADDRESS_VERSIONS_CSV_HEADER)
        rows_out.append({
            "status": "actionable",
            "kcdx_id": spec["kcdx_id"],
            "valid_from_version": spec["valid_from_version"],
            "field_delta": [{"field": f, "old": old, "new": new}
                            for f, (old, new) in delta.items()],
            "edits": spec["edits"],
        })
        acted_keys.add(spec["kcdx_id"])

    # CLASSIFY the already-acted rows: the resolver produces NO spec for a row whose
    # recommended action is already reflected in the DB (verify-all's already-covered
    # skip, last_verified >= swept; close-intervals' already-closed skip, valid_through ==
    # last_verified). EVERY other no-spec path is a ReverifyResolveError (handled above ->
    # 422), so an input row that produced no spec is UNAMBIGUOUSLY already-acted. The match
    # key is `kcdx_id`: the resolver keys each spec by its target row's kcdx_id
    # (`spec["kcdx_id"]`), one spec per input row at most (verify-all attributes by
    # matched_address_version_id -> exactly one row; close-intervals resolves the single
    # interval-containing row of kcdx_id). The endpoint does NOT recompute the skip -- it
    # surfaces it (the resolver stays untouched, it owns the skip logic). The marker is by
    # action: close-intervals -> "interval already closed"; verify-all -> "already
    # current".
    reason = ("interval already closed" if body.action == "close-intervals"
              else "already current")
    for r in body.rows:
        if r.kcdx_id not in acted_keys:
            rows_out.append({
                "status": "already_acted",
                "kcdx_id": r.kcdx_id,
                "version": r.version,
                "reason": reason,
            })

    return {"action": body.action, "rows": rows_out}
