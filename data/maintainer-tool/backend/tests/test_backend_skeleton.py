"""test_backend_skeleton.py -- the maintainer-tool backend skeleton (Phase 2 step 1).

WHAT THIS PROVES
----------------
The backend boots and its skeleton works END-TO-END over the REAL app + the REAL
data-core import seam (no mock of the app, no stubbed data-core -- the duplication
AP / R3 self-check: the test drives FastAPI's TestClient against the real /health
route, which imports the real seeds_shared in-process). Cases:

  1. APP BOOTS + the import seam resolves: TestClient constructs the real app, and
     a /health call returns 200 with the documented response shape.

  2. RESOLVED checkout: pointed at a checkout that HAS the reference DB + the three
     seed CSVs (built from the mini-dump fixture the data-core tests use), /health
     reports state="resolved", every artifact present, and the known version tag
     listed (US-1 the happy path; the dropdown source D10/US-10).

  3. EMPTY checkout: pointed at an empty dir (no DB, no seeds), /health reports
     state="empty" and NAMES the missing artifacts (US-1 acceptance / S7 "Empty --
     no DB/seeds resolved (names where the backend looked)"). This is the s01 empty
     state the frontend renders.

  4. The version-tag ADAPTER maps a KNOWN tag -> the (tag, ordinal) the data-core
     would have produced from a DLL of that version, and REJECTS an unknown tag
     (VersionTagError) -- the "resolved version another way, no DLL read" the design
     names (S5). Asserted against the REAL adapter over the resolved checkout +
     against the data-core's own baseline constant.

  5. The adapter REFUSES to fabricate a dll_path: data_core_dll_param raises
     NotImplementedError (the surfaced integration fork -- the adapter resolves the
     version context and stops at the data-core's param boundary; the write-call
     threading is the user's open decision, steps 2-5).

SEED/BASELINE FIXTURE
---------------------
Reuses the data-core's apply-oracle mechanism (the same one test_db_editor_*.py
use): a module-scoped baseline DB rebuilt ONCE from the committed seeds off the
mini-dump excerpt, copied into a temp "checkout" laid out as the backend's config
expects (data/seeds/ holding the seeds + the two reference DBs). No real WHGame.dll
is read -- the backend never reads a DLL (D14/D18); the rebuild path uses the dump
excerpt only.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import os
import shutil
import sys
import tempfile

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package + the data-core test fixtures -----------------
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(REPO_ROOT, "data", "refdata-extractor", "python")
DATA_CORE_TESTS = os.path.join(REPO_ROOT, "data", "refdata-extractor", "tests")
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

# The backend package imports as `app.*` -- put `backend/` on the path so
# `import app.main` resolves (the same dir uvicorn runs `app.main:app` from).
sys.path.insert(0, BACKEND_DIR)
# The data-core python dir (the import seam adds it too, but the rebuild helper
# here imports import_to_sqlite directly to build the fixture DB).
sys.path.insert(0, DATA_CORE_PYDIR)

import import_to_sqlite as imp                      # noqa: E402
from app import adapter                             # noqa: E402
from app.config import load_config                  # noqa: E402
from app.main import app, _checkout_status          # noqa: E402

# The mini-dump excerpt the data-core tests rebuild from (a fast real rebuild).
DUMP_DIR = os.path.join(DATA_CORE_TESTS, "fixtures", "mini-dump",
                        "refdata-1.5.1164953")
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")
GVT = imp.GAME_VERSION_TAG          # "1.5.1164953"
GVO = imp.GAME_VERSION_ORDINAL      # 1164953


# ----------------------------------------------------------------------------
# Build a "resolved" checkout: data/seeds/ with the seeds + the two reference DBs,
# laid out exactly as app.config derives them.
# ----------------------------------------------------------------------------
def _build_resolved_checkout():
    """A temp dir laid out as a real checkout: <root>/data/seeds/ holds the three
    seed CSVs AND the rebuilt reference.sqlite + reference-dev.sqlite. Returns the
    checkout root. Skips (not fails) if the data-core fixture inputs are absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="backend_checkout_")
    seed_dir = os.path.join(root, "data", "seeds")
    os.makedirs(seed_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(seed_dir, f))

    # Rebuild the reference DBs into the checkout's seed dir (config.out_dir ==
    # data/seeds), pointing the importer's seed-path constants at the checkout's
    # seeds for the duration (the data-core apply-oracle convention).
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
    root = tempfile.mkdtemp(prefix="backend_empty_")
    yield root
    shutil.rmtree(root, ignore_errors=True)


# ----------------------------------------------------------------------------
# Case 1: the real app boots + /health responds with the documented shape.
# ----------------------------------------------------------------------------
def test_app_boots_and_health_responds():
    client = TestClient(app)
    resp = client.get("/health")
    assert resp.status_code == 200, resp.text
    body = resp.json()
    # The documented response shape (the keys s01 feeds the frontend).
    for key in ("state", "detail", "checkout_path", "checkout_source",
                "seed_dir", "out_dir", "artifacts", "known_version_tags"):
        assert key in body, f"/health response missing {key!r}: {body}"
    assert body["state"] in ("resolved", "empty", "error"), body["state"]
    # The artifacts block names each required piece.
    assert set(body["artifacts"]) == {"user_db", "dev_db", "seed_files"}


# ----------------------------------------------------------------------------
# Case 2: a RESOLVED checkout -> state="resolved", every artifact present.
# ----------------------------------------------------------------------------
def test_resolved_checkout_reports_resolved(resolved_checkout):
    status = _checkout_status(load_config(checkout_override=resolved_checkout))
    assert status["state"] == "resolved", status["detail"]
    assert status["checkout_source"] == "override"
    assert status["artifacts"]["user_db"] is True
    assert status["artifacts"]["dev_db"] is True
    assert all(status["artifacts"]["seed_files"].values()), status["artifacts"]
    # The known version tag is listed (the dropdown source, D10/US-10).
    assert GVT in status["known_version_tags"], status["known_version_tags"]


# ----------------------------------------------------------------------------
# Case 3: an EMPTY checkout -> state="empty", missing artifacts NAMED (US-1/S7).
# ----------------------------------------------------------------------------
def test_empty_checkout_reports_empty_and_names_missing(empty_checkout):
    status = _checkout_status(load_config(checkout_override=empty_checkout))
    assert status["state"] == "empty", status["detail"]
    assert status["artifacts"]["user_db"] is False
    assert not all(status["artifacts"]["seed_files"].values())
    # The empty-state copy NAMES where the backend looked + what is missing (S7).
    assert empty_checkout in status["checkout_path"]
    assert "missing:" in status["detail"], status["detail"]
    assert "reference.sqlite" in status["detail"], status["detail"]
    # An empty checkout still offers the data-core baseline tag (the floor) so the
    # dropdown is never blank -- known_versions falls back to the baseline constant.
    assert status["known_version_tags"] == [GVT], status["known_version_tags"]


# ----------------------------------------------------------------------------
# Case 4: the version-tag ADAPTER maps a known tag -> (tag, ordinal); rejects
# an unknown tag (the "resolved version, no DLL read" the design names, S5).
# ----------------------------------------------------------------------------
def test_adapter_resolves_known_tag(resolved_checkout):
    config = load_config(checkout_override=resolved_checkout)
    ctx = adapter.resolve_tag(config, GVT)
    # Exactly what resolve_version(dll_path) returns for a DLL of this version --
    # produced from the tag, with NO DLL read.
    assert ctx.tag == GVT
    assert ctx.ordinal == GVO


def test_adapter_rejects_unknown_tag(resolved_checkout):
    config = load_config(checkout_override=resolved_checkout)
    with pytest.raises(adapter.VersionTagError):
        adapter.resolve_tag(config, "9.9.9999999")


def test_adapter_known_versions_floor_without_db(empty_checkout):
    # With no DB resolved, the known set is the data-core's baseline constant (the
    # floor) -- read from the data-core, not a backend copy.
    config = load_config(checkout_override=empty_checkout)
    versions = adapter.known_versions(config)
    assert versions == {GVT: GVO}, versions


# ----------------------------------------------------------------------------
# Case 5: the adapter REFUSES to fabricate a dll_path (the surfaced fork).
# ----------------------------------------------------------------------------
def test_adapter_refuses_to_fabricate_dll_path():
    ctx = adapter.VersionContext(tag=GVT, ordinal=GVO)
    with pytest.raises(NotImplementedError):
        adapter.data_core_dll_param(ctx)
