"""test_reverify_batch_endpoint.py -- the /save/reverify-batch PREVIEW endpoint.

WHAT THIS PROVES
----------------
The /save/reverify-batch endpoint drives the REAL app + the REAL data-core
`reverify_resolver` over the REAL mini-dump DBs (no mock -- TestClient against the real
route, which calls real seeds_shared.reverify_resolver). It is a PREVIEW: it resolves
the v3 report rows into per-row edit-specs + returns the per-row FIELD-DELTAS, and --
the load-bearing property -- WRITES NOTHING (the DB is BYTE-IDENTICAL after the
preview, asserted by hashing the DB files before + after). The resolve CORRECTNESS is
the data-core oracle (test_reverify_resolver); this asserts the endpoint drives the
resolve seam, shapes the batch field-delta list, and never touches the DB / opens a
transaction.

THE NO-WRITE PROOF (the load-bearing assertion)
-----------------------------------------------
A PREVIEW endpoint writes nothing and opens no transaction (Save-previews /
Confirm-transacts). After the /save/reverify-batch POST the reference DB files are
BYTE-IDENTICAL to before. The write is /confirm/batch's, not this one. FALSIFIABLE: a
preview that mutated the DB fails the byte-identical row.

NOTE -- a PREVIEW touches no git remote (it writes nothing, opens no txn), so this test
needs no temp bare-git remote (unlike the confirm-endpoint tests, which run `git -C`
and need the safe.bareRepository=all form). The checkout fixture builds only the
rebuilt DBs + a direct-injected crafted state; no git setup.

RUN
---
    python -m pytest backend/tests/test_reverify_batch_endpoint.py -q
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

HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))
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

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953"
LATER_TAG = "1.6.2000000"
LATER_ORDINAL = 2000000
MID_TAG = "1.5.1500000"
MID_ORDINAL = 1500000


def _gv_id(con, tag):
    row = con.execute("SELECT id FROM game_versions WHERE tag = ?", (tag,)).fetchone()
    return row[0] if row else None


def _ek_id(con, val):
    row = con.execute(
        'SELECT id FROM "_dict_address_versions_evidence_kind" WHERE val = ?',
        (val,)).fetchone()
    return row[0] if row else None


def _inject_state(user_db):
    """Direct-inject the crafted state into the USER reference.sqlite (the endpoint
    reads it READ-ONLY): the later/middle game_versions + four crafted curated rows
    (a closed gap row, an open already-covered row, an already-verified-beyond row, an
    open close-target). Returns the injected kcdx_ids + av ids the cases key on. Done
    BEFORE the byte-identical baseline is hashed -- this is fixture setup, not the
    endpoint's write."""
    con = sqlite3.connect(user_db)
    try:
        gvt_id = _gv_id(con, GVT)
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (MID_TAG, MID_ORDINAL))
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (LATER_TAG, LATER_ORDINAL))
        mid_id = _gv_id(con, MID_TAG)
        later_id = _gv_id(con, LATER_TAG)
        # The OLD evidence_kind on the crafted rows is a DIFFERENT tier (maintainer_ghidra)
        # so the re-verify's new evidence_kind (live_production / live_test_plugin) is a
        # VISIBLE change in the field-delta (an unchanged cell is absent from a delta --
        # the resolver still emits it in `edits`, but the human signal needs the change).
        ek_old = _ek_id(con, "maintainer_ghidra")

        names = con.execute("SELECT MAX(id) FROM address_names").fetchone()[0] or 0
        avmax = con.execute("SELECT MAX(id) FROM address_versions").fetchone()[0] or 0
        module_id = con.execute("SELECT id FROM modules LIMIT 1").fetchone()[0]
        kind_id = con.execute(
            'SELECT id FROM "_dict_address_versions_kind" WHERE val = ?',
            ("function",)).fetchone()[0]

        def _add_entity(name):
            nonlocal names
            names += 1
            con.execute("INSERT INTO address_names (id, name) VALUES (?, ?)",
                        (names, name))
            return names

        def _add_av(kcdx_id, vf, vt, lvv, vb, vd, ek):
            nonlocal avmax
            avmax += 1
            con.execute(
                "INSERT INTO address_versions "
                "(id, kcdx_id, kind, module_id, rva, valid_from, valid_through, "
                " last_verified_at_version, verified_by, verified_date, evidence_kind) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (avmax, kcdx_id, kind_id, module_id, 0x1000 + avmax, vf, vt, lvv,
                 vb, vd, ek))
            return avmax

        e_gap = _add_entity("kcdx_test_gap_closed")
        id_gap = _add_av(e_gap, gvt_id, mid_id, gvt_id, "Old", "2025-01-01", ek_old)
        e_open = _add_entity("kcdx_test_open_covered")
        id_open = _add_av(e_open, gvt_id, None, gvt_id, "Old", "2025-01-01", ek_old)
        e_close = _add_entity("kcdx_test_close")
        id_close = _add_av(e_close, gvt_id, None, mid_id, "Old", "2025-01-01", ek_old)
        # ALREADY-CLOSED-TO-LAST-VERIFIED (close-intervals already-acted): valid_from=GVT,
        # valid_through=MID, last_verified=MID -- valid_through == last_verified, both
        # non-NULL. The resolver's close-intervals path SKIPS it (the close is already done),
        # so it produces NO spec -> the endpoint must classify it `already_acted`.
        e_closed_done = _add_entity("kcdx_test_closed_done")
        id_closed_done = _add_av(e_closed_done, gvt_id, mid_id, mid_id, "Old",
                                 "2025-01-01", ek_old)
        con.commit()
        return {"e_gap": e_gap, "id_gap": id_gap, "e_open": e_open, "id_open": id_open,
                "e_close": e_close, "id_close": id_close,
                "e_closed_done": e_closed_done, "id_closed_done": id_closed_done}
    finally:
        con.close()


def _build_resolved_checkout():
    """A temp checkout (<root>/data/db-export + <root>/data with the rebuilt DBs), then
    the crafted state injected into the USER DB. Skips if the mini-dump is absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="reverify_batch_checkout_")
    seed_dir = os.path.join(root, "data", "db-export")
    out_dir = os.path.join(root, "data")
    os.makedirs(seed_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(seed_dir, f))

    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir, "address_versions_seed.csv")
    try:
        imp.run_rebuild(DUMP_DIR, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved

    ids = _inject_state(os.path.join(out_dir, "reference.sqlite"))
    return root, ids


@pytest.fixture()
def checkout():
    root, ids = _build_resolved_checkout()
    yield root, ids
    shutil.rmtree(root, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    def _make(checkout_root):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        return TestClient(app)
    return _make


def _out_dir(root):
    return os.path.join(root, "data")


def _db_hash(root):
    h = hashlib.sha256()
    for name in ("reference.sqlite", "reference-dev.sqlite"):
        p = os.path.join(_out_dir(root), name)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def _row_for(body, kcdx_id):
    for r in body["rows"]:
        if r["kcdx_id"] == kcdx_id:
            return r
    return None


def _delta_field(row, name):
    for c in row["field_delta"]:
        if c["field"] == name:
            return (c["old"], c["new"])
    return None


def _report_row(kcdx_id, version, verdict, rank, matched):
    return {"kcdx_id": kcdx_id, "version": version, "verdict": verdict,
            "method_rank": rank, "matched_address_version_id": matched}


# ============================================================================
# Verify-all preview: returns the per-row field-deltas + edits, WRITES NOTHING.
# ============================================================================
def test_reverify_batch_verify_all_previews_and_writes_nothing(checkout, client_at):
    root, ids = checkout
    client = client_at(root)
    db_before = _db_hash(root)

    resp = client.post("/save/reverify-batch", json={
        "action": "verify-all",
        "rows": [
            _report_row(ids["e_gap"], LATER_TAG, "verified_working", 1, ids["id_gap"]),
            _report_row(ids["e_open"], LATER_TAG, "passed_not_verified", 3,
                        ids["id_open"]),
        ],
        "author_name": "BatchSigner",
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["action"] == "verify-all", body

    gap = _row_for(body, ids["e_gap"])
    openrow = _row_for(body, ids["e_open"])
    assert gap is not None and openrow is not None, body

    # The proof-rank evidence_kind surfaces in the field-delta (the human signal).
    assert _delta_field(gap, "evidence_kind")[1] == "live_production", gap
    assert _delta_field(openrow, "evidence_kind")[1] == "live_test_plugin", openrow
    # The audit trio: verified_by is the injected author; last_verified -> swept.
    assert _delta_field(gap, "verified_by")[1] == "BatchSigner", gap
    assert _delta_field(gap, "last_verified_at_version")[1] == LATER_TAG, gap
    # The gap-extension on the CLOSED gap row.
    assert _delta_field(gap, "valid_through_version") == (MID_TAG, LATER_TAG), gap
    # The OPEN already-covered interval: NO valid_through edit in the delta or edits.
    assert _delta_field(openrow, "valid_through_version") is None, openrow
    assert "valid_through_version" not in openrow["edits"], openrow
    # The edits round-trip the BatchRowSpec shape the FE re-posts to /confirm/batch.
    assert gap["edits"]["evidence_kind"] == "live_production", gap

    # THE LOAD-BEARING ASSERTION: the preview wrote NOTHING.
    assert _db_hash(root) == db_before, "a reverify-batch preview must not touch the DB"


# ============================================================================
# Close-intervals preview: the retract surfaces in the delta, WRITES NOTHING.
# ============================================================================
def test_reverify_batch_close_intervals_previews_and_writes_nothing(checkout,
                                                                    client_at):
    root, ids = checkout
    client = client_at(root)
    db_before = _db_hash(root)

    resp = client.post("/save/reverify-batch", json={
        "action": "close-intervals",
        "rows": [_report_row(ids["e_close"], LATER_TAG, "failed", 5, None)],
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()
    close = _row_for(body, ids["e_close"])
    assert close is not None, body
    # The retract: valid_through -> the row's last_verified (MID). Old is '' (open).
    assert _delta_field(close, "valid_through_version") == ("", MID_TAG), close
    assert close["edits"]["valid_through_version"] == MID_TAG, close
    assert _db_hash(root) == db_before, "a close-intervals preview must not touch the DB"


# ============================================================================
# Report-vs-DB reconciliation: an already-acted row is classified
# `already_acted` / no-action EXPLICITLY (not silently omitted), the open row stays
# actionable, the preview WRITES NOTHING. The FE reads this classification, never
# re-derives it.
# ============================================================================
def test_reverify_batch_close_intervals_classifies_already_acted(checkout, client_at):
    root, ids = checkout
    client = client_at(root)
    db_before = _db_hash(root)

    # A close-intervals batch with BOTH an OPEN row (e_close -> actionable close) AND an
    # ALREADY-CLOSED-TO-LAST-VERIFIED row (e_closed_done -> already-acted, the resolver
    # produces no spec for it).
    # The already-closed row is swept at MID (the version its closed interval [GVT, MID]
    # CONTAINS) -- the resolver resolves the interval-containing row for the SWEPT version,
    # then hits the already-closed skip (valid_through == last_verified == MID). The OPEN
    # row (e_close, [GVT, open]) is swept at LATER -- the open interval covers it, and its
    # last_verified (MID) < swept, so it is a live close.
    resp = client.post("/save/reverify-batch", json={
        "action": "close-intervals",
        "rows": [
            _report_row(ids["e_close"], LATER_TAG, "failed", 5, None),
            _report_row(ids["e_closed_done"], MID_TAG, "failed", 5, None),
        ],
    })
    assert resp.status_code == 200, resp.text
    body = resp.json()

    # (1) The already-closed row is classified already_acted / no-action: NO field-delta,
    # NO edits, carrying its identity + the close-intervals marker.
    done = _row_for(body, ids["e_closed_done"])
    assert done is not None, body
    assert done["status"] == "already_acted", done
    assert done["kcdx_id"] == ids["e_closed_done"], done
    assert done["version"] == MID_TAG, done
    assert done["reason"] == "interval already closed", done
    assert "field_delta" not in done, done
    assert "edits" not in done, done

    # (2) The OPEN row is classified actionable WITH its valid_through field-delta + edits
    # -- the classification did not break the working actionable path.
    close = _row_for(body, ids["e_close"])
    assert close is not None, body
    assert close["status"] == "actionable", close
    assert _delta_field(close, "valid_through_version") == ("", MID_TAG), close
    assert close["edits"]["valid_through_version"] == MID_TAG, close

    # (3) The preview WROTE NOTHING (the DB is byte-identical).
    assert _db_hash(root) == db_before, \
        "a reverify-batch preview must not touch the DB (already-acted classification)"


# ============================================================================
# A structural report-vs-DB mismatch -> 422, DB byte-identical, logged.
# ============================================================================
def test_reverify_batch_stale_matched_id_rejected(checkout, client_at, caplog):
    root, ids = checkout
    client = client_at(root)
    db_before = _db_hash(root)
    with caplog.at_level(logging.WARNING, logger="app.routes_save"):
        resp = client.post("/save/reverify-batch", json={
            "action": "verify-all",
            "rows": [_report_row(ids["e_gap"], LATER_TAG, "verified_working", 1,
                                 99999999)],  # a matched id no av row has
        })
    assert resp.status_code == 422, resp.text
    assert _db_hash(root) == db_before, "the DB is byte-identical on a reject"
    assert any("reverify-batch preview rejected" in r.message for r in caplog.records), \
        [r.message for r in caplog.records]


def test_reverify_batch_bad_action_rejected(checkout, client_at):
    root, ids = checkout
    client = client_at(root)
    db_before = _db_hash(root)
    resp = client.post("/save/reverify-batch", json={
        "action": "nonsense",
        "rows": [_report_row(ids["e_gap"], LATER_TAG, "verified_working", 1,
                             ids["id_gap"])],
    })
    assert resp.status_code == 422, resp.text
    assert _db_hash(root) == db_before, "the DB is byte-identical on a bad action"


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


def test_reverify_batch_acceptance_signal(checkout, client_at):
    root, ids = checkout
    client = client_at(root)
    results = []

    # ACCEPT 1: a verify-all preview returns the per-row field-deltas + edits.
    db_before = _db_hash(root)
    r = client.post("/save/reverify-batch", json={
        "action": "verify-all",
        "rows": [_report_row(ids["e_gap"], LATER_TAG, "verified_working", 1,
                             ids["id_gap"])],
        "author_name": "AcceptSigner",
    })
    body = r.json() if r.status_code == 200 else {}
    gap = _row_for(body, ids["e_gap"]) if body else None
    ok1 = (r.status_code == 200 and gap is not None
           and gap["edits"].get("evidence_kind") == "live_production"
           and gap["edits"].get("valid_through_version") == LATER_TAG)
    results.append(("reverify-batch-preview-returns-deltas", ok1,
                    None if ok1 else f"status={r.status_code} body={body}"))

    # ACCEPT 2: the preview wrote NOTHING (the DB is byte-identical).
    ok2 = _db_hash(root) == db_before
    results.append(("reverify-batch-preview-writes-nothing", ok2,
                    None if ok2 else "the preview MUTATED the DB"))

    # ACCEPT 3: an already-acted row is classified already_acted / no-action
    # EXPLICITLY (not silently omitted), carrying its marker, no field-delta/edits.
    db_before3 = _db_hash(root)
    r3 = client.post("/save/reverify-batch", json={
        "action": "close-intervals",
        "rows": [_report_row(ids["e_closed_done"], MID_TAG, "failed", 5, None)],
    })
    body3 = r3.json() if r3.status_code == 200 else {}
    done = _row_for(body3, ids["e_closed_done"]) if body3 else None
    ok3 = (r3.status_code == 200 and done is not None
           and done.get("status") == "already_acted"
           and done.get("reason") == "interval already closed"
           and "edits" not in done
           and _db_hash(root) == db_before3)
    results.append(("reverify-batch-classifies-already-acted", ok3,
                    None if ok3 else f"status={r3.status_code} body={body3}"))

    _emit_signal(results)
    failures = [(aid, d) for aid, ok, d in results if not ok]
    assert not failures, "reverify-batch acceptance drift:\n  " + \
        "\n  ".join(f"{aid}: {d}" for aid, d in failures)
