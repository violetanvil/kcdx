"""app.routes_save -- the save endpoints (the six job shapes, design §6 US-3…US-8 /
§7 save spine / §10 D11/D12/D13/D16; step 4b).

The save API that drives the data-core write path for ALL SIX job shapes, each
through the deferred-commit seam (step 4a). A save runs validate -> write -> export ->
round-trip INSIDE an open transaction and HOLDS it uncommitted, returning the result +
the field delta + the AP18/D12 flags for the confirm gate; the commit (the held txn +
the git commit) is step 5, a cancel ROLLBACKs (`app.pending_saves` holds the txn,
keyed by save_id, for the step-5 confirm/cancel endpoints).

THIN CALLER (D13 / R3 / design §5 law 6): the backend computes NOTHING. It
deserializes the body, resolves the chosen version tag via the step-1 adapter
(`version=(tag, ordinal)`, NO DLL server-side -- the 1b seam), calls the data-core
write with `defer_commit=True`, registers the held handle, and SURFACES what the
data-core returns (the apply result + the AP18 / nothing_changed flags + the
field-delta the data-core's `field_delta` computes). No validation / SQL / delta /
rule logic here -- the data-core's gate validates + writes; `field_delta` is the
data-core's. A validation failure (the data-core's verdict) maps to an HTTP error
with NO write and NO held txn (the save handle never registers).

THE VERSION PASSING (the 1b seam, no DLL -- the settled fork)
-------------------------------------------------------------
The data-core write functions accept EXACTLY ONE of `dll_path` / `version`. The
backend has no DLL server-side (D14/D15/D18), so every save passes
`version=(ctx.tag, ctx.ordinal)` (from the adapter's `resolve_tag`) with no dll_path
-- the data-core skips the `.rdata` scan. (The earlier "how does the tag thread into
the write" fork is settled: 1b added `version=`; the adapter resolves the context;
this endpoint passes it.)

THE SIX JOB SHAPES -> THE DATA-CORE WRITE
-----------------------------------------
  re-verify / full-column UPDATE (US-3 / US-5) -> update_version_row  (one endpoint;
      both are an `edits` dict over an existing version row -- a re-verify edits the
      four trio cells, a full-column edits any editable cells. ONE endpoint covers
      both: the job is the same write shape, the `edits` content is the only
      difference, and the validator gates whichever cells the maintainer changed.)
  create version (US-6)        -> create_version    (AP18 new row + D12 nothing-changed)
  create entity (US-7)         -> create_entity      (AP18 new row)
  supersede (US-8 / Job 4)     -> supersede_entity   (UPDATE, not AP18-gated)
  deprecate (US-8 / Job 5)     -> deprecate_entity   (UPDATE, not AP18-gated)
"""
import logging
from collections import OrderedDict
from typing import Optional

from fastapi import APIRouter, HTTPException
from pydantic import BaseModel

from . import data_core
from .adapter import resolve_tag, VersionTagError
from .config import load_config
from .pending_saves import REGISTRY

# One module logger, event-driven (logging.md): a log fires on the save-failure
# branch (the data-core's rejection) before the HTTP error returns -- never per
# request. The held-save lifecycle (register/discard) logs in app.pending_saves.
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
# The shared drive: resolve the tag, run the chosen data-core write under a held
# deferred-commit txn (on the registry's per-save executor -- thread affinity),
# register the handle, and assemble the response. EVERY endpoint is a thin wrapper
# that supplies its data-core call as a closure + its body's record_kind.
# ---------------------------------------------------------------------------
def _run_save(body, *, kind, record_kind, write):
    """Drive one deferred-commit save end-to-end and return the response dict.

    `write(out_dir, version)` is a closure invoking the chosen db_editor write with
    `defer_commit=True`, `dll_path=None`, and the body's edit fields -- it RUNS ON THE
    registry's per-save single-thread executor (so the SQLite connections it opens
    belong to that one thread; the step-5 commit/rollback runs on the same thread --
    the thread-affinity invariant the held connections require, see app.pending_saves).
    `record_kind` selects the field-delta order; `kind` is the save's job label.

    Maps the data-core's failure modes to HTTP errors (NO write, NO held txn on any
    of them -- the data-core validates before opening a DB, and rolls back + closes on
    any deferred-mode error):
      VersionTagError                -> 422 (the maintainer picked an unknown tag)
      DbEditError                    -> 422 (a malformed edit shape -- the caller's bug)
      RoundTripError / RuntimeError  -> 422 (the shared validator rejected the state)
      VersionResolveError            -> 422 (a version/baseline refusal)
    Each logs before the HTTP error returns (logging.md). A success returns 200 with
    {save_id, field_delta, + the data-core's surfaced flags}; the txn stays HELD.
    """
    config = load_config()

    # Resolve the chosen version tag to (tag, ordinal) -- the data-core's `version=`
    # param, NO DLL (the 1b seam). An unknown tag is the maintainer's, surfaced as 422.
    try:
        ctx = resolve_tag(config, body.version_tag)
    except VersionTagError as exc:
        log.warning("save rejected -- unknown version tag (kind=%s, tag=%s): %s",
                    kind, body.version_tag, exc)
        raise HTTPException(status_code=422, detail=str(exc))

    version = (ctx.tag, ctx.ordinal)
    meta = {"kind": kind, "version_tag": ctx.tag}

    # Run the data-core write under a held deferred-commit txn ON the registry's
    # per-save executor. A validation/refusal failure raises on the executor thread:
    # NO write, NO held txn, NO save_id registered (run_save tears down the executor
    # + re-raises). The backend computes NOTHING -- the closure just selects params.
    try:
        save_id, result = REGISTRY.run_save(
            lambda: write(config.out_dir, version), meta)
    except data_core.DbEditError as exc:
        log.warning("save rejected -- malformed edit (kind=%s): %s", kind, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except data_core.RoundTripError as exc:
        log.warning("save rejected -- round-trip divergence (kind=%s): %s", kind, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except data_core.VersionResolveError as exc:
        log.warning("save rejected -- version refusal (kind=%s): %s", kind, exc)
        raise HTTPException(status_code=422, detail=str(exc))
    except (ValueError, RuntimeError) as exc:
        # The shared validator's verdict (a duplicate tuple, a partial trio, a
        # supersession cycle, a missing required column, ...) surfaces as the
        # applier's RuntimeError -- the data-core's gate, surfaced verbatim. (A
        # VersionRefusal / BaselineRefusal subclass RuntimeError, so they land here.)
        log.warning("save rejected -- validator (kind=%s): %s", kind, exc)
        raise HTTPException(status_code=422, detail=str(exc))

    return _response(save_id, result, body, record_kind)


def _response(save_id, result, body, record_kind):
    """Assemble the save response: the save_id (the held-txn token the step-5
    confirm/cancel resolve), the field delta (D8 -- the data-core's `field_delta`
    over the body's saved/prospective dicts, the SAME shaping routes_delta serves),
    and any AP18 / nothing_changed / addition_kind flags the data-core surfaced (the
    create writes return a {"result": handle, "ap18_new_row", ...} flag dict; the
    lifecycle writes return a {"result": handle, "action", "kcdx_id"} dict (kcdx_id
    surfaces, no AP18 flag); update_version_row returns a bare handle (no flags)."""
    field_order = _FIELD_ORDER_BY_KIND[record_kind]
    delta: "OrderedDict" = data_core.field_delta(
        body.saved, body.prospective, field_order=field_order)

    response = {
        "save_id": save_id,
        # The field delta as an ordered LIST (the order is load-bearing -- the
        # data-core's deterministic header order; a JSON array preserves it, an
        # object's key order does not. Same boundary as routes_delta.)
        "field_delta": [{"field": field, "old": old, "new": new}
                        for field, (old, new) in delta.items()],
    }

    # The create writes (create_version / create_entity) return a flag dict; surface
    # its AP18 / nothing_changed / addition_kind markers for the confirm gate (D11/D12).
    # The update/lifecycle writes return the handle directly (no flags). Keys on the
    # dict shape, not the job -- so a new create-shape's flags surface with no change.
    if isinstance(result, dict):
        for flag in ("ap18_new_row", "nothing_changed", "addition_kind",
                     "kcdx_id", "valid_from_version", "name"):
            if flag in result:
                response[flag] = result[flag]

    return response


# ---------------------------------------------------------------------------
# The six endpoints. Each deserializes its body, supplies its data-core write as a
# closure (defer_commit=True, dll_path=None, version=), and returns the response.
# ---------------------------------------------------------------------------
@router.post("/save/update-version")
def save_update_version(body: UpdateVersionSave):
    """Re-verify / full-column UPDATE (US-3 / US-5). Holds a deferred-commit txn over
    the one-row UPDATE; the field delta + save_id feed s06's confirm. NOT AP18-gated
    (an UPDATE to an existing row)."""
    return _run_save(
        body, kind="update-version", record_kind="version",
        write=lambda out_dir, version: data_core.update_version_row(
            out_dir, None, body.kcdx_id, body.valid_from_version, body.edits,
            version=version, defer_commit=True))


@router.post("/save/create-version")
def save_create_version(body: CreateVersionSave):
    """Create a new version (US-6) for an existing entity. AP18-gated (a new row) +
    the D12 nothing-changed verdict -- both surface in the response for the confirm
    gate. Holds the deferred-commit txn over the append."""
    return _run_save(
        body, kind="create-version", record_kind="version",
        write=lambda out_dir, version: data_core.create_version(
            out_dir, None, body.kcdx_id, body.valid_from_version, body.columns,
            version=version, defer_commit=True))


@router.post("/save/create-entity")
def save_create_entity(body: CreateEntitySave):
    """Create a brand-new entity (US-7). AP18-gated; the data-core assigns the next
    free kcdx_id (returned in the response). Holds the deferred-commit txn over the
    names + first-version append."""
    return _run_save(
        body, kind="create-entity", record_kind="version",
        write=lambda out_dir, version: data_core.create_entity(
            out_dir, None, body.name, body.first_version_columns,
            version=version, defer_commit=True))


@router.post("/save/supersede")
def save_supersede(body: SupersedeSave):
    """Supersede an entity (US-8 / Job 4): superseded_by + superseded_at_version set
    together. An UPDATE -- NOT AP18-gated. Holds the deferred-commit txn; record_kind
    is "names" (the field delta orders by the address_names header)."""
    return _run_save(
        body, kind="supersede", record_kind="names",
        write=lambda out_dir, version: data_core.supersede_entity(
            out_dir, None, body.kcdx_id, body.superseded_by,
            body.superseded_at_version, version=version, defer_commit=True))


@router.post("/save/deprecate")
def save_deprecate(body: DeprecateSave):
    """Deprecate an entity (US-8 / Job 5): is_deprecated + deprecated_at_version set
    together (+ optional deprecation_replacement). An UPDATE -- NOT AP18-gated. Holds
    the deferred-commit txn; record_kind is "names"."""
    return _run_save(
        body, kind="deprecate", record_kind="names",
        write=lambda out_dir, version: data_core.deprecate_entity(
            out_dir, None, body.kcdx_id, is_deprecated=body.is_deprecated,
            deprecated_at_version=body.deprecated_at_version,
            deprecation_replacement=body.deprecation_replacement,
            version=version, defer_commit=True))
