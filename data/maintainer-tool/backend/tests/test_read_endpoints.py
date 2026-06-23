"""test_read_endpoints.py -- the backend read endpoints.

WHAT THIS PROVES
----------------
The read endpoints SURFACE the data-core's read-for-display values end-to-end over
the REAL app + the REAL data-core (no mock of either -- the no-duplication self-check:
TestClient drives FastAPI against the real /entities routes, which call the real
seeds_shared.read_api in-process). The backend is a THIN CALLER: each case asserts
the endpoint passes through what the data-core returns (status/kind present, count
matches, newest-first ordering, the 404 / no-DB signal) -- NOT the status DERIVATION,
which is the data-core's own oracle (test_read_api_*.py). Cases:

  1. GET /entities over a resolved mini-dump checkout -> the curated set: each row has
     name/kcdx_id/status/kind; the count matches read_curated_set's; a known row's
     status is one of the four valid tokens (surfaced, not re-derived).

  2. GET /entities/{id} for a known id -> identity + the six lifecycle fields.

  3. GET /entities/{unknown_id} -> HTTP 404 (the data-core's None contract; a genuine
     not-found is a real HTTP error, distinct from the no-DB empty-state).

  4. GET /entities/{id}/versions -> the rows NEWEST-first, each carrying a status.

  5. A no-DB checkout (an empty dir that resolves no curated DB) -> every read
     endpoint returns the empty/error SIGNAL (HTTP 200, state="empty" -- the
     empty-state the frontend binds, mirroring /health), NOT a 500 crash, and the
     failure is LOGGED (asserted via caplog).

CHECKOUT WIRING
---------------
The endpoints call load_config() with no override -> they resolve the checkout via
KCDX_CHECKOUT (config priority 1). So each request points the endpoints at the
fixture checkout by setting KCDX_CHECKOUT for the duration (an env-var monkeypatch),
exactly how an operator wires the mounted volume. Reuses the skeleton test's
_build_resolved_checkout (the real mini-dump rebuild); skips gracefully if the
mini-dump fixture is absent.

RUN
---
    python -m pytest backend/tests/ -q
"""
import hashlib
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
DATA_CORE_PYDIR = os.path.join(BACKEND_DIR, "data_core")
DATA_CORE_TESTS = os.path.join(REPO_ROOT, "data", "refdata-extractor", "tests")
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

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
# one of these per row; the DERIVATION is the data-core's oracle, not re-tested here.
VALID_STATUS = {"DEPRECATED", "SUPERSEDED", "VERIFIED", "UNVERIFIED"}


def _build_resolved_checkout():
    """A temp checkout laid out as app.config derives it -- <root>/data/db-export/ holds
    the three curated CSVs (the rebuild genesis), and <root>/data/ (config.out_dir)
    holds the rebuilt reference DBs (the skeleton test's pattern). Skips (not fails) if
    the data-core fixture inputs are absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="backend_read_checkout_")
    seed_dir = os.path.join(root, "data", "db-export")  # config.seed_dir -- the curated CSV export dir
    out_dir = os.path.join(root, "data")               # config.out_dir == data/
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
        # Rebuild the DBs into out_dir (data/), where config.out_dir resolves them --
        # NOT the genesis CSV subdir (data/db-export/ holds the curated CSVs only).
        imp.run_rebuild(DUMP_DIR, out_dir)
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
    volume. The endpoints call load_config() per request, so the env var is all
    that is needed to point them at the fixture."""
    def _make(checkout_root):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        return TestClient(app)
    return _make


def _out_dir(checkout_root):
    """The out_dir app.config derives for a checkout root (data/), for calling the 2a
    functions directly to assert the endpoint surfaces THEIR values. Mirrors
    config.out_dir == <checkout>/data (the DBs live there, NOT the CSV subdir)."""
    return os.path.join(checkout_root, "data")


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

    # Each row carries the list-screen-bound fields; a known row's status is a valid token.
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
    # The detail-screen identity + lifecycle fields are present.
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

    # The verify-only content_hash crosses the wire (the function check): every row
    # carries it, as a lowercase-64-hex STRING (a fingerprinted row) or null (a
    # never-fingerprinted row) -- never absent, never a non-string non-null. It is a
    # SEPARATE field from the display columns (the endpoint surfaces it as the data-core
    # returns it; _json_safe passes the hex str through unchanged).
    for row in body:
        assert "content_hash" in row, f"verify-only content_hash absent: {row}"
        ch = row["content_hash"]
        assert ch is None or isinstance(ch, str), (
            f"content_hash must be a hex string or null, got {ch!r}")
        if ch is not None:
            assert len(ch) == 64 and ch == ch.lower() and all(
                c in "0123456789abcdef" for c in ch), (
                f"content_hash must be lowercase 64-char hex: {ch!r}")
    # At least one fingerprinted row exists in the fixture (a function row carries the
    # bulk-promote hash) -- proves content_hash genuinely crossed, not vacuously all-null.
    assert any(row["content_hash"] is not None for row in body), (
        "expected at least one fingerprinted (function) version row to surface a "
        "non-null verify-only content_hash")

    # THE FIX (the 422 bug): the version-rows endpoint exposes the
    # version-TAG STRING `valid_from_version` (the write-path identity key the editor
    # sends to save/confirm -- resolve_tag rejects the FK ordinal), ALONGSIDE the
    # `valid_from` ordinal (sort/status). The TAG must be a real server-known game
    # version (a known_version_tag), NOT the stringified ordinal. Read the server's
    # known tags from /health to assert the surfaced tag is one of them.
    known_tags = set(client.get("/health").json()["known_version_tags"])
    for row in body:
        assert "valid_from" in row, row              # the ordinal still ships (sort/status)
        assert "valid_from_version" in row, row      # the tag ships alongside (write identity)
        assert "valid_through_version" in row, row
        # The current (open-interval) row's valid_from_version is a real known tag, never
        # the ordinal -- a row sending the ordinal as version_tag is the 422 bug.
        if row["valid_from_version"] is not None:
            assert row["valid_from_version"] in known_tags, (
                f"valid_from_version {row['valid_from_version']!r} is not a known game "
                f"version {known_tags!r} -- the ordinal must not be surfaced as the tag")
            assert str(row["valid_from_version"]) != str(row["valid_from"]), (
                f"valid_from_version equals the ordinal {row['valid_from']!r} -- it must "
                f"be the TAG, not the FK ordinal (the 422 bug)")


# ----------------------------------------------------------------------------
# Case 4b: GET /modules surfaces the module registry (the `module` Select source).
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
    # its raw return proves the endpoint reshapes NOTHING.
    expected = _json_safe(data_core.read_modules(_out_dir(resolved_checkout)))
    assert body == expected, "endpoint must surface read_modules verbatim"

    for row in body:
        assert set(row) == {"id", "name", "path"}, row
        assert isinstance(row["id"], int)
        assert isinstance(row["name"], str) and row["name"]
        assert isinstance(row["path"], str) and row["path"]


def test_modules_no_db_checkout_returns_empty_signal(empty_checkout, client_at):
    """A no-DB checkout -> GET /modules returns the same empty SIGNAL (200,
    state="empty") the other read endpoints return, not a 500 -- the empty
    state the frontend binds."""
    client = client_at(empty_checkout)
    resp = client.get("/modules")
    assert resp.status_code == 200, resp.text
    assert resp.json()["state"] == "empty", resp.json()


# ----------------------------------------------------------------------------
# Case 5: a no-DB checkout -> the empty/error SIGNAL (200, state="empty"), logged --
# not a 500 crash. The empty-state the frontend binds (mirrors /health).
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
    # The failure is logged -- the failure branch logs before returning.
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


# ----------------------------------------------------------------------------
# Case 6: GET /needs-action surfaces the lifecycle-completeness set (the three
# incomplete-lifecycle kinds + a consistent total_count + version), READ-ONLY.
#
# WHAT THIS PROVES: the endpoint exposes audit_lifecycle's structured three-kind
# shape (uncovered / never_verified / broken_refs) + version, derives the ONE
# total_count the `[Needs action]` badge binds, and mutates NOTHING (the curated DB
# is byte-identical before/after -- DETECTION is read-only). The mini-dump checkout
# is the real curated seed set; whichever kinds are non-empty, the shape + count
# consistency + byte-identity are the falsifiable bar.
# ----------------------------------------------------------------------------
NEEDS_ACTION_KINDS = ("uncovered", "never_verified", "broken_refs")


def _db_hash(checkout_root):
    """A content hash over the curated reference DBs under the checkout's out_dir
    (data/) -- proves a read endpoint mutated nothing (byte-identical before/after)."""
    h = hashlib.sha256()
    for name in ("reference.sqlite", "reference-dev.sqlite"):
        p = os.path.join(_out_dir(checkout_root), name)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def test_needs_action_surfaces_three_kinds_and_consistent_count(resolved_checkout,
                                                                client_at):
    client = client_at(resolved_checkout)
    resp = client.get("/needs-action")
    assert resp.status_code == 200, resp.text
    body = resp.json()

    # The three kind lists + total_count + version are present (the badge bindings).
    for kind in NEEDS_ACTION_KINDS:
        assert kind in body, f"needs-action missing kind list {kind!r}: {body}"
        assert isinstance(body[kind], list), (kind, body[kind])
    assert "total_count" in body, f"needs-action missing total_count: {body}"
    assert "version" in body, f"needs-action missing version: {body}"

    # total_count is the consistent sum of the three lists' lengths (the badge).
    expected_total = sum(len(body[k]) for k in NEEDS_ACTION_KINDS)
    assert body["total_count"] == expected_total, (body["total_count"], expected_total)

    # The endpoint surfaces the data-core's three lists + version verbatim (thin caller,
    # modulo the JSON-boundary seam): total_count is the ONLY backend-derived field.
    expected = _json_safe(data_core.audit_lifecycle(_out_dir(resolved_checkout)))
    for kind in NEEDS_ACTION_KINDS:
        assert body[kind] == expected[kind], (
            f"endpoint must surface audit_lifecycle's {kind} verbatim")
    assert body["version"] == expected["version"]


def test_needs_action_is_read_only(resolved_checkout, client_at):
    """The GET is read-only: the curated DB is byte-identical after the request
    (DETECTION is read-only -- no write, no transaction)."""
    client = client_at(resolved_checkout)
    before = _db_hash(resolved_checkout)
    resp = client.get("/needs-action")
    assert resp.status_code == 200, resp.text
    assert _db_hash(resolved_checkout) == before, (
        "GET /needs-action mutated the curated DB -- a read endpoint mutates nothing")


def test_needs_action_no_db_checkout_returns_empty_signal(empty_checkout, client_at,
                                                          caplog):
    """A no-DB checkout -> GET /needs-action returns the same empty SIGNAL (200,
    state="empty") the other read endpoints return, not a 500 -- the empty state
    the frontend binds (the error/empty states route through it). The detection
    failure (DbReadError from _open_ro) is caught and logged before returning (the
    shared _no_db_signal branch this endpoint reuses)."""
    client = client_at(empty_checkout)
    with caplog.at_level(logging.WARNING, logger="app.routes_read"):
        resp = client.get("/needs-action")
    assert resp.status_code == 200, resp.text
    assert resp.json()["state"] == "empty", resp.json()
    assert any("data-core read failed" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


# ============================================================================
# A compact result emitter -- prints one PASS/FAIL line per load-bearing property +
# a final summary line, for a quick read of the suite verdict.
# ============================================================================
def _emit_signal(results):
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


def test_needs_action_acceptance_signal(resolved_checkout, client_at):
    client = client_at(resolved_checkout)
    results = []

    # ACCEPT 1: the endpoint returns the structured three-kind shape + total_count + version.
    resp = client.get("/needs-action")
    body = resp.json() if resp.status_code == 200 else {}
    ok1 = (resp.status_code == 200
           and all(k in body and isinstance(body[k], list) for k in NEEDS_ACTION_KINDS)
           and "total_count" in body and "version" in body)
    results.append(("needs-action-returns-three-kind-shape", ok1,
                    None if ok1 else f"status={resp.status_code} body={body}"))

    # ACCEPT 2: total_count equals the sum of the three lists' lengths (the badge).
    expected_total = sum(len(body.get(k, [])) for k in NEEDS_ACTION_KINDS) if body else -1
    ok2 = bool(body) and body.get("total_count") == expected_total
    results.append(("needs-action-total-count-consistent", ok2,
                    None if ok2 else f"total_count={body.get('total_count')} sum={expected_total}"))

    # ACCEPT 3: the GET is read-only (the curated DB is byte-identical after).
    before = _db_hash(resolved_checkout)
    client.get("/needs-action")
    ok3 = _db_hash(resolved_checkout) == before
    results.append(("needs-action-read-only", ok3,
                    None if ok3 else "GET /needs-action MUTATED the curated DB"))

    _emit_signal(results)
    failures = [(aid, d) for aid, ok, d in results if not ok]
    assert not failures, "needs-action acceptance drift:\n  " + \
        "\n  ".join(f"{aid}: {d}" for aid, d in failures)
