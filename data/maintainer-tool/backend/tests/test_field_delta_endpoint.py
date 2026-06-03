"""test_field_delta_endpoint.py -- the field-delta endpoint (Phase 2 step 3, D8 + D12).

WHAT THIS PROVES
----------------
POST /field-delta SURFACES the data-core's field_delta / is_new_version_nothing_changed
end-to-end over the REAL app + the REAL data-core (no mock of either -- the R3/D13 self-check:
TestClient drives FastAPI against the real route, which calls the real seeds_shared in-process).
The backend is a THIN CALLER: each delta case PINS the response to what the data-core's
field_delta returns directly (not a hand-built expectation), so a drift in the data-core or a
re-computation in the backend surfaces. Cases:

  1. A version-row edit with several changed fields -> the changed fields as {field, old, new},
     in the data-core's deterministic order, unchanged fields absent; the list of (field, old,
     new) tuples equals what field_delta returns directly (pinned to the data-core).
  2. A None-vs-'' (and ''-vs-missing) no-op field is NOT in the delta (the data-core's
     empty-cell semantics surface through the API).
  3. record_kind="names" orders the delta by ADDRESS_NAMES_CSV_HEADER (a lifecycle edit).
  4. The D12 verdict: is_new_version=true + a new version identical to its source except
     valid_from_version -> nothing_changed=True; a real change -> nothing_changed=False; and
     the verdict field is ABSENT when is_new_version is not set.
  5. The response list ORDER matches the data-core's OrderedDict order (the whole point of a
     list, not a JSON object -- asserted explicitly).

This endpoint is PURE -- no DB, no checkout, no mini-dump fixture (the data-core's field_delta
is a pure dict-vs-dict comparison). So the test needs no fixture and no skip guard.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import os
import sys

from fastapi.testclient import TestClient

# --- locate the backend package + the data-core (the read-endpoint test's pattern) ---
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(REPO_ROOT, "data", "refdata-extractor", "python")

sys.path.insert(0, BACKEND_DIR)
sys.path.insert(0, DATA_CORE_PYDIR)

from app import data_core                           # noqa: E402
from app.main import app                            # noqa: E402

client = TestClient(app)


def _tuples(changes):
    """The response list as (field, old, new) tuples -- the comparable shape to pin
    against the data-core's OrderedDict.items()."""
    return [(c["field"], c["old"], c["new"]) for c in changes]


# ----------------------------------------------------------------------------
# Case 1: a version-row edit -> exactly the changed fields, pinned to the data-core.
# ----------------------------------------------------------------------------
def test_version_delta_surfaces_changed_fields_pinned_to_data_core():
    saved = {
        "kcdx_id": "7", "valid_from_version": "1.4", "module": "WHGame.dll",
        "rva": "0x1000", "kind": "function", "last_verified_at_version": "1.4",
        "evidence_kind": "maintainer_ghidra",
    }
    prospective = dict(saved)
    prospective["rva"] = "0x2000"
    prospective["last_verified_at_version"] = "1.5"
    prospective["evidence_kind"] = "live_test_plugin"

    resp = client.post("/field-delta",
                       json={"saved": saved, "prospective": prospective})
    assert resp.status_code == 200, resp.text
    body = resp.json()

    # Pin to the data-core: the endpoint surfaces field_delta verbatim (reshaped to a list),
    # re-computing nothing. A backend that re-derived the delta would drift from this.
    expected = list(data_core.field_delta(
        saved, prospective,
        field_order=data_core.ADDRESS_VERSIONS_CSV_HEADER).items())
    expected_tuples = [(field, old, new) for field, (old, new) in expected]
    assert _tuples(body["changes"]) == expected_tuples, (body["changes"], expected_tuples)

    # The exact changed set (and unchanged fields absent: module/kind/kcdx_id/valid_from).
    changed_fields = {c["field"] for c in body["changes"]}
    assert changed_fields == {"rva", "last_verified_at_version", "evidence_kind"}, changed_fields
    # No D12 verdict requested -> the field is absent (an UPDATE does not steer).
    assert "nothing_changed" not in body, body


# ----------------------------------------------------------------------------
# Case 2: a None-vs-'' / ''-vs-missing no-op field is NOT in the delta.
# ----------------------------------------------------------------------------
def test_empty_cell_noop_is_not_a_change():
    # signature: None on one side, '' on the other -> the data-core's _cell treats both as
    # the SAME empty cell, so it must NOT appear in the delta. verified_by: '' vs missing
    # key -> also an empty-vs-empty no-op. Only rva genuinely changes.
    saved = {"kcdx_id": "7", "signature": None, "verified_by": "", "rva": "0x1000"}
    prospective = {"kcdx_id": "7", "signature": "", "rva": "0x2000"}

    resp = client.post("/field-delta",
                       json={"saved": saved, "prospective": prospective})
    assert resp.status_code == 200, resp.text
    changes = resp.json()["changes"]

    changed_fields = {c["field"] for c in changes}
    assert changed_fields == {"rva"}, changed_fields  # signature/verified_by are no-ops
    # Pin the whole result to the data-core (the empty-cell semantics are the data-core's).
    expected = list(data_core.field_delta(
        saved, prospective,
        field_order=data_core.ADDRESS_VERSIONS_CSV_HEADER).items())
    expected_tuples = [(field, old, new) for field, (old, new) in expected]
    assert _tuples(changes) == expected_tuples, (changes, expected_tuples)


# ----------------------------------------------------------------------------
# Case 3: record_kind="names" orders by ADDRESS_NAMES_CSV_HEADER (lifecycle edit).
# ----------------------------------------------------------------------------
def test_names_record_kind_orders_by_names_header():
    # A names(lifecycle)-row edit: change `notes` (late in the names header) and
    # `superseded_by` (early). The order must follow ADDRESS_NAMES_CSV_HEADER, which is what
    # record_kind="names" selects -- NOT the version header (where these columns don't exist).
    saved = {"id": "7", "name": "Foo", "superseded_by": "", "notes": "old"}
    prospective = {"id": "7", "name": "Foo", "superseded_by": "9", "notes": "new"}

    resp = client.post("/field-delta",
                       json={"saved": saved, "prospective": prospective,
                             "record_kind": "names"})
    assert resp.status_code == 200, resp.text
    changes = resp.json()["changes"]

    # Pinned to the data-core called with the NAMES header (proves record_kind selected it).
    expected = list(data_core.field_delta(
        saved, prospective,
        field_order=data_core.ADDRESS_NAMES_CSV_HEADER).items())
    expected_tuples = [(field, old, new) for field, (old, new) in expected]
    assert _tuples(changes) == expected_tuples, (changes, expected_tuples)
    # superseded_by precedes notes in the names header -> that order, not insertion order.
    fields = [c["field"] for c in changes]
    assert fields.index("superseded_by") < fields.index("notes"), fields


# ----------------------------------------------------------------------------
# Case 4: the D12 nothing-changed verdict (new version identical except valid_from).
# ----------------------------------------------------------------------------
def test_d12_nothing_changed_true_for_identical_new_version():
    source = {
        "kcdx_id": "7", "valid_from_version": "1.4", "module": "WHGame.dll",
        "rva": "0x1000", "kind": "function", "last_verified_at_version": "1.4",
    }
    # A new version: a copy at a new valid_from_version, nothing else differs -> D12 fires.
    new_version = dict(source)
    new_version["valid_from_version"] = "1.5"

    resp = client.post("/field-delta",
                       json={"saved": source, "prospective": new_version,
                             "is_new_version": True})
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["nothing_changed"] is True, body
    # Pin to the data-core's own verdict.
    assert body["nothing_changed"] == data_core.is_new_version_nothing_changed(
        source, new_version)


def test_d12_nothing_changed_false_when_a_real_field_changed():
    source = {
        "kcdx_id": "7", "valid_from_version": "1.4", "module": "WHGame.dll",
        "rva": "0x1000", "kind": "function", "last_verified_at_version": "1.4",
    }
    new_version = dict(source)
    new_version["valid_from_version"] = "1.5"
    new_version["rva"] = "0x2000"  # a real change beyond valid_from -> NOT nothing-changed

    resp = client.post("/field-delta",
                       json={"saved": source, "prospective": new_version,
                             "is_new_version": True})
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["nothing_changed"] is False, body
    assert body["nothing_changed"] == data_core.is_new_version_nothing_changed(
        source, new_version)


# ----------------------------------------------------------------------------
# Case 5: the response list ORDER matches the data-core's OrderedDict order.
# ----------------------------------------------------------------------------
def test_response_list_order_matches_data_core_ordereddict():
    # Author the prospective dict with keys in a DIFFERENT order than the version header, so a
    # naive pass-through of dict insertion order would FAIL this -- the list must follow the
    # data-core's deterministic (authored-header) order, which is why a list is used.
    saved = {
        "evidence_kind": "maintainer_ghidra", "rva": "0x1000",
        "kind": "function", "module": "WHGame.dll", "kcdx_id": "7",
    }
    prospective = {
        "evidence_kind": "live_test_plugin", "rva": "0x2000",
        "kind": "data", "module": "WHGame.dll", "kcdx_id": "7",
    }

    resp = client.post("/field-delta",
                       json={"saved": saved, "prospective": prospective})
    assert resp.status_code == 200, resp.text
    fields = [c["field"] for c in resp.json()["changes"]]

    # The data-core orders by ADDRESS_VERSIONS_CSV_HEADER: module < rva < kind < evidence_kind.
    # The response must reproduce THAT order, regardless of the input dicts' key order.
    expected_order = list(data_core.field_delta(
        saved, prospective,
        field_order=data_core.ADDRESS_VERSIONS_CSV_HEADER).keys())
    assert fields == expected_order, (fields, expected_order)
    # Concretely: module is not changed (so absent); rva, kind, evidence_kind are, in header order.
    assert fields == ["rva", "kind", "evidence_kind"], fields


# ----------------------------------------------------------------------------
# Case 6: an unrecognized record_kind is REJECTED (422), never silently defaulted.
# ----------------------------------------------------------------------------
def test_unknown_record_kind_is_rejected_not_silently_defaulted():
    # record_kind is a closed Literal["version","names"]. A typo must be a clean 422 (Pydantic),
    # NOT a silent fall-through to the version order -- a wrong-order delta shipping silently is
    # the failure this guards (the field order is load-bearing for s06 layout stability).
    saved = {"kcdx_id": "7", "rva": "0x1000"}
    prospective = {"kcdx_id": "7", "rva": "0x2000"}

    resp = client.post("/field-delta",
                       json={"saved": saved, "prospective": prospective,
                             "record_kind": "verison"})  # deliberate typo
    assert resp.status_code == 422, resp.text
