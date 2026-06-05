"""app.main -- the FastAPI app entry + the health/load endpoint (design D14 / US-1 / S7).

The backend app skeleton. Step 1 (this step) ships ONE endpoint -- the health/load
endpoint that reports whether the configured checkout (D18) resolves a reference DB
+ seeds, and the EMPTY/ERROR states when it does not (US-1 acceptance: "if no
DB/seeds resolve at the configured checkout path, the empty state explains why";
S7 required state "Empty -- no DB/seeds resolved (names where the backend looked)").

No read/save/commit endpoints yet (steps 2-5). The app holds NO data-core rule
logic (D13/R3): it imports the data-core through the app.data_core seam and reads
the checkout through app.config; every rule is the data-core's.
"""
import logging
import os

from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware

from . import adapter
from .config import cors_origins, load_config, maintainer_identity
from .routes_confirm import router as confirm_router
from .routes_delta import router as delta_router
from .routes_read import router as read_router
from .routes_save import router as save_router

# One module logger, event-driven (logging.md): the failure branches below log on
# the data-core read failure, never per request.
log = logging.getLogger(__name__)

app = FastAPI(
    title="kcdx maintainer tool -- backend",
    description="The Python API over the headless Address Library data-core "
                "(design D14). Step 1: skeleton + config + import seam + "
                "version-tag adapter + health/load. Step 2b: read endpoints "
                "(curated set / entity detail / version rows).",
    version="0.1.0",
)

# CORS: the served frontend (a separate origin -- vite preview :4173 / dev :5173, or the
# operator's production origin) calls this backend cross-origin, so the browser needs an
# Access-Control-Allow-Origin header or it blocks the call. The allowed origins are an
# env-configurable ALLOWLIST (KCDX_CORS_ORIGINS, localhost dev default) -- NEVER a wildcard:
# the maintainer tool writes + commits the Address Library, so a tight allowlist is the
# security-correct choice (D17 operator-wired seam; security-invariants.md). Methods are
# GET + POST only (the API's whole surface -- read endpoints are GET, save/confirm/cancel are
# POST; OPTIONS preflight is handled by the middleware automatically). allow_credentials is
# False: the frontend sends no cookies/credentials (auth is the operator-wired seam, D17).
app.add_middleware(
    CORSMiddleware,
    allow_origins=cors_origins(),
    allow_methods=["GET", "POST"],
    allow_headers=["*"],
    allow_credentials=False,
)

# The read-for-display endpoints (GET /entities, /entities/{id}, /.../versions) --
# their own module (structure-by-responsibility); main.py stays the app + health
# coordinator.
app.include_router(read_router)

# The field-delta endpoint (POST /field-delta) the s06 confirm surface calls -- a
# distinct concern from the read routes, so its own router (structure-by-responsibility).
app.include_router(delta_router)

# The save (PREVIEW) endpoints (POST /save/*) -- the six job shapes, each VALIDATING
# the prospective edit + returning the field-delta with NO DB write (step 4b,
# Save-previews/Confirm-transacts model). Their own router (structure-by-
# responsibility); the step-5 confirm endpoints run the actual transaction.
app.include_router(save_router)

# The Confirm transaction + Cancel (POST /confirm/*, /cancel) -- the synchronous atomic
# save (step 5): start txn -> DB ops (deferred) -> commit DB -> export CSVs -> integrity
# -> git commit/push to private, rollback-everything on failure. Its own router
# (structure-by-responsibility); the git plumbing + the cheap integrity check are the
# backend's own concern (app.git_commit / app.csv_integrity), the write+validate are the
# data-core's (D13/R3).
app.include_router(confirm_router)


def _version_tags_newest_first(versions):
    """Order a {tag: ordinal} map's TAGS newest-first by the data-core's version
    ordinal -- NOT a lexicographic tag sort ("1.10" < "1.9" as strings but is the
    newer build). The frontend + spec treat known_version_tags[0] as the current
    (newest) version (the verified-at default + the version-dropdown order), so the
    list is surfaced ordinal-descending. Ties on ordinal fall back to the tag string
    (descending) for a deterministic order."""
    return [
        tag for tag, _ord in sorted(
            versions.items(), key=lambda kv: (kv[1], kv[0]), reverse=True)
    ]


def _checkout_status(config):
    """Resolve what the configured checkout actually holds -- the load endpoint's
    core. Reports each required artifact's presence so the empty/error state can
    NAME where the backend looked (S7). Holds no rule logic -- it stats paths the
    config derives + reads the data-core's known-version set."""
    seed_files = {os.path.basename(p): os.path.isfile(p)
                  for p in config.seed_files}
    seeds_present = all(seed_files.values())
    user_db_present = os.path.isfile(config.user_db)
    dev_db_present = os.path.isfile(config.dev_db)

    # The known game versions the server holds (the dropdown source, D10/US-10).
    # Reading this also confirms the data-core import seam resolved end-to-end.
    versions_error = None
    try:
        versions = adapter.known_versions(config)
    except Exception as exc:  # the seam/DB read failed -- report it, don't crash
        versions = {}
        versions_error = f"{type(exc).__name__}: {exc}"
        # logging.md: the failure branch logs before it returns -- name the
        # data-core seam read failure + the checkout path it looked at, so a
        # swallowed-into-versions_error read failure is never silent.
        log.warning(
            "data-core known-versions read failed (checkout_path=%s, "
            "user_db=%s): %s", config.checkout_path, config.user_db, exc)

    # "resolved" == the checkout yields what the tool needs to load: the curated
    # USER DB + all three seed CSVs. Missing either -> empty/error (US-1, S7).
    resolved = user_db_present and seeds_present and versions_error is None

    if resolved:
        state = "resolved"
        detail = "the configured checkout resolves the reference DB and seeds"
    elif versions_error is not None:
        state = "error"
        detail = f"the data-core read failed: {versions_error}"
    else:
        state = "empty"
        missing = [name for name, ok in seed_files.items() if not ok]
        if not user_db_present:
            missing.append(os.path.basename(config.user_db))
        detail = ("no reference DB / seeds at the configured checkout path; "
                  f"missing: {', '.join(missing) or 'unknown'}")

    return {
        "state": state,                  # "resolved" | "empty" | "error"
        "detail": detail,                # human-readable cause (S7 empty/error copy)
        "checkout_path": config.checkout_path,
        "checkout_source": config.checkout_source,   # env | override | dev-default
        "seed_dir": config.seed_dir,
        "out_dir": config.out_dir,
        "artifacts": {
            "user_db": user_db_present,
            "dev_db": dev_db_present,
            "seed_files": seed_files,
        },
        # NEWEST-FIRST by the data-core's version ordinal (see _version_tags_newest_first).
        "known_version_tags": _version_tags_newest_first(versions),
        # The configured maintainer identity (FIX 3) -- a NON-SECRET public author identity
        # (name + email, the shape of a git author line) the frontend uses as the
        # verified_by default for the audit trio. Surfaced here because the frontend already
        # consumes /health (the lighter touch over a new endpoint). The push CREDENTIAL
        # (KCDX_PUSH_TOKEN) is a SECRET and is NEVER read or surfaced here
        # (security-invariants.md -- secrets never enter an exposed surface).
        "maintainer_identity": maintainer_identity(),
    }


@app.get("/health")
def health():
    """Health/load endpoint (US-1 / S7). Reports whether the configured checkout
    (D18) resolves a reference DB + seeds, naming where the backend looked when it
    does not (the empty/error states s01 feeds the frontend, S7). Reads fresh
    config each call so an operator can mount the volume after boot."""
    config = load_config()
    return _checkout_status(config)
