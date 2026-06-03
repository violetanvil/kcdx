"""app.routes_save -- the save (PREVIEW) endpoints (the six job shapes, design S6
US-3...US-8 / S7 save spine / S10 D8/D11/D12/D13; step 4b).

WHY PREVIEW-ONLY (the Save-previews / Confirm-transacts model)
--------------------------------------------------------------
Save is a PREVIEW: it VALIDATES the prospective edit through the data-core's gate +
returns the field-delta (`field: old -> new`) for the maintainer to review, and
writes NOTHING -- no DB write, no transaction, no held state. The maintainer reviews
the diff, then hits Confirm (step 5), which runs the WHOLE atomic transaction
synchronously in one request. Nothing touches the DB until Confirm; a Save (valid or
invalid) leaves the DB byte-identical. (plan-spec "Cross-step invariants" -- the
Save-previews/Confirm-transacts revision, user-settled 2026-06-03; this REPLACES the
earlier held-transaction model -- there is no registry, no executor-per-save, no
reaper.)

THIN CALLER (D13 / R3 / design S5 law 6): the backend computes NOTHING. It
deserializes the body, resolves the chosen version tag via the step-1 adapter
(`version=(tag, ordinal)`, NO DLL server-side -- the 1b seam), calls the data-core's
DRY-VALIDATE path (`validate_only=True` -- the validator runs, no DB write), computes
the field-delta via data_core.field_delta, and SURFACES the verdict. The VALIDATION
is the data-core's single gate (D13/D19 -- `validate_only=True` routes through
db_editor to import_to_sqlite.validate_direct_edit, which validates the PROSPECTIVE DB
STATE: the prospective DB rows -- the DB as it would be after the direct write -- run
through the same whole-state validator the direct write runs, before any DB write).
The backend reimplements no validation / SQL / delta / rule logic. An invalid edit
(the data-core's verdict) maps to `valid: false` + the validator's error, with NO
write (a preview never writes).

THE VERSION PASSING (the 1b seam, no DLL -- the settled fork)
-------------------------------------------------------------
The data-core write/validate functions accept EXACTLY ONE of `dll_path` / `version`.
The backend has no DLL server-side (D14/D15/D18), so every Save passes
`version=(ctx.tag, ctx.ordinal)` (from the adapter's `resolve_tag`) with no dll_path
-- the data-core skips the `.rdata` scan. An unknown tag is the adapter's
VersionTagError -> an HTTP 422 before the data-core is reached.

THE SIX JOB SHAPES -> THE DATA-CORE VALIDATE
--------------------------------------------
  re-verify / full-column UPDATE (US-3 / US-5) -> update_version_row  (one endpoint;
      both are an `edits` dict over an existing version row -- ONE endpoint covers
      both, the validator gates whichever cells the maintainer changed.)
  create version (US-6)        -> create_version    (AP18 new row + D12 nothing-changed)
  create entity (US-7)         -> create_entity      (AP18 new row)
  supersede (US-8 / Job 4)     -> supersede_entity   (UPDATE, not AP18-gated)
  deprecate (US-8 / Job 5)     -> deprecate_entity   (UPDATE, not AP18-gated)

Each call passes `validate_only=True`: the data-core validates the prospective edit
and returns the validator's verdict (+ the create flags read from the prospective
seed) WITHOUT opening or writing any DB.
"""
import logging
from collections import OrderedDict
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from . import data_core
from .adapter import resolve_tag, VersionTagError
from .config import load_config

# One module logger, event-driven (logging.md): a log fires on a save-preview rejection
# branch (an unknown tag, the data-core's validation reject) before the response
# returns -- never per request.
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
# the field delta the data-core computes -- the confirm gate's acceptance signal
# (D8). The backend assembles nothing: it forwards the two dicts to data_core.field_delta.
# ---------------------------------------------------------------------------
class _SaveBody(BaseModel):
    """The fields every save body shares: the version tag the edit targets (the
    adapter resolves it; NO DLL) + the saved/prospective record dicts for the
    response field delta."""
    version_tag: str
    saved: dict = {}
    prospective: dict = {}


class UpdateVersionSave(_SaveBody):
    """Re-verify / full-column UPDATE (US-3 / US-5): an `edits` dict over the existing
    version row identified by (kcdx_id, valid_from_version). `edits` keys must be
    editable version columns (the data-core's EDITABLE_VERSION_COLUMNS gate -- a
    non-editable / identity-key column is a DbEditError -> 422)."""
    kcdx_id: int
    valid_from_version: str
    edits: dict


class CreateVersionSave(_SaveBody):
    """Create a new version (US-6): a new (kcdx_id, valid_from_version) row for an
    EXISTING entity, its cells in `columns` (prefilled from a source row). AP18-gated
    + the D12 nothing-changed verdict."""
    kcdx_id: int
    valid_from_version: str
    columns: dict


class CreateEntitySave(_SaveBody):
    """Create a brand-new entity (US-7): a `name` + the first version row's authored
    cells (`first_version_columns`). The data-core assigns the next free kcdx_id
    (append-only). AP18-gated."""
    name: str
    first_version_columns: dict


class SupersedeSave(_SaveBody):
    """Supersede an entity (US-8 / Job 4): set superseded_by + superseded_at_version
    on the existing names row `kcdx_id`. An UPDATE -- not AP18-gated."""
    kcdx_id: int
    superseded_by: str
    superseded_at_version: str


class DeprecateSave(_SaveBody):
    """Deprecate an entity (US-8 / Job 5): set is_deprecated + deprecated_at_version
    (+ optional deprecation_replacement) on the names row `kcdx_id`. An UPDATE -- not
    AP18-gated. is_deprecated False + the versions cleared un-deprecates."""
    kcdx_id: int
    is_deprecated: bool = True
    deprecated_at_version: Optional[str] = None
    deprecation_replacement: Optional[str] = None


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
    the Confirm step's (step 5). So a rejection is surfaced as `valid: false` + the
    error (NOT an HTTP error -- an invalid edit is a normal preview outcome the s06
    surface renders, design D8), while a malformed edit shape or an unknown tag IS an
    HTTP error (a caller bug / bad input, not a validation verdict to show in the
    diff).

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
    # param, NO DLL (the 1b seam). An unknown tag is the maintainer's, surfaced as 422.
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
    """Assemble the VALID preview response: the field delta (D8 -- the maintainer's
    acceptance signal), `valid: true`, no errors, and any AP18 / nothing_changed /
    addition_kind markers the create shapes surfaced (D11/D12 -- read from the
    prospective seed at preview time, so the s06 confirm gate has them before any
    write). The update/lifecycle shapes carry no flags."""
    response = {
        "field_delta": _field_delta_list(body, record_kind),
        "valid": True,
        "errors": [],
    }
    # The create shapes surface AP18 / nothing_changed / addition_kind / the new
    # row's identity; key on what the flag dict carries, not the job -- so a new
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
    """Re-verify / full-column UPDATE (US-3 / US-5) -- PREVIEW. Validates the edit +
    returns the field delta for s06's confirm; writes NOTHING. NOT AP18-gated (an
    UPDATE to an existing row)."""
    return _run_preview(
        body, kind="update-version", record_kind="version",
        validate=lambda version: data_core.update_version_row(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.edits, version=version, validate_only=True))


@router.post("/save/create-version")
def save_create_version(body: CreateVersionSave):
    """Create a new version (US-6) for an existing entity -- PREVIEW. AP18-gated (a
    new row) + the D12 nothing-changed verdict -- both surface in the response for
    the confirm gate; writes NOTHING."""
    return _run_preview(
        body, kind="create-version", record_kind="version",
        validate=lambda version: data_core.create_version(
            load_config().out_dir, None, body.kcdx_id, body.valid_from_version,
            body.columns, version=version, validate_only=True))


@router.post("/save/create-entity")
def save_create_entity(body: CreateEntitySave):
    """Create a brand-new entity (US-7) -- PREVIEW. AP18-gated; the data-core assigns
    the next free kcdx_id (returned in the response). Writes NOTHING."""
    return _run_preview(
        body, kind="create-entity", record_kind="version",
        validate=lambda version: data_core.create_entity(
            load_config().out_dir, None, body.name, body.first_version_columns,
            version=version, validate_only=True))


@router.post("/save/supersede")
def save_supersede(body: SupersedeSave):
    """Supersede an entity (US-8 / Job 4) -- PREVIEW: superseded_by +
    superseded_at_version set together. An UPDATE -- NOT AP18-gated. record_kind is
    "names" (the field delta orders by the address_names header); writes NOTHING."""
    return _run_preview(
        body, kind="supersede", record_kind="names",
        validate=lambda version: data_core.supersede_entity(
            load_config().out_dir, None, body.kcdx_id, body.superseded_by,
            body.superseded_at_version, version=version, validate_only=True))


@router.post("/save/deprecate")
def save_deprecate(body: DeprecateSave):
    """Deprecate an entity (US-8 / Job 5) -- PREVIEW: is_deprecated +
    deprecated_at_version set together (+ optional deprecation_replacement). An UPDATE
    -- NOT AP18-gated. record_kind is "names"; writes NOTHING."""
    return _run_preview(
        body, kind="deprecate", record_kind="names",
        validate=lambda version: data_core.deprecate_entity(
            load_config().out_dir, None, body.kcdx_id,
            is_deprecated=body.is_deprecated,
            deprecated_at_version=body.deprecated_at_version,
            deprecation_replacement=body.deprecation_replacement,
            version=version, validate_only=True))
