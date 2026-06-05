"""test_maintainer_identity.py -- the env/config-injected maintainer identity (FIX 3).

WHAT THIS PROVES
----------------
A single configured maintainer identity (name + email) is injected via env/config and is used
for BOTH the GUI's verified_by default (the audit-trio signer) AND the git commit author --
mirroring the existing KCDX_CHECKOUT / KCDX_CORS_ORIGINS env pattern:

  1. config.maintainer_identity() resolves the env vars (KCDX_MAINTAINER_NAME/EMAIL) when set,
     and the documented dev defaults when unset (the boot-without-config posture), each half
     independently.
  2. routes_confirm._resolve_author falls back to the CONFIGURED identity when no body field /
     X-Kcdx-Author-* header is present (the request-context auth-ready seam, D17) -- so the git
     author and the audit-trio signer are one identity.
  3. /health EXPOSES the identity (maintainer_identity: {name, email}) so the frontend can use
     `name` as the verified_by default -- WITHOUT the push token. The email is a NON-SECRET
     public author identity (like a git author); the push CREDENTIAL (KCDX_PUSH_TOKEN) is a
     SECRET and is NEVER surfaced in the health body or any response (security-invariants.md).

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import json
import os
import sys

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package (the skeleton test's pattern) --------------------------------
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
sys.path.insert(0, BACKEND_DIR)

from app import config as cfg                                # noqa: E402
from app.config import (                                     # noqa: E402
    MAINTAINER_NAME_ENV_VAR,
    MAINTAINER_EMAIL_ENV_VAR,
    DEV_DEFAULT_MAINTAINER_NAME,
    DEV_DEFAULT_MAINTAINER_EMAIL,
    maintainer_identity,
)
from app import routes_confirm                               # noqa: E402
from app.git_commit import PUSH_TOKEN_ENV_VAR                # noqa: E402
from app.main import app                                     # noqa: E402


@pytest.fixture
def clean_identity_env(monkeypatch):
    """Unset the identity env vars so a case controls them deterministically (read-fresh-per-call
    means the env is the only input)."""
    monkeypatch.delenv(MAINTAINER_NAME_ENV_VAR, raising=False)
    monkeypatch.delenv(MAINTAINER_EMAIL_ENV_VAR, raising=False)
    yield monkeypatch


# ----------------------------------------------------------------------------
# config.maintainer_identity(): the env wins; unset -> the documented dev default.
# ----------------------------------------------------------------------------
def test_maintainer_identity_dev_default_without_env(clean_identity_env):
    ident = maintainer_identity()
    assert ident == {"name": DEV_DEFAULT_MAINTAINER_NAME,
                     "email": DEV_DEFAULT_MAINTAINER_EMAIL}, ident
    # The dev default is a real, non-secret author identity so the app boots without env.
    assert DEV_DEFAULT_MAINTAINER_NAME == "VioletAnvil"


def test_maintainer_identity_env_overrides(clean_identity_env):
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "RealMaintainer")
    clean_identity_env.setenv(MAINTAINER_EMAIL_ENV_VAR, "real@example.com")
    assert maintainer_identity() == {"name": "RealMaintainer", "email": "real@example.com"}


def test_maintainer_identity_halves_resolve_independently(clean_identity_env):
    # Only the name is wired -> the name is the env's, the email falls back to the dev default.
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "OnlyName")
    ident = maintainer_identity()
    assert ident["name"] == "OnlyName"
    assert ident["email"] == DEV_DEFAULT_MAINTAINER_EMAIL


def test_maintainer_identity_blank_env_uses_dev_default(clean_identity_env):
    # An empty/whitespace env value is treated as unset (the same posture as cors_origins).
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "   ")
    assert maintainer_identity()["name"] == DEV_DEFAULT_MAINTAINER_NAME


# ----------------------------------------------------------------------------
# routes_confirm._resolve_author: no body/header -> the CONFIGURED identity is the
# default git author (the audit-trio signer and the git author are one identity).
# ----------------------------------------------------------------------------
def test_resolve_author_falls_back_to_configured_identity(clean_identity_env):
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "ConfiguredMaintainer")
    clean_identity_env.setenv(MAINTAINER_EMAIL_ENV_VAR, "configured@example.com")
    # No body field, no header -> the configured identity is the default author.
    name, email = routes_confirm._resolve_author(None, None, None, None)
    assert name == "ConfiguredMaintainer"
    assert email == "configured@example.com"


def test_resolve_author_explicit_context_still_wins(clean_identity_env):
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "ConfiguredMaintainer")
    # An explicit body field > a header > the configured identity (priority order unchanged).
    name, email = routes_confirm._resolve_author("BodyName", "body@x", "HeaderName", "header@x")
    assert (name, email) == ("BodyName", "body@x")
    # A header (no body) > the configured identity.
    name2, _ = routes_confirm._resolve_author(None, None, "HeaderName", None)
    assert name2 == "HeaderName"


# ----------------------------------------------------------------------------
# /health EXPOSES the identity WITHOUT the push token (security-invariants.md).
# ----------------------------------------------------------------------------
def test_health_exposes_identity_without_push_token(clean_identity_env):
    clean_identity_env.setenv(MAINTAINER_NAME_ENV_VAR, "ExposedMaintainer")
    clean_identity_env.setenv(MAINTAINER_EMAIL_ENV_VAR, "exposed@example.com")
    # A push token IS in the env -> prove it never reaches the exposed health body.
    secret = "ghp_THISisASECRETtokenVALUE0123456789"
    clean_identity_env.setenv(PUSH_TOKEN_ENV_VAR, secret)

    client = TestClient(app)
    resp = client.get("/health")
    assert resp.status_code == 200, resp.text
    body = resp.json()

    # The identity is exposed (name + email) for the verified_by default.
    assert body["maintainer_identity"] == {"name": "ExposedMaintainer",
                                           "email": "exposed@example.com"}, body["maintainer_identity"]

    # The push token (a SECRET) is NOWHERE in the exposed body -- not under any key, not the
    # value, not the env-var name. Serialize the WHOLE body and scan it (a leak anywhere fails).
    raw = json.dumps(body)
    assert secret not in raw, "the push TOKEN leaked into the /health body"
    assert PUSH_TOKEN_ENV_VAR not in raw, "the push-token ENV VAR name leaked into /health"
    assert "token" not in raw.lower(), f"a 'token' key/value leaked into /health: {raw}"
