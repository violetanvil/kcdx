"""test_read_endpoints.py -- the backend read endpoints (Phase 2 step 2b).

WHAT THIS PROVES
----------------
The read endpoints SURFACE the data-core's read-for-display values end-to-end over
the REAL app + the REAL data-core (no mock of either -- the duplication/R3 self-check:
TestClient drives FastAPI against the real /entities routes, which call the real
seeds_shared.read_api in-process). The backend is a THIN CALLER: each case asserts
the endpoint passes through what 2a returns (status/kind present, count matches,
newest-first ordering, the 404 / no-DB signal) -- NOT the status DERIVATION, which is
2a's own oracle (test_read_api_*.py). Cases:

  1. GET /entities over a resolved mini-dump checkout -> the curated set: each row has
     name/kcdx_id/status/kind; the count matches read_curated_set's; a known row's
     status is one of the four valid tokens (surfaced, not re-derived).

  2. GET /entities/{id} for a known id -> identity + the six lifecycle fields.

  3. GET /entities/{unknown_id} -> HTTP 404 (the 2a None contract; a genuine
     not-found is a real HTTP error, distinct from the no-DB empty-state).

  4. GET /entities/{id}/versions -> the rows NEWEST-first, each carrying a status.

  5. A no-DB checkout (an empty dir that resolves no curated DB) -> every read
     endpoint returns the empty/error SIGNAL (HTTP 200, state="empty" -- the s01
     empty-state the frontend binds, mirroring /health), NOT a 500 crash, and the
     failure is LOGGED (asserted via caplog).

CHECKOUT WIRING
---------------
The endpoints call load_config() with no override -> they resolve the checkout via
KCDX_CHECKOUT (config priority 1). So each request points the endpoints at the
fixture checkout by setting KCDX_CHECKOUT for the duration (an env-var monkeypatch),
exactly how an operator wires the mounted volume (D18). Reuses the skeleton test's
_build_resolved_checkout (the real mini-dump rebuild); skips gracefully if the
mini-dump fixture is absent.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import logging
import os
import shutil
import sys
import tempfile

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package + the data-core test fixtures (skeleton pattern) ---
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(REPO_ROOT, "data", "refdata-extractor", "python")
DATA_CORE_TESTS = os.path.join(REPO_ROOT, "data", "refdata-extractor", "tests")
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

sys.path.insert(0, BACKEND_DIR)
sys.path.insert(0, DATA_CORE_PYDIR)

import import_to_sqlite as imp                      # noqa: E402
from app import data_core                           # noqa: E402
from app.config import CHECKOUT_ENV_VAR             # noqa: E402
from app.main import app                            # noqa: E402
from app.routes_read import _json_safe              # noqa: E402

DUMP_DIR = os.path.join(DATA_CORE_TESTS, "fixtures", "mini-dump",
                        "refdata-1.5.1164953")
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")

# The four valid derived-status tokens (read_api's STATUS_*). The endpoint surfaces
# one of these per row; the DERIVATION is 2a's oracle, not re-tested here.
VALID_STATUS = {"DEPRECATED", "SUPERSEDED", "VERIFIED", "UNVERIFIED"}


def _build_resolved_checkout():
    """A temp checkout laid out as app.config derives it -- <root>/data/seeds/ with
    the three seed CSVs + the rebuilt reference DBs (the skeleton test's pattern).
    Skips (not fails) if the data-core fixture inputs are absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="backend_read_checkout_")
    seed_dir = os.path.join(root, "data", "seeds")
    os.makedirs(seed_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(seed_dir, f))

    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir,
                                                 "address_versions_seed.csv")
    try:
        imp.run_rebuild(DUMP_DIR, seed_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved
    return root


@pytest.fixture(scope="module")
def resolved_checkout():
    root = _build_resolved_checkout()
    yield root
    shutil.rmtree(root, ignore_errors=True)


@pytest.fixture(scope="module")
def empty_checkout():
    root = tempfile.mkdtemp(prefix="backend_read_empty_")
    yield root
    shutil.rmtree(root, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    """A TestClient whose endpoints resolve a given checkout root: set KCDX_CHECKOUT
    (config priority 1) for the duration, exactly how an operator wires the mounted
    volume (D18). The endpoints call load_config() per request, so the env var is all
    that is needed to point them at the fixture."""
    def _make(checkout_root):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        return TestClient(app)
    return _make


def _out_dir(checkout_root):
    """The out_dir app.config derives for a checkout root (data/seeds), for calling
    the 2a functions directly to assert the endpoint surfaces THEIR values."""
    return os.path.join(checkout_root, "data", "seeds")


# ----------------------------------------------------------------------------
# Case 1: GET /entities surfaces the curated set (count + shape + valid status).
# ----------------------------------------------------------------------------
def test_list_entities_surfaces_curated_set(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    resp = client.get("/entities")
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert isinstance(body, list) and body, "expected a non-empty curated set"

    # The endpoint SURFACES read_curated_set's values -- count matches, and the
    # endpoint did not drop/add rows (thin caller, no re-derivation). _json_safe is
    # the API-boundary serialization seam the endpoint applies (recurse, reshape
    # nothing); the comparison applies the same seam so it stays honest about the
    # boundary.
    expected = _json_safe(data_core.read_curated_set(_out_dir(resolved_checkout)))
    assert len(body) == len(expected), (len(body), len(expected))
    assert body == expected, "endpoint must pass through read_curated_set verbatim"

    # Each row carries the s01-bound fields; a known row's status is a valid token.
    for row in body:
        assert set(row) == {"kcdx_id", "name", "status", "kind"}, row
        assert isinstance(row["kcdx_id"], int)
        assert isinstance(row["name"], str) and row["name"]
        assert row["status"] in VALID_STATUS, row["status"]


# ----------------------------------------------------------------------------
# Case 2: GET /entities/{id} surfaces identity + lifecycle for a known id.
# ----------------------------------------------------------------------------
def test_get_entity_surfaces_identity_and_lifecycle(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    # A known id: the first row of the curated set the data-core returns.
    known = data_core.read_curated_set(_out_dir(resolved_checkout))[0]
    kcdx_id = known["kcdx_id"]

    resp = client.get(f"/entities/{kcdx_id}")
    assert resp.status_code == 200, resp.text
    body = resp.json()
    # The endpoint passes through read_entity_detail's dict verbatim (modulo the
    # JSON-boundary serialization seam -- recurse, reshape nothing).
    expected = _json_safe(
        data_core.read_entity_detail(_out_dir(resolved_checkout), kcdx_id))
    assert body == expected, "endpoint must surface read_entity_detail verbatim"
    # The s02 identity + lifecycle fields are present.
    for key in ("kcdx_id", "name", "superseded_by", "superseded_at_version",
                "is_deprecated", "deprecated_at_version",
                "deprecation_replacement", "notes"):
        assert key in body, f"detail missing {key!r}: {body}"
    assert body["kcdx_id"] == kcdx_id
    assert body["name"] == known["name"]


# ----------------------------------------------------------------------------
# Case 3: GET /entities/{unknown_id} -> 404 (the 2a None contract).
# ----------------------------------------------------------------------------
def test_get_unknown_entity_is_404(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    # An id past the curated set's max -> read_entity_detail returns None -> 404.
    max_id = max(r["kcdx_id"]
                 for r in data_core.read_curated_set(_out_dir(resolved_checkout)))
    resp = client.get(f"/entities/{max_id + 100000}")
    assert resp.status_code == 404, resp.text


# ----------------------------------------------------------------------------
# Case 4: GET /entities/{id}/versions -> rows newest-first, each with a status.
# ----------------------------------------------------------------------------
def test_get_entity_versions_newest_first_with_status(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    out_dir = _out_dir(resolved_checkout)
    # Pick a known id that actually has version rows.
    kcdx_id = next(r["kcdx_id"] for r in data_core.read_curated_set(out_dir)
                   if data_core.read_version_rows(out_dir, r["kcdx_id"]))

    resp = client.get(f"/entities/{kcdx_id}/versions")
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert isinstance(body, list) and body, "expected >=1 version row"

    # The endpoint surfaces read_version_rows verbatim (thin caller), modulo the
    # JSON-boundary serialization seam (_json_safe -- recurse the structure, reshape
    # nothing). The data-core returns the curated display columns as JSON-native
    # scalars, so applying the same seam to its raw return proves the endpoint
    # reshapes NOTHING.
    expected = _json_safe(data_core.read_version_rows(out_dir, kcdx_id))
    assert body == expected, "endpoint must surface read_version_rows verbatim"

    # Each row carries a derived status; the data-core orders them newest-first --
    # the endpoint preserves that order (it serializes the list as-returned).
    for row in body:
        assert row.get("status") in VALID_STATUS, row.get("status")


# ----------------------------------------------------------------------------
# Case 4b: GET /modules surfaces the module registry (s04 `module` Select source).
# ----------------------------------------------------------------------------
def test_list_modules_surfaces_module_registry(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    resp = client.get("/modules")
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert isinstance(body, list) and body, "expected a non-empty module registry"

    # Thin caller: the endpoint passes through read_modules verbatim (modulo the
    # JSON-boundary serialization seam -- recurse, reshape nothing). The data-core
    # returns {id, name, path} as JSON-native scalars, so applying the same seam to
    # its raw return proves the endpoint reshapes NOTHING (D13/R3).
    expected = _json_safe(data_core.read_modules(_out_dir(resolved_checkout)))
    assert body == expected, "endpoint must surface read_modules verbatim"

    for row in body:
        assert set(row) == {"id", "name", "path"}, row
        assert isinstance(row["id"], int)
        assert isinstance(row["name"], str) and row["name"]
        assert isinstance(row["path"], str) and row["path"]


def test_modules_no_db_checkout_returns_empty_signal(empty_checkout, client_at):
    """A no-DB checkout -> GET /modules returns the same empty SIGNAL (200,
    state="empty") the other read endpoints return, not a 500 -- the s01 empty
    state the frontend binds."""
    client = client_at(empty_checkout)
    resp = client.get("/modules")
    assert resp.status_code == 200, resp.text
    assert resp.json()["state"] == "empty", resp.json()


# ----------------------------------------------------------------------------
# Case 5: a no-DB checkout -> the empty/error SIGNAL (200, state="empty"), logged --
# not a 500 crash. The s01 empty-state the frontend binds (mirrors /health).
# ----------------------------------------------------------------------------
def test_no_db_checkout_returns_empty_signal_and_logs(empty_checkout, client_at,
                                                       caplog):
    client = client_at(empty_checkout)
    with caplog.at_level(logging.WARNING, logger="app.routes_read"):
        resp = client.get("/entities")
    # Not a 500: the missing-DB read is a normal empty STATE, not a crash.
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["state"] == "empty", body
    assert empty_checkout in body["checkout_path"], body
    assert body["detail"], "the empty signal names the cause"
    # The failure is logged (logging.md -- the failure branch logs before returning).
    assert any("data-core read failed" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


def test_no_db_entity_and_versions_also_return_signal(empty_checkout, client_at):
    """The {id} and {id}/versions endpoints return the same no-DB empty signal (200,
    state="empty") -- they catch DbReadError before the None->404 / []-rows logic,
    so a missing checkout never surfaces as a 404 or a 500."""
    client = client_at(empty_checkout)
    for path in ("/entities/1", "/entities/1/versions"):
        resp = client.get(path)
        assert resp.status_code == 200, (path, resp.text)
        assert resp.json()["state"] == "empty", (path, resp.json())
