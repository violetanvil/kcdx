"""app.routes_read -- the read-for-display endpoints (US-1 / US-2 / US-9, design D13).

The browse/view API the frontend's s01 (navigator), s02 (entity detail), and s03
(version history / compare) bind. Each endpoint is a THIN CALLER of the data-core
read seam (seeds_shared.read_api, landed step 2a): it resolves the configured
checkout via app.config, calls the 2a function with config.out_dir, and returns
what that function returns -- FastAPI serializes the dict/list to JSON.

The backend computes NOTHING here (D13 / R3 / design S5 law 6): no status
derivation, no SQL, no rule logic. The derived `status` on each row arrives
already computed from the data-core; this module only surfaces it. A read router
sits in its own module (not main.py) per structure-by-responsibility -- main.py is
the app + health coordinator; the read routes are one responsibility-unit.

THE NO-DB SIGNAL (design §7 / s01 §States "Empty -- no DB/seeds resolved")
--------------------------------------------------------------------------
read_api raises DbReadError when the configured checkout resolves no curated DB.
The s01 empty-state is a SYSTEM-CAUSED app STATE the frontend renders with a
named-cause copy -- NOT an HTTP error page. So the endpoint mirrors /health's
contract exactly: HTTP 200 with the {state, detail} signal (state="empty"), the
same machine signal s01 already binds for /health. The frontend renders the copy;
the endpoint provides the state token + the named cause. (A genuine not-found --
an unknown kcdx_id -- IS a real HTTP error: 404, per the 2a None contract.)
"""
import logging

from fastapi import APIRouter

from . import data_core
from .config import load_config


def _json_safe(value):
    """The API edge's serialization seam -- recurse a data-core return into a
    JSON-transportable shape, deriving/reshaping NOTHING. The read endpoints return
    the data-core's plain dicts/lists of JSON-native scalars (int / str / None), so
    this only walks the nested structure and passes every scalar through unchanged.
    Its job is the boundary contract, not a transform: it marks (and the endpoint
    tests assert) that the backend reshapes nothing the data-core returned (D13/R3 --
    the backend computes nothing). The recursion is the structural guarantee; a
    scalar is returned as-is."""
    if isinstance(value, dict):
        return {k: _json_safe(v) for k, v in value.items()}
    if isinstance(value, list):
        return [_json_safe(v) for v in value]
    return value

# One module logger, event-driven: a log fires on the DB-read failure branch, never
# per request (logging.md -- every failure logged, no per-request spam).
log = logging.getLogger(__name__)

router = APIRouter()


def _no_db_signal(exc, config):
    """The s01 "no DB resolved" empty-state signal -- the read-path counterpart to
    /health's {state, detail}. Logs the data-core read failure (logging.md: the
    failure branch logs before it returns, naming the seam failure + the checkout
    path it looked at) THEN returns the state the frontend binds. Returned as a 200
    body (not an HTTP error) because the empty state is a normal app state s01
    renders with named-cause copy, not an error page (design §7 / s01 §States)."""
    log.warning(
        "data-core read failed: no curated DB at the configured checkout "
        "(checkout_path=%s, out_dir=%s): %s",
        config.checkout_path, config.out_dir, exc)
    return {
        "state": "empty",                      # "empty" -- mirrors /health's state token
        "detail": ("no reference DB resolved at the configured checkout path; "
                   f"{exc}"),                   # the named cause s01 surfaces
        "checkout_path": config.checkout_path,
        "out_dir": config.out_dir,
    }


@router.get("/entities")
def list_entities():
    """The curated entity set for s01 (the navigator list + status/kind filters).

    Returns [{kcdx_id, name, status, kind}] -- exactly what read_curated_set
    returns; `status` and `kind` arrive already derived/decoded from the data-core
    (the backend derives nothing). The frontend filters/searches client-side over
    the full set (s01 §Contents "local, no write")."""
    config = load_config()
    try:
        return _json_safe(data_core.read_curated_set(config.out_dir))
    except data_core.DbReadError as exc:
        return _no_db_signal(exc, config)


@router.get("/entities/{kcdx_id}")
def get_entity(kcdx_id: int):
    """An entity's identity + lifecycle fields for s02 (the detail header).

    Returns read_entity_detail's dict verbatim. read_entity_detail returns None for
    an unknown id (the 2a no-matching-row contract) -> HTTP 404 here (a genuine
    not-found is a real HTTP error, distinct from the no-DB empty-state which is a
    200 {state} body). kcdx_id is the curated id (a path int)."""
    config = load_config()
    try:
        detail = data_core.read_entity_detail(config.out_dir, kcdx_id)
    except data_core.DbReadError as exc:
        return _no_db_signal(exc, config)
    if detail is None:
        # WHY 404 not the empty-signal: the DB resolved, the entity is simply not in
        # it -- a true not-found, not a missing-checkout state. (HTTPException is
        # imported lazily so the no-DB path above never constructs one.)
        from fastapi import HTTPException
        raise HTTPException(status_code=404,
                            detail=f"no entity with kcdx_id {kcdx_id}")
    return _json_safe(detail)


@router.get("/entities/{kcdx_id}/versions")
def get_entity_versions(kcdx_id: int):
    """The entity's version rows, NEWEST-first, each with its derived status (s02
    version table, s03 history/compare).

    Returns read_version_rows' list verbatim -- the curated display columns (the
    data-core's _VERSION_DISPLAY_COLUMNS allowlist; the engine-computed content_hash
    and the dev-only columns never cross the wire), the per-row `status` already
    derived and `kind`/`evidence_kind` already decoded by the data-core. An unknown
    id yields [] (the 2a no-rows contract), distinct from the entity-detail 404; the
    frontend treats [] as "no versions" for that id."""
    config = load_config()
    try:
        return _json_safe(data_core.read_version_rows(config.out_dir, kcdx_id))
    except data_core.DbReadError as exc:
        return _no_db_signal(exc, config)
