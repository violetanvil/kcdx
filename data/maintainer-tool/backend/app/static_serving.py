"""app.static_serving -- serve the built React SPA's static files alongside the API (D14).

D14 settled the SINGLE IMAGE: one uvicorn process serves the API AND the built frontend
`dist/` static files, same-origin (D18's "CORS may not apply" note). This module owns the
serving side ONLY -- the static-dir path is config (KCDX_STATIC_DIR, app.config.static_dir);
the image's stage-2 COPY places the built `dist/` there (step 16b). It holds NO data-core
rule logic (D13/R3) -- static serving is pure backend plumbing.

WHY a catch-all GET route registered LAST, not StaticFiles mounted at "/"
------------------------------------------------------------------------
The API routers (/health, /entities, /save/*, /confirm, ...) MUST keep priority -- static
serving must never shadow them. FastAPI matches routes in REGISTRATION order, so registering
this catch-all AFTER every API router is what guarantees the API wins: an API path resolves
at its own route; only a path no API route claimed reaches the catch-all. A `StaticFiles`
sub-app mounted at "/" would intercept the path-resolution at the mount and cannot express
the SPA deep-link fallback (an unknown route -> index.html, not a 404) -- so a plain catch-all
GET that (a) serves a real asset if the file exists under the static dir, (b) else returns
index.html for client-side routing, is the FastAPI-idiomatic mechanism that satisfies both.

THE DEGRADED CONTRACT (a missing static dir must NOT crash the app)
------------------------------------------------------------------
The dir is absent on a bare dev checkout (no FE build) and could be absent in a
misconfigured container. The app still boots and the API still serves regardless (the
catch-all is the only thing that touches the dir, and it touches it per-request, never at
import). When no `index.html` resolves, a SPA/asset request returns HTTP 503 with a clear
JSON body naming the cause ("frontend not built or not mounted") and the failure is LOGGED
(logging.md -- the failure branch logs before it returns). 503 (not 404) distinguishes
"backend up, frontend bundle absent" from a genuine missing resource, mirroring /health's
report-the-state-don't-crash posture.
"""
import logging
import os

from fastapi import FastAPI
from fastapi.responses import FileResponse, JSONResponse

from .config import static_dir

# One module logger, event-driven (logging.md): the degraded branch logs when no index.html
# resolves, never per served asset.
log = logging.getLogger(__name__)

_INDEX_HTML = "index.html"


def _safe_join(root, rel_path):
    """Resolve rel_path under root, refusing any path that escapes root (a `..` traversal
    or an absolute path -- input-validation.md: a path from the request is untrusted). The
    request path is browser/operator-controlled, so a served asset is confined to the static
    dir. Returns the absolute path if it is a real file under root, else None."""
    # normpath collapses `..`; the abspath + commonpath check is the traversal fence.
    candidate = os.path.normpath(os.path.join(root, rel_path.lstrip("/")))
    root_abs = os.path.abspath(root)
    candidate_abs = os.path.abspath(candidate)
    # The candidate must sit INSIDE root_abs (commonpath == root_abs) -- a traversal that
    # escaped resolves to a different commonpath and is rejected.
    try:
        inside = os.path.commonpath([root_abs, candidate_abs]) == root_abs
    except ValueError:
        # commonpath raises on mixed drives / absolute-vs-relative -- treat as outside.
        return None
    if inside and os.path.isfile(candidate_abs):
        return candidate_abs
    return None


def _frontend_unavailable(requested):
    """The degraded-contract response: no index.html resolved at the configured static dir,
    so the SPA cannot be served. Log the cause (the path the backend looked at) THEN return
    a 503 the operator can act on -- the API stays up; only the SPA surface is unavailable."""
    looked_at = static_dir()
    log.warning(
        "frontend not built or not mounted: no %s at static_dir=%s (requested=%r) -- "
        "API still serving; build the FE bundle or mount it (KCDX_STATIC_DIR)",
        _INDEX_HTML, looked_at, requested)
    return JSONResponse(
        status_code=503,
        content={
            "state": "frontend_unavailable",
            "detail": (
                f"the frontend is not built or not mounted (no {_INDEX_HTML} at the "
                f"configured static dir); the API is serving normally"),
            "static_dir": looked_at,
        },
    )


def register_static_serving(app: FastAPI):
    """Register the SPA catch-all on `app` -- call AFTER every API router is included so the
    API keeps route-resolution priority (the load-bearing ordering). The catch-all claims
    only GET paths no API route matched: it serves a real asset under the static dir when the
    file exists, else falls back to index.html (SPA client-side routing on a hard refresh),
    else degrades to a logged 503 when no bundle is present."""

    @app.get("/{spa_path:path}", include_in_schema=False)
    def serve_spa(spa_path: str):
        """Serve the built SPA. Read static_dir fresh per request (an operator can mount the
        volume after boot, mirroring /health's load_config-per-call). A real asset under the
        dir is served directly; any other (unknown) path returns index.html so client-side
        routing works; a missing bundle degrades to the logged 503 contract."""
        root = static_dir()

        # (1) A real static asset (the SPA's JS/CSS/favicon/...) -> serve it directly.
        asset = _safe_join(root, spa_path)
        if asset is not None:
            return FileResponse(asset)

        # (2) Any other non-API path (an unknown client route, or "/") -> index.html, so a
        #     hard refresh of a deep link lands on the SPA, not a 404 (client-side routing).
        index = os.path.join(root, _INDEX_HTML)
        if os.path.isfile(index):
            return FileResponse(index)

        # (3) No bundle present -> the degraded contract (logged 503, API unaffected).
        return _frontend_unavailable(spa_path)
