"""test_cors.py -- the backend CORS allowlist (the served-frontend cross-origin fix).

WHAT THIS PROVES
----------------
The browser-served frontend (a separate origin -- vite preview :4173 / dev :5173) can
call the backend cross-origin: the FastAPI app emits an `access-control-allow-origin`
header for an ALLOWED origin, so the browser no longer blocks the call. Without the
CORSMiddleware the header is absent and every cross-origin fetch from the frontend is
blocked by the browser's same-origin policy. The TestClient header assertion is the
headless gate for the live-browser proof.

The allowlist is env-configurable (KCDX_CORS_ORIGINS -- the operator-wired seam),
localhost dev default, and NEVER a wildcard origin (the maintainer tool writes + commits
the Address Library; a tight allowlist is the security-correct choice). Cases:

  1. ALLOWED origin -> the response carries `access-control-allow-origin` echoing the
     origin (the cross-origin call the browser permits). The live header over the REAL
     app (no mock) -- the headless counterpart to the browser actually loading the page.

  2. The OPTIONS PREFLIGHT for a mutating POST is permitted (the middleware answers it
     with the allow-origin + the GET/POST allow-methods) -- the save/confirm POSTs
     are preflighted by the browser before the real request.

  3. A DISALLOWED origin gets NO `access-control-allow-origin` header (the allowlist is
     tight, not a wildcard) -- the browser then blocks that origin, the security-correct
     default.

  4. The env-var OVERRIDE: KCDX_CORS_ORIGINS resolves to the operator's wired origins
     (comma-separated), and an unset/empty var falls back to the localhost dev default.
     Asserted at the config resolver (`cors_origins()` reads the env fresh each call) so
     the override is exercised without re-importing the app.

RUN
---
    python -m pytest backend/tests/ -q
"""
import os
import sys

from fastapi.testclient import TestClient

# --- locate the backend package (same path setup as test_backend_skeleton) ----
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
sys.path.insert(0, BACKEND_DIR)

from app import config                                # noqa: E402
from app.main import app                              # noqa: E402

ALLOWED_ORIGIN = "http://localhost:4173"              # the vite preview port (dev default)
DISALLOWED_ORIGIN = "http://evil.example.com"


# ----------------------------------------------------------------------------
# Case 1: an ALLOWED origin gets the access-control-allow-origin header.
# ----------------------------------------------------------------------------
def test_allowed_origin_gets_cors_header():
    client = TestClient(app)
    resp = client.get("/health", headers={"Origin": ALLOWED_ORIGIN})
    assert resp.status_code == 200, resp.text
    # The CORS header is present and echoes the allowed origin -- the browser permits
    # the cross-origin read. Header name is case-insensitive (httpx/requests CaseInsensitiveDict).
    assert "access-control-allow-origin" in resp.headers, dict(resp.headers)
    assert resp.headers["access-control-allow-origin"] == ALLOWED_ORIGIN


# ----------------------------------------------------------------------------
# Case 2: the OPTIONS preflight for a mutating POST is permitted (allow-origin +
# the GET/POST allow-methods) -- the save/confirm POSTs are browser-preflighted.
# ----------------------------------------------------------------------------
def test_preflight_for_post_is_permitted():
    client = TestClient(app)
    resp = client.options(
        "/field-delta",
        headers={
            "Origin": ALLOWED_ORIGIN,
            "Access-Control-Request-Method": "POST",
        },
    )
    # The middleware answers the preflight (200) with the allow-origin echoed and POST
    # among the allowed methods.
    assert resp.status_code == 200, resp.text
    assert resp.headers.get("access-control-allow-origin") == ALLOWED_ORIGIN
    assert "POST" in resp.headers.get("access-control-allow-methods", "")


# ----------------------------------------------------------------------------
# Case 3: a DISALLOWED origin gets NO access-control-allow-origin header (the
# allowlist is tight, not a wildcard) -- the browser blocks that origin.
# ----------------------------------------------------------------------------
def test_disallowed_origin_gets_no_cors_header():
    client = TestClient(app)
    resp = client.get("/health", headers={"Origin": DISALLOWED_ORIGIN})
    # The route still returns 200 (CORS is a browser-enforced policy, not a server reject),
    # but the allow-origin header is ABSENT, so a real browser blocks the cross-origin read.
    # Crucially it is never the wildcard "*" (the allowlist is tight).
    allow_origin = resp.headers.get("access-control-allow-origin")
    assert allow_origin != DISALLOWED_ORIGIN, dict(resp.headers)
    assert allow_origin != "*", "CORS allowlist must never be a wildcard origin"


# ----------------------------------------------------------------------------
# Case 4: the env-var override (KCDX_CORS_ORIGINS) + the localhost dev default.
# cors_origins() reads the env fresh each call, so the override is exercised
# at the resolver without re-importing the app.
# ----------------------------------------------------------------------------
def test_cors_origins_dev_default_when_unset(monkeypatch):
    monkeypatch.delenv(config.CORS_ORIGINS_ENV_VAR, raising=False)
    origins = config.cors_origins()
    # The localhost dev default: both vite ports, both localhost + 127.0.0.1 spellings.
    assert origins == [
        "http://localhost:4173",
        "http://localhost:5173",
        "http://127.0.0.1:4173",
        "http://127.0.0.1:5173",
    ], origins


def test_cors_origins_env_override(monkeypatch):
    monkeypatch.setenv(config.CORS_ORIGINS_ENV_VAR,
                       "https://maint.example.com, https://kcdx.example.com")
    origins = config.cors_origins()
    # The operator's wired origins, comma-split + whitespace-stripped, replace the default.
    assert origins == ["https://maint.example.com", "https://kcdx.example.com"], origins


def test_cors_origins_empty_env_falls_back_to_default(monkeypatch):
    # An empty / whitespace-only var is treated as unset -> the dev default (never an
    # empty allowlist, which would block every origin including the dev frontend).
    monkeypatch.setenv(config.CORS_ORIGINS_ENV_VAR, "   ")
    origins = config.cors_origins()
    assert origins == [
        "http://localhost:4173",
        "http://localhost:5173",
        "http://127.0.0.1:4173",
        "http://127.0.0.1:5173",
    ], origins
