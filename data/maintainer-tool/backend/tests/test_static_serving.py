"""test_static_serving.py -- the backend serves the built React SPA alongside the API (D14).

WHAT THIS PROVES
----------------
D14 settled the SINGLE IMAGE: one uvicorn process serves the JSON API AND the built
frontend `dist/` static files, same-origin. This test is the headless gate for that:
it builds the real app with KCDX_STATIC_DIR pointed at a temp dir laid out like a Vite
`dist/` (an index.html + an assets/app.js) and drives FastAPI's TestClient against it.
Every assertion is FALSIFIABLE -- each case names what makes it FAIL. Cases:

  1. SPA ROOT: GET / returns 200 and the index.html marker. FAILS if the static root
     isn't served (no StaticFiles/catch-all wiring).

  2. REAL ASSET: GET /assets/app.js returns 200 and the JS content. FAILS if real
     static files under the dir aren't served (the catch-all only does index.html).

  3. DEEP-LINK FALLBACK (the load-bearing case): GET a CLIENT route -- a path no API
     router claims and no real file matches -- returns 200 and the index.html marker,
     NOT a 404. FAILS if the SPA deep-link fallback is missing, which breaks a hard
     refresh / typed-deep-link of any client-side route. NOTE: the path is a genuinely
     client-only route (NOT /entities/123 -- that IS a real API route, GET
     /entities/{kcdx_id} in routes_read; using it would test the API, not the fallback).

  4. API PRIORITY: GET /health still returns 200 with the health shape (a `state` key),
     NOT the index.html marker. FAILS if the static catch-all shadowed the API (mounted
     before the routers / matched too greedily). This is the API-not-shadowed invariant.

  5. MISSING dist/ GRACEFUL: with KCDX_STATIC_DIR pointed at a non-existent path, the app
     STILL boots and GET /health returns 200. FAILS if a missing `dist/` crashes the app
     at import/startup -- the 79 pre-existing backend tests run with no dist/ present and
     depend on this. (A SPA/asset request with no bundle degrades to a logged 503, the
     static_serving degraded contract -- the API is unaffected.)

  6. KCDX_STATIC_DIR config (the config-test style, mirroring test_cors / test_config):
     env set -> that abspath; env unset -> the documented dev default (<app>/static).
     static_dir() reads the env fresh each call, so the override is exercised at the
     resolver.

WHY A FRESH APP PER STATIC-DIR CASE
-----------------------------------
The catch-all reads static_dir() FRESH per request (so it tracks the env), but the
imported `app.main.app` registered its routers + catch-all once at import. Re-importing
the app module under a monkeypatched KCDX_STATIC_DIR gives a clean app whose catch-all
sees the temp dir -- the same fresh-read seam cors_origins / load_config use, applied to
the whole app construction so the static mount is unambiguous.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/test_static_serving.py -v
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import importlib
import os
import sys

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package (same path setup as test_backend_skeleton) ----
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
sys.path.insert(0, BACKEND_DIR)

from app import config                                # noqa: E402

# A recognizable marker baked into the fixture index.html -- a string the API never
# emits, so finding it in a response body PROVES the SPA index was served (not an API
# JSON body that merely 200s).
INDEX_MARKER = "kcdx-spa-root-marker-7f3a"
ASSET_JS = "console.log('kcdx spa asset');"

# A genuinely client-only deep-link path: no API router claims it (the API surface is
# /health, /entities[...], /modules, /needs-action, /field-delta, /save/*, /confirm/*,
# /cancel) and no real file under the fixture dir matches it. So it must fall through to
# the SPA index -- the deep-link fallback. Deliberately NOT /entities/123 (a real API
# route -- GET /entities/{kcdx_id}); that would exercise the API, not the fallback.
CLIENT_ROUTE = "/some/client-only/view"


def _make_dist(tmp_path):
    """Lay out a temp dir like a Vite `dist/`: index.html (carrying the marker) +
    assets/app.js. Returns the dir path."""
    (tmp_path / "index.html").write_text(
        f"<!doctype html><html><body><div id=root>{INDEX_MARKER}</div></body></html>",
        encoding="utf-8")
    assets = tmp_path / "assets"
    assets.mkdir()
    (assets / "app.js").write_text(ASSET_JS, encoding="utf-8")
    return str(tmp_path)


def _fresh_app():
    """Re-import app.main so the app is constructed (routers + static catch-all) under
    the CURRENT KCDX_STATIC_DIR env. Returns the FastAPI app object. The catch-all reads
    static_dir() fresh per request, but a fresh import makes the whole construction
    unambiguous for the case under test."""
    import app.main as main_mod
    main_mod = importlib.reload(main_mod)
    return main_mod.app


@pytest.fixture
def spa_client(tmp_path, monkeypatch):
    """A TestClient over a fresh app whose KCDX_STATIC_DIR points at a temp `dist/`."""
    dist = _make_dist(tmp_path)
    monkeypatch.setenv(config.STATIC_DIR_ENV_VAR, dist)
    return TestClient(_fresh_app())


# ----------------------------------------------------------------------------
# Case 1: GET / serves the SPA index (the index.html marker is in the body).
# FAILS if the static root isn't served.
# ----------------------------------------------------------------------------
def test_spa_root_serves_index(spa_client):
    resp = spa_client.get("/")
    assert resp.status_code == 200, resp.text
    assert INDEX_MARKER in resp.text, \
        f"GET / must serve the SPA index.html (marker {INDEX_MARKER!r}); got: {resp.text!r}"


# ----------------------------------------------------------------------------
# Case 2: GET /assets/app.js serves the real static asset (the JS content).
# FAILS if real files under the static dir aren't served.
# ----------------------------------------------------------------------------
def test_real_static_asset_is_served(spa_client):
    resp = spa_client.get("/assets/app.js")
    assert resp.status_code == 200, resp.text
    assert ASSET_JS in resp.text, \
        f"GET /assets/app.js must serve the real asset content; got: {resp.text!r}"
    # It is the asset, NOT the index fallback (the asset content differs from the marker).
    assert INDEX_MARKER not in resp.text, \
        "a real asset must serve its own content, not fall back to index.html"


# ----------------------------------------------------------------------------
# Case 3 (LOAD-BEARING): a client-route deep link -> index.html with 200, NOT 404.
# FAILS if the SPA deep-link fallback is missing (a hard refresh of a client route 404s).
# ----------------------------------------------------------------------------
def test_client_deep_link_falls_back_to_index(spa_client):
    resp = spa_client.get(CLIENT_ROUTE)
    assert resp.status_code == 200, \
        f"a client deep link ({CLIENT_ROUTE}) must serve the SPA (200), not 404; " \
        f"status={resp.status_code} body={resp.text!r}"
    assert INDEX_MARKER in resp.text, \
        f"a client deep link must serve index.html (marker {INDEX_MARKER!r}) so the " \
        f"client router takes over; got: {resp.text!r}"


# ----------------------------------------------------------------------------
# Case 4: GET /health still resolves to the API, NOT the static catch-all.
# FAILS if the static mount shadowed the API routers.
# ----------------------------------------------------------------------------
def test_api_route_not_shadowed_by_static(spa_client):
    resp = spa_client.get("/health")
    assert resp.status_code == 200, resp.text
    body = resp.json()                                   # the API returns JSON, not HTML
    assert "state" in body, \
        f"/health must resolve to the API (a JSON body with a `state` key), not the SPA; " \
        f"got: {body!r}"
    # The API body is NOT the SPA index -- the catch-all did not shadow the route.
    assert INDEX_MARKER not in resp.text, \
        "/health must serve the API JSON, not the static index.html (the catch-all " \
        "must be registered AFTER the API routers)"


# ----------------------------------------------------------------------------
# Case 5: a MISSING dist/ -- the app still boots and the API still works.
# FAILS if a missing static dir crashes the app (the 79 existing tests depend on this).
# ----------------------------------------------------------------------------
def test_missing_dist_does_not_crash_app(tmp_path, monkeypatch):
    missing = str(tmp_path / "does-not-exist-dist")
    assert not os.path.exists(missing)
    monkeypatch.setenv(config.STATIC_DIR_ENV_VAR, missing)
    # The app constructs without raising even though the static dir is absent.
    client = TestClient(_fresh_app())
    resp = client.get("/health")
    assert resp.status_code == 200, \
        f"the API must work with no dist/ present; /health status={resp.status_code} " \
        f"body={resp.text!r}"
    assert "state" in resp.json(), resp.text


def test_missing_dist_spa_request_degrades_not_crash(tmp_path, monkeypatch):
    """With no bundle, a SPA/asset request degrades to the logged 503 contract (not a
    crash, not a stack trace). The API is unaffected (Case 5 above). FAILS if a SPA
    request with no bundle raises instead of returning a clean status."""
    missing = str(tmp_path / "no-bundle")
    monkeypatch.setenv(config.STATIC_DIR_ENV_VAR, missing)
    client = TestClient(_fresh_app())
    resp = client.get("/some/client/route")
    # The degraded contract: a deliberate status with a JSON body naming the cause --
    # NOT a 500/uncaught exception. 503 distinguishes "backend up, bundle absent".
    assert resp.status_code == 503, \
        f"a SPA request with no bundle must degrade to a clean 503, got {resp.status_code}"
    assert resp.json().get("state") == "frontend_unavailable", resp.text


# ----------------------------------------------------------------------------
# Case 6: KCDX_STATIC_DIR config -- env set -> that abspath; unset -> the dev default.
# (The config-test style, mirroring test_cors's cors_origins cases.)
# static_dir() reads the env fresh each call.
# ----------------------------------------------------------------------------
def test_static_dir_env_override(monkeypatch, tmp_path):
    target = str(tmp_path / "my-dist")
    monkeypatch.setenv(config.STATIC_DIR_ENV_VAR, target)
    # The env value, made absolute (static_dir abspaths it -- mirrors load_config).
    assert config.static_dir() == os.path.abspath(target), config.static_dir()


def test_static_dir_dev_default_when_unset(monkeypatch):
    monkeypatch.delenv(config.STATIC_DIR_ENV_VAR, raising=False)
    # The documented dev default: <app>/static, where the image's stage-2 COPY lands
    # the built `dist/`. Asserted against the same derivation config exposes, so the
    # default value (not just "some path") is pinned.
    expected = config._default_static_dir()
    assert config.static_dir() == expected, config.static_dir()
    # Concretely it is the `static` dir alongside the app package.
    assert os.path.basename(expected) == "static", expected


def test_static_dir_empty_env_falls_back_to_default(monkeypatch):
    # An empty / whitespace-only var is treated as unset -> the dev default (mirrors
    # cors_origins' empty-env handling).
    monkeypatch.setenv(config.STATIC_DIR_ENV_VAR, "   ")
    assert config.static_dir() == config._default_static_dir(), config.static_dir()
