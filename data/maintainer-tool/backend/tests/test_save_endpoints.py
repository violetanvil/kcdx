"""test_save_endpoints.py -- the backend save endpoints (Phase 2 step 4b).

WHAT THIS PROVES
----------------
The six save endpoints drive the data-core's DEFERRED-COMMIT write path end-to-end
over the REAL app + the REAL data-core + the REAL mini-dump DBs (no mock of either --
the R3/law-6 self-check: TestClient drives FastAPI against the real /save/* routes,
which call real seeds_shared.db_editor in defer_commit mode). The backend is a THIN
CALLER: each case asserts the endpoint OPENS a held transaction (the write is
written-but-UNCOMMITTED -- a separate read-only DB read still sees the OLD value),
surfaces the data-core's result/flags, and -- on an invalid edit -- aborts with an
HTTP error, NO write, NO held txn. The write CORRECTNESS is the data-core's oracles
(test_db_editor_*); this step asserts the endpoint drives the deferred seam + holds
the txn for step 5.

THE HELD-BUT-UNCOMMITTED PROOF (the load-bearing assertion)
----------------------------------------------------------
After a successful save the transaction is HELD OPEN, not committed. A SEPARATE
read-only connection to the SAME user DB still reads the PRE-edit value (an
uncommitted SQLite transaction is invisible to other connections). That is the proof
the endpoint opened a deferred txn and did NOT commit -- step 5 owns the commit.

THE THREAD-AFFINITY PROOF (the step's load-bearing constraint)
--------------------------------------------------------------
The data-core opens the held connections with check_same_thread=True; the save runs
on the registry's per-save single-thread executor so the connections belong to that
thread, and step 5's commit/rollback run on the SAME executor. A test rolls the held
txn back THROUGH the registry's executor (the step-5 seam) and asserts the DB is
byte-identical -- proving the held txn is discardable from a different request thread
without the cross-thread sqlite3.ProgrammingError the constraint warns of.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import hashlib
import logging
import os
import shutil
import sqlite3
import sys
import tempfile

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package + the data-core test fixtures (read-test pattern) ---
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
from app.pending_saves import REGISTRY              # noqa: E402

DUMP_DIR = os.path.join(DATA_CORE_TESTS, "fixtures", "mini-dump",
                        "refdata-1.5.1164953")
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953" -- the only known version
GVO = imp.GAME_VERSION_ORDINAL


def _build_resolved_checkout():
    """A temp checkout laid out as app.config derives it -- <root>/data/seeds/ with
    the three seed CSVs + the rebuilt reference DBs (the read-test's pattern). Each
    save test gets a FRESH checkout (function scope) so a held/aborted save never
    pollutes another test's DB. Skips (not fails) if the mini-dump fixture is absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="backend_save_checkout_")
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


@pytest.fixture()
def checkout():
    """A FRESH resolved checkout per test (function scope) -- a held save leaves its
    txn open until reaped, so each test must not see another's DB state."""
    root = _build_resolved_checkout()
    yield root
    shutil.rmtree(root, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    """A TestClient whose endpoints resolve a given checkout root via KCDX_CHECKOUT
    (config priority 1 -- how an operator wires the mounted volume, D18)."""
    def _make(checkout_root):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        return TestClient(app)
    return _make


def _out_dir(checkout_root):
    return os.path.join(checkout_root, "data", "seeds")


def _user_db(checkout_root):
    return os.path.join(_out_dir(checkout_root), "reference.sqlite")


def _db_hash(checkout_root):
    """A content hash of the user DB file -- the byte-identical proof for the
    no-write-on-invalid + the rollback-leaves-clean cases. Read after closing every
    handle the test opened (a held save keeps its OWN connections open, but the FILE
    on disk is unchanged until commit, so the bytes match the pre-save hash)."""
    with open(_user_db(checkout_root), "rb") as f:
        return hashlib.sha256(f.read()).hexdigest()


def _read_trio_cell(checkout_root, kcdx_id, valid_from, column):
    """Read one address_versions cell from a SEPARATE read-only connection -- the
    held-but-uncommitted proof. An uncommitted save's edit is invisible here, so this
    returns the PRE-edit value while the txn is held. The column is a raw DB column
    (the data-core stores last_verified_at_version as a game_versions.id FK; we read
    verified_by/verified_date which are plain text -- visible old-vs-not changes)."""
    con = sqlite3.connect(f"file:{_user_db(checkout_root)}?mode=ro", uri=True)
    try:
        row = con.execute(
            f"SELECT av.{column} FROM address_versions av "
            "JOIN game_versions gv ON av.valid_from = gv.id "
            "WHERE av.kcdx_id = ? AND gv.tag = ?",
            (kcdx_id, valid_from)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _count_versions(checkout_root, kcdx_id):
    """Count an entity's address_versions rows from a SEPARATE read-only connection --
    the held-but-uncommitted proof for a create-version (the new row is invisible
    until commit)."""
    con = sqlite3.connect(f"file:{_user_db(checkout_root)}?mode=ro", uri=True)
    try:
        return con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ?",
            (kcdx_id,)).fetchone()[0]
    finally:
        con.close()


def _reap(save_id):
    """Roll back + discard a held save through the registry's executor (the step-5
    cancel SEAM) so a test's held txn does not leak into the next test. Idempotent --
    a missing save_id is a no-op. This IS the minimal internal rollback-via-registry
    the step builds to test the held state can be cleaned up; the HTTP cancel endpoint
    is step 5."""
    if save_id in REGISTRY.pending_ids():
        REGISTRY.run_on_executor(save_id, data_core.rollback)
        REGISTRY.discard(save_id)


# ============================================================================
# The six job shapes -- each opens a held deferred-commit save (written-but-
# UNCOMMITTED: a separate read still sees the OLD value), returns a save_id + the
# result/flags, and is reaped (rolled back) so the txn does not leak.
# ============================================================================
def test_update_version_holds_uncommitted(checkout, client_at):
    client = client_at(checkout)
    # The PRE-edit verified_by on (kcdx_id=1, GVT) -- the held-state baseline.
    before = _read_trio_cell(checkout, 1, GVT, "verified_by")
    assert before is not None

    resp = client.post("/save/update-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": GVT,
        "edits": {"verified_by": "ChangedReviewer"},
        "saved": {"verified_by": before},
        "prospective": {"verified_by": "ChangedReviewer"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert save_id in REGISTRY.pending_ids(), "the txn must be HELD"
        # HELD-BUT-UNCOMMITTED: a separate read still sees the OLD verified_by.
        assert _read_trio_cell(checkout, 1, GVT, "verified_by") == before, \
            "the held (uncommitted) edit must be invisible to a separate read"
        # The field delta surfaced (the data-core's, over saved/prospective).
        assert any(c["field"] == "verified_by" for c in body["field_delta"]), body
        # An UPDATE is NOT AP18-gated.
        assert "ap18_new_row" not in body, body
    finally:
        _reap(save_id)


# A NEW game-version tag for the new VERSION ROW (the row's valid_from_version, a seed
# cell -- distinct from the RESOLUTION version_tag, which stays GVT/known). A new
# version at a NEW tag validates cleanly + surfaces the AP18/D12 flags but
# materializes 0 rows (the apply diff materializes only baseline-version rows --
# policy.md; the data-core's own create_version oracle uses this exact pattern). It is
# the constructible held-success for create-version in a single-version fixture.
NEW_ROW_TAG = "1.6.2000000"


def test_create_version_holds_uncommitted_and_flags_ap18(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # A NEW version row for an existing entity at a NEW valid_from_version, with a
    # CHANGED cell (a different rva) so D12 nothing_changed is FALSE. The RESOLUTION
    # version_tag is GVT (known -- the adapter resolves it); the new ROW's
    # valid_from_version is the new tag.
    resp = client.post("/save/create-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": NEW_ROW_TAG,
        "columns": {"module": "WHGame.dll", "kind": "function",
                    "rva": "0x00ABCDEF"},
        "saved": {"rva": "0x0071A5A4"},
        "prospective": {"rva": "0x00ABCDEF"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert save_id in REGISTRY.pending_ids(), "the txn must be HELD"
        # AP18 surfaced for a new version (D11); D12 nothing_changed is FALSE (rva
        # differs from the source).
        assert body["ap18_new_row"] is True, body
        assert body["addition_kind"] == "version", body
        assert body["nothing_changed"] is False, body
        # HELD-BUT-UNCOMMITTED: the DB file is byte-identical until commit.
        assert _db_hash(checkout) == db_before, \
            "the held (uncommitted) create-version must not touch the DB file"
    finally:
        _reap(save_id)


def test_create_entity_holds_uncommitted_and_flags_ap18(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # A data_slot kind at a high RVA MINTS (fingerprint NULL) without the
    # function-kind bulk-baseline gate (mirrors the data-core's create-entity oracle,
    # which uses a non-function kind so the test exercises the INSERT machinery, not
    # the function-promote baseline). 0x09000000 is well past any function entry.
    resp = client.post("/save/create-entity", json={
        "version_tag": GVT,
        "name": "kcdx_test_new_entity",
        "first_version_columns": {"valid_from_version": GVT,
                                  "module": "WHGame.dll", "kind": "data_slot",
                                  "rva": "0x09000000"},
        "saved": {},
        "prospective": {"name": "kcdx_test_new_entity", "module": "WHGame.dll",
                        "kind": "data_slot"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert save_id in REGISTRY.pending_ids(), "the txn must be HELD"
        # AP18 surfaced for a new entity (D11).
        assert body["ap18_new_row"] is True, body
        assert body["addition_kind"] == "entity", body
        assert isinstance(body["kcdx_id"], int) and body["kcdx_id"] > 0, body
        # HELD-BUT-UNCOMMITTED: the new entity is invisible to a separate read (the
        # DB FILE is byte-identical until commit).
        assert _db_hash(checkout) == db_before, \
            "the held (uncommitted) new entity must not touch the DB file"
        assert _count_versions(checkout, body["kcdx_id"]) == 0, \
            "the new entity's row is invisible to a separate read until commit"
    finally:
        _reap(save_id)


def test_supersede_holds_uncommitted(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # Supersede entity 1 BY entity 2 (CGame_Update) at GVT -- a valid edge (no self-
    # supersede, no cycle). superseded_by carries the successor's NAME.
    resp = client.post("/save/supersede", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "superseded_by": "CGame_Update",
        "superseded_at_version": GVT,
        "saved": {"superseded_by": ""},
        "prospective": {"superseded_by": "CGame_Update"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert save_id in REGISTRY.pending_ids(), "the txn must be HELD"
        assert "ap18_new_row" not in body, "supersede is an UPDATE, not AP18-gated"
        # HELD-BUT-UNCOMMITTED: the DB file is byte-identical until commit.
        assert _db_hash(checkout) == db_before, \
            "the held (uncommitted) supersede must not touch the DB file"
    finally:
        _reap(save_id)


def test_deprecate_holds_uncommitted(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    resp = client.post("/save/deprecate", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "is_deprecated": True,
        "deprecated_at_version": GVT,
        "saved": {"is_deprecated": ""},
        "prospective": {"is_deprecated": "1"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert save_id in REGISTRY.pending_ids(), "the txn must be HELD"
        assert "ap18_new_row" not in body, "deprecate is an UPDATE, not AP18-gated"
        assert _db_hash(checkout) == db_before, \
            "the held (uncommitted) deprecate must not touch the DB file"
    finally:
        _reap(save_id)


# ============================================================================
# The D12 nothing-changed verdict fires for an identical new version.
# ============================================================================
def test_create_version_nothing_changed_fires(checkout, client_at):
    """A new version IDENTICAL to its source on every authored column except
    valid_from_version surfaces nothing_changed=True (D12 -- steer to re-verify). Use
    a NULL-trio non-function source (kcdx_id 19, a vtable_index with vtable_slot=4) so
    the identical copy at the NEW tag is itself apply-valid; the new-tag row validates
    + surfaces the flags (and materializes 0 rows -- the single-version-fixture
    pattern). The held txn is reaped."""
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # An IDENTICAL copy of entity 19's source row at the new tag -- same module/kind/
    # vtable_slot, NULL trio. Only valid_from_version differs -> nothing_changed True.
    resp = client.post("/save/create-version", json={
        "version_tag": GVT,
        "kcdx_id": 19,
        "valid_from_version": NEW_ROW_TAG,
        "columns": {"module": "WHGame.dll", "kind": "vtable_index",
                    "vtable_slot": "4"},
        "saved": {"module": "WHGame.dll", "kind": "vtable_index", "vtable_slot": "4"},
        "prospective": {"module": "WHGame.dll", "kind": "vtable_index",
                        "vtable_slot": "4"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    save_id = body["save_id"]
    try:
        assert body["ap18_new_row"] is True, body
        assert body["nothing_changed"] is True, \
            "D12 nothing_changed must fire for an identical-except-valid_from copy"
        assert _db_hash(checkout) == db_before, "the held create-version writes nothing"
    finally:
        _reap(save_id)


# ============================================================================
# Invalid edits per shape -> HTTP error, NO save_id, DB byte-identical, logged.
# ============================================================================
def test_invalid_update_unknown_column_aborts(checkout, client_at, caplog):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    with caplog.at_level(logging.WARNING, logger="app.routes_save"):
        resp = client.post("/save/update-version", json={
            "version_tag": GVT,
            "kcdx_id": 1,
            "valid_from_version": GVT,
            # `kcdx_id` is the identity key -- never editable (DbEditError).
            "edits": {"kcdx_id": "999"},
        })
    assert resp.status_code == 422, resp.text
    assert REGISTRY.pending_ids() == pending_before, "no held txn on an invalid edit"
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on a reject"
    assert any("save rejected" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


def test_invalid_update_malformed_date_aborts(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    # A partial trio / malformed verified_date is the validator's reject. Set only
    # verified_date to a malformed value while the other trio cells stay -> the
    # validator rejects (a malformed date / a trio that is no longer all-set-or-null).
    resp = client.post("/save/update-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": GVT,
        "edits": {"verified_date": "not-a-date"},
    })
    assert resp.status_code == 422, resp.text
    assert REGISTRY.pending_ids() == pending_before, "no held txn on a validator reject"
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on a reject"


def test_invalid_create_entity_missing_required_aborts(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    # Missing required `module` / `kind` on the first version row -> the validator
    # rejects (no write, no held txn).
    resp = client.post("/save/create-entity", json={
        "version_tag": GVT,
        "name": "kcdx_missing_required",
        "first_version_columns": {"valid_from_version": GVT},
    })
    assert resp.status_code == 422, resp.text
    assert REGISTRY.pending_ids() == pending_before
    assert _db_hash(checkout) == db_before, "no write on an invalid create-entity"


def test_invalid_supersede_self_aborts(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    # Entity 1 superseding ITSELF (superseded_by == its own name "lua_pcall") is a
    # validator HARD ERROR (no self-supersede) -> 422, no write.
    resp = client.post("/save/supersede", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "superseded_by": "lua_pcall",
        "superseded_at_version": GVT,
    })
    assert resp.status_code == 422, resp.text
    assert REGISTRY.pending_ids() == pending_before
    assert _db_hash(checkout) == db_before, "no write on a self-supersede"


def test_invalid_deprecate_replacement_without_deprecated_aborts(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    # deprecation_replacement set while NOT deprecated is a validator HARD ERROR
    # (replacement-requires-deprecated) -> 422, no write.
    resp = client.post("/save/deprecate", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "is_deprecated": False,
        "deprecation_replacement": "CGame_Update",
    })
    assert resp.status_code == 422, resp.text
    assert REGISTRY.pending_ids() == pending_before
    assert _db_hash(checkout) == db_before, "no write on replacement-without-deprecated"


# ============================================================================
# The version= path (no DLL): a valid tag resolves via the adapter; an unknown tag
# -> the adapter's VersionTagError -> an HTTP error. No dll_path ever read.
# ============================================================================
def test_unknown_version_tag_aborts(checkout, client_at, caplog):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    pending_before = REGISTRY.pending_ids()
    with caplog.at_level(logging.WARNING, logger="app.routes_save"):
        resp = client.post("/save/update-version", json={
            "version_tag": "9.9.9999999",      # not a known game version
            "kcdx_id": 1,
            "valid_from_version": GVT,
            "edits": {"verified_by": "X"},
        })
    assert resp.status_code == 422, resp.text
    assert "not a known game version" in resp.text
    assert REGISTRY.pending_ids() == pending_before, "no held txn on an unknown tag"
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on an unknown tag"
    assert any("unknown version tag" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


# ============================================================================
# The thread-affinity mechanism: the save runs on the registry's executor, the
# handle is retrievable by save_id, and a rollback THROUGH the registry's executor
# (the step-5 cancel seam) leaves the DB byte-identical -- the held txn discardable
# from a different request thread with no cross-thread sqlite3.ProgrammingError.
# ============================================================================
def test_registry_holds_handle_and_rollback_via_executor_leaves_clean(checkout,
                                                                       client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    resp = client.post("/save/update-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": GVT,
        "edits": {"verified_by": "ThreadAffinityProbe"},
    })
    assert resp.status_code == 200, resp.text
    save_id = resp.json()["save_id"]

    # The registry HOLDS the handle for a later commit/rollback (step 5's seam).
    assert save_id in REGISTRY.pending_ids()
    handle = REGISTRY.get(save_id)
    assert handle is not None and not handle.finished, "the handle is held, open"

    # Roll back THROUGH the registry's executor -- the SAME thread that opened the
    # connections (thread affinity). On a different thread this would raise
    # sqlite3.ProgrammingError; through the executor it succeeds, proving the seam.
    REGISTRY.run_on_executor(save_id, data_core.rollback)
    assert handle.finished, "the held txn is rolled back (discarded)"
    REGISTRY.discard(save_id)
    assert save_id not in REGISTRY.pending_ids(), "the reaped save is gone"

    # The held txn was DISCARDED, never committed -> the DB is byte-identical.
    assert _db_hash(checkout) == db_before, \
        "rolling back the held txn leaves the DB byte-identical (nothing landed)"
