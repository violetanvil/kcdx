"""test_save_endpoints.py -- the backend save (PREVIEW) endpoints.

WHAT THIS PROVES
----------------
The six save endpoints DRY-VALIDATE a prospective edit and return the field-delta
over the REAL app + the REAL data-core + the REAL mini-dump DBs (no mock of either --
the no-duplication self-check: TestClient drives FastAPI against the real /save/* routes,
which call real seeds_shared.db_editor in validate_only mode). The backend is a THIN
CALLER: each case asserts the endpoint returns the field-delta + the validator's
verdict (valid/invalid) + the create flags (new-row-approval / nothing_changed), and --
the load-bearing property -- WRITES NOTHING: a Save (valid OR invalid) leaves the DB
BYTE-IDENTICAL. The write CORRECTNESS is the data-core's oracles (test_db_editor_* +
test_validate_prospective); this asserts the endpoint drives the dry-validate seam +
shapes the preview for the save screen.

THE NO-WRITE PROOF (the load-bearing assertion)
-----------------------------------------------
Save is PREVIEW-ONLY ("Save-previews / Confirm-transacts -- NOTHING held across
think-time"). After ANY Save -- valid, invalid, a create, a lifecycle edit -- the DB
file is BYTE-IDENTICAL to before. There is no held transaction, no registry, no open
connection to check: the proof IS the byte-identical DB. The write is the Confirm
step's, not this one.

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

# --- locate the backend package + the data-core test fixtures (read-test pattern) ---
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(BACKEND_DIR, "data_core")
DATA_CORE_TESTS = os.path.join(REPO_ROOT, "data", "refdata-extractor", "tests")
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, BACKEND_DIR)
sys.path.insert(0, DATA_CORE_PYDIR)

import import_to_sqlite as imp                      # noqa: E402
from app.config import CHECKOUT_ENV_VAR             # noqa: E402
from app.main import app                            # noqa: E402

DUMP_DIR = os.path.join(DATA_CORE_TESTS, "fixtures", "mini-dump",
                        "refdata-1.5.1164953")
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953" -- the only known version


def _build_resolved_checkout():
    """A temp checkout laid out as app.config derives it -- <root>/data/db-export/ with
    the three curated CSVs (the rebuild genesis) +
    <root>/data/ (config.out_dir) with the rebuilt reference DBs (the read-test's pattern).
    Each save test gets a FRESH checkout (function scope) -- though a preview writes
    nothing, a fresh DB keeps the byte-identical baseline unambiguous per test. Skips
    (not fails) if the mini-dump fixture is absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="backend_save_checkout_")
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
        # Rebuild into out_dir (data/), where config.out_dir resolves the DBs.
        imp.run_rebuild(DUMP_DIR, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved
    return root


@pytest.fixture()
def checkout():
    """A FRESH resolved checkout per test (function scope)."""
    root = _build_resolved_checkout()
    yield root
    shutil.rmtree(root, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    """A TestClient whose endpoints resolve a given checkout root via KCDX_CHECKOUT
    (config priority 1 -- how an operator wires the mounted volume)."""
    def _make(checkout_root):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        return TestClient(app)
    return _make


def _out_dir(checkout_root):
    # config.out_dir == <checkout>/data (the DBs live there, NOT the CSV subdir).
    return os.path.join(checkout_root, "data")


def _user_db(checkout_root):
    return os.path.join(_out_dir(checkout_root), "reference.sqlite")


def _dev_db(checkout_root):
    return os.path.join(_out_dir(checkout_root), "reference-dev.sqlite")


def _db_hash(checkout_root):
    """A content hash over BOTH reference DB FILES -- the byte-identical no-write
    proof. A Save is preview-only: the files are unchanged before and after every
    Save (valid or invalid), so this hash matches the pre-save hash."""
    h = hashlib.sha256()
    for db in (_user_db(checkout_root), _dev_db(checkout_root)):
        with open(db, "rb") as f:
            h.update(f.read())
    return h.hexdigest()


def _field(body, name):
    """The (old, new) of one field in the response's field_delta list, or None."""
    for c in body["field_delta"]:
        if c["field"] == name:
            return (c["old"], c["new"])
    return None


# A NEW game-version tag for a new VERSION ROW (the row's valid_from_version, a seed
# cell -- distinct from the RESOLUTION version_tag, which stays GVT/known). A new
# version at a NEW tag validates cleanly + surfaces the new-row-approval / nothing-changed
# flags but materializes 0 rows (the apply diff materializes only baseline-version rows;
# the data-core's own create_version oracle uses this exact pattern). It is the
# constructible valid preview for create-version in a single-version fixture.
NEW_ROW_TAG = "1.6.2000000"


# ============================================================================
# The six job shapes -- each Save VALIDATES a valid prospective edit, returns the
# field-delta + valid:true (+ the create flags), and WRITES NOTHING (byte-identical).
# ============================================================================
def test_update_version_previews_valid(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)

    resp = client.post("/save/update-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": GVT,
        "edits": {"verified_by": "ChangedReviewer"},
        "saved": {"verified_by": "OldReviewer"},
        "prospective": {"verified_by": "ChangedReviewer"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is True, body
    assert body["errors"] == [], body
    # The field delta surfaced (the data-core's, over saved/prospective).
    assert _field(body, "verified_by") == ("OldReviewer", "ChangedReviewer"), body
    # An UPDATE does not require new-row approval.
    assert "ap18_new_row" not in body, body
    # NO WRITE: the DB is byte-identical -- a Save preview touches nothing.
    assert _db_hash(checkout) == db_before, "a Save preview must not touch the DB"


def test_create_version_previews_valid_and_flags_ap18(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # A NEW version row for an existing entity at a NEW valid_from_version, with a
    # CHANGED cell (a different rva) so nothing_changed is FALSE. The RESOLUTION
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
    assert body["valid"] is True, body
    # A new version surfaces the approval flag; nothing_changed is FALSE (rva differs).
    assert body["ap18_new_row"] is True, body
    assert body["addition_kind"] == "version", body
    assert body["nothing_changed"] is False, body
    assert _db_hash(checkout) == db_before, "the create-version preview wrote nothing"


def test_create_entity_previews_valid_and_flags_ap18(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # A data_slot kind at a high RVA MINTS (fingerprint NULL) without the
    # function-kind bulk-baseline gate (mirrors the data-core's create-entity oracle).
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
    assert body["valid"] is True, body
    # A new entity surfaces the approval flag + the assigned id.
    assert body["ap18_new_row"] is True, body
    assert body["addition_kind"] == "entity", body
    assert isinstance(body["kcdx_id"], int) and body["kcdx_id"] > 0, body
    assert _db_hash(checkout) == db_before, "the create-entity preview wrote nothing"


def test_supersede_previews_valid(checkout, client_at):
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
    assert body["valid"] is True, body
    assert _field(body, "superseded_by") == ("", "CGame_Update"), body
    assert "ap18_new_row" not in body, "supersede is an UPDATE, no new-row approval"
    assert _db_hash(checkout) == db_before, "the supersede preview wrote nothing"


def test_deprecate_previews_valid(checkout, client_at):
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
    assert body["valid"] is True, body
    assert "ap18_new_row" not in body, "deprecate is an UPDATE, no new-row approval"
    assert _db_hash(checkout) == db_before, "the deprecate preview wrote nothing"


def test_edit_notes_previews_valid(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # Edit entity 1's notes -- a standalone curated prose column (no pair rule). The field delta
    # surfaces the notes change; an UPDATE -> no new-row approval; the preview writes nothing.
    resp = client.post("/save/edit-notes", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "notes": "verified against the 1.5 build",
        "saved": {"notes": ""},
        "prospective": {"notes": "verified against the 1.5 build"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is True, body
    # The field-delta carries the notes change (record_kind "names").
    assert _field(body, "notes") == ("", "verified against the 1.5 build"), body
    assert "ap18_new_row" not in body, "edit-notes is an UPDATE, no new-row approval"
    assert _db_hash(checkout) == db_before, "the edit-notes preview wrote nothing"


# ============================================================================
# The nothing-changed verdict fires for an identical new version preview.
# ============================================================================
def test_create_version_nothing_changed_fires(checkout, client_at):
    """A new version IDENTICAL to its source on every authored column except
    valid_from_version surfaces nothing_changed=True (steer to re-verify). Use
    a NULL-trio non-function source (kcdx_id 19, a vtable_index with vtable_slot=4) so
    the identical copy at the NEW tag is itself apply-valid."""
    client = client_at(checkout)
    db_before = _db_hash(checkout)
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
    assert body["valid"] is True, body
    assert body["ap18_new_row"] is True, body
    assert body["nothing_changed"] is True, \
        "nothing_changed must fire for an identical-except-valid_from copy"
    assert _db_hash(checkout) == db_before, "the create-version preview wrote nothing"


# ============================================================================
# Invalid edits per shape -> valid:false + the validator's error, DB byte-identical,
# logged. The field-delta is still shown (WHAT was tried alongside WHY it is invalid).
# ============================================================================
def test_invalid_update_unknown_column_aborts(checkout, client_at, caplog):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    with caplog.at_level(logging.WARNING, logger="app.routes_save"):
        resp = client.post("/save/update-version", json={
            "version_tag": GVT,
            "kcdx_id": 1,
            "valid_from_version": GVT,
            # `kcdx_id` is the identity key -- never editable (DbEditError -> 422,
            # a malformed edit SHAPE, the caller's bug -- not a validation verdict).
            "edits": {"kcdx_id": "999"},
        })
    assert resp.status_code == 422, resp.text
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on a reject"
    assert any("save preview rejected" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


def test_invalid_update_malformed_date_previews_invalid(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # A malformed verified_date is the validator's reject (a CONTENT verdict, not a
    # shape error) -> 200 valid:false + the error, NOT an HTTP error.
    resp = client.post("/save/update-version", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "valid_from_version": GVT,
        "edits": {"verified_date": "not-a-date"},
        "saved": {"verified_date": ""},
        "prospective": {"verified_date": "not-a-date"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is False, body
    assert body["errors"] and isinstance(body["errors"][0], str), body
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on a reject"


def test_invalid_create_entity_missing_required_previews_invalid(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # Missing required `module` / `kind` on the first version row -> the validator
    # rejects (a content verdict) -> 200 valid:false. No write.
    resp = client.post("/save/create-entity", json={
        "version_tag": GVT,
        "name": "kcdx_missing_required",
        "first_version_columns": {"valid_from_version": GVT},
        "saved": {},
        "prospective": {"name": "kcdx_missing_required"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is False, body
    assert body["errors"], body
    assert _db_hash(checkout) == db_before, "no write on an invalid create-entity"


def test_invalid_supersede_self_previews_invalid(checkout, client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # Entity 1 superseding ITSELF (superseded_by == its own name "lua_pcall") is a
    # validator HARD ERROR (no self-supersede) -> 200 valid:false, no write.
    resp = client.post("/save/supersede", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "superseded_by": "lua_pcall",
        "superseded_at_version": GVT,
        "saved": {"superseded_by": ""},
        "prospective": {"superseded_by": "lua_pcall"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is False, body
    assert body["errors"], body
    assert _db_hash(checkout) == db_before, "no write on a self-supersede"


def test_invalid_deprecate_replacement_without_deprecated_previews_invalid(checkout,
                                                                           client_at):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    # deprecation_replacement set while NOT deprecated is a validator HARD ERROR
    # (replacement-requires-deprecated) -> 200 valid:false, no write.
    resp = client.post("/save/deprecate", json={
        "version_tag": GVT,
        "kcdx_id": 1,
        "is_deprecated": False,
        "deprecation_replacement": "CGame_Update",
        "saved": {"deprecation_replacement": ""},
        "prospective": {"deprecation_replacement": "CGame_Update"},
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["valid"] is False, body
    assert body["errors"], body
    assert _db_hash(checkout) == db_before, "no write on replacement-without-deprecated"


# ============================================================================
# The version= path (no DLL): a valid tag resolves via the adapter; an unknown tag
# -> the adapter's VersionTagError -> an HTTP 422 (bad input, before the data-core).
# No dll_path ever read; the DB is byte-identical.
# ============================================================================
def test_unknown_version_tag_aborts(checkout, client_at, caplog):
    client = client_at(checkout)
    db_before = _db_hash(checkout)
    with caplog.at_level(logging.WARNING, logger="app.routes_save"):
        resp = client.post("/save/update-version", json={
            "version_tag": "9.9.9999999",      # not a known game version
            "kcdx_id": 1,
            "valid_from_version": GVT,
            "edits": {"verified_by": "X"},
        })
    assert resp.status_code == 422, resp.text
    assert "not a known game version" in resp.text
    assert _db_hash(checkout) == db_before, "the DB is byte-identical on an unknown tag"
    assert any("unknown version tag" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]
