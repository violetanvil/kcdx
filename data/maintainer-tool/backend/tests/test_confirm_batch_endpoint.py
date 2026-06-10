"""test_confirm_batch_endpoint.py -- the backend BATCH Confirm transaction (Phase 6
step 6.2: the D32 / §7 batch save-spine -- N version-row UPDATEs as ONE atomic
transaction, all-or-nothing).

WHAT THIS PROVES
----------------
POST /confirm/batch runs the WHOLE atomic batch save SYNCHRONOUSLY in one request over
the REAL app + the REAL data-core (update_version_rows_batch -> ONE apply_direct_edit
-> ONE DeferredCommit) + the REAL mini-dump DBs + REAL git (a THROWAWAY LOCAL BARE
remote -- NOT real GitHub). The transaction opens AND closes inside the one request. The
cases assert the load-bearing properties:

  - SUCCESS: a batch of N version-row UPDATEs lands -- every one of the N rows holds its
    new value in the DB, ONE git commit staging ONLY the 3 data/db-export/ CSVs by exact
    path, the request-context author, the push reached the throwaway bare remote.

  - ALL-OR-NOTHING (the D21/D32 invariant -- the load-bearing case): a batch whose ONE
    row is invalid (an out-of-enum evidence_kind the validator rejects) returns FAILED
    and rolls EVERYTHING back -- the DB + db-export CSVs are BYTE-IDENTICAL INCLUDING
    sqlite_sequence, NO new commit, the remote did not advance, nothing landed. The
    earlier VALID rows are NOT committed. FALSIFIABLE BY DESIGN: a per-row-commit
    fake-batch would have landed the valid rows before the bad one -> the state hash
    would DIFFER -> this assertion fails. This is the single canonical guard against a
    commit-per-row impl labeled "batch".

  - AUTHOR: the request-context identity authors the batch commit (D17).

REAL EVERYTHING; skips gracefully if the mini-dump fixture is absent. Emits the canonical
acceptance signal (.claude/rules/acceptance-signal.md) to stdout -- the agent greps the
ACCEPT-RESULT / ACCEPT-SUITE tokens; the user reads nothing.

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import hashlib
import os
import shutil
import sqlite3
import subprocess
import sys
import tempfile

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package + the data-core test fixtures (the confirm-test pattern) ---
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(REPO_ROOT, "data", "refdata-extractor", "python")
DATA_CORE_TESTS = os.path.join(REPO_ROOT, "data", "refdata-extractor", "tests")
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, BACKEND_DIR)
sys.path.insert(0, DATA_CORE_PYDIR)

import import_to_sqlite as imp                       # noqa: E402
from app.config import CHECKOUT_ENV_VAR              # noqa: E402
from app.git_commit import PUSH_TOKEN_ENV_VAR, PRIVATE_REMOTE  # noqa: E402
from app.main import app                             # noqa: E402

DUMP_DIR = os.path.join(DATA_CORE_TESTS, "fixtures", "mini-dump",
                        "refdata-1.5.1164953")
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")
DB_FILES = ("reference.sqlite", "reference-dev.sqlite")

# The exact paths Confirm stages (must match routes_confirm._staged_rel_paths): ONLY the
# 3 derived CSVs at data/db-export/ (D20). The DB is the local originator (D1) -- not staged.
STAGED_REL_PATHS = sorted([
    "data/db-export/module_seed.csv",
    "data/db-export/address_names_seed.csv",
    "data/db-export/address_versions_seed.csv",
])

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953"
AUTHOR_NAME = "Batch Maintainer"
AUTHOR_EMAIL = "batch.maintainer@example.invalid"


def _git(checkout, *args, env=None, check=True):
    # -c safe.bareRepository=all: the test creates its OWN throwaway bare remote in
    # a temp dir (see _build_git_checkout) and runs git ops against it (rev-parse on
    # the bare repo). A machine with safe.bareRepository=explicit otherwise refuses
    # ALL bare-repo ops; that guard is for untrusted bare repos in shared locations,
    # not a test's own temp remote -- so allow it for these self-created repos.
    proc = subprocess.run(["git", "-c", "safe.bareRepository=all", "-C", checkout, *args],
                          capture_output=True, text=True, env=env)
    if check and proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {proc.stderr or proc.stdout}")
    return proc


def _build_git_checkout():
    """A temp checkout that is a REAL git repo (seeds + rebuilt DBs + seeded db-export
    CSVs committed as the baseline) plus a THROWAWAY LOCAL BARE remote `private`. Mirrors
    test_confirm_endpoint._build_git_checkout. Skips if the mini-dump fixture is absent."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="batch_confirm_checkout_")
    seed_dir = os.path.join(root, "data", "seeds")
    out_dir = os.path.join(root, "data")
    export_dir = os.path.join(root, "data", "db-export")
    os.makedirs(seed_dir, exist_ok=True)
    os.makedirs(export_dir, exist_ok=True)
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

    from seeds_shared.csv_exporter import export_seeds as _export_seeds
    _export_seeds(os.path.join(out_dir, "reference.sqlite"), export_dir)

    _git(root, "init", "-q")
    _git(root, "config", "user.name", "Baseline")
    _git(root, "config", "user.email", "baseline@example.invalid")
    _git(root, "config", "commit.gpgsign", "false")
    _git(root, "add", "--", *STAGED_REL_PATHS)
    _git(root, "commit", "-q", "-m", "baseline: seeds + reference DBs + db-export record")

    bare = tempfile.mkdtemp(prefix="batch_confirm_bare_remote_")
    _git(bare, "init", "--bare", "-q")
    _git(root, "remote", "add", PRIVATE_REMOTE, bare)
    _git(root, "push", "-q", PRIVATE_REMOTE, "HEAD:refs/heads/main")

    return root, bare


@pytest.fixture()
def checkout():
    root, bare = _build_git_checkout()
    yield root, bare
    shutil.rmtree(root, ignore_errors=True)
    shutil.rmtree(bare, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    def _make(checkout_root, *, push=False):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        if push:
            monkeypatch.setenv(PUSH_TOKEN_ENV_VAR, "dummy-local-token")
        else:
            monkeypatch.delenv(PUSH_TOKEN_ENV_VAR, raising=False)
        return TestClient(app)
    return _make


# --- helpers over the checkout's DBs / git state -----------------------------------
def _out_dir(root):
    return os.path.join(root, "data")


def _export_dir(root):
    return os.path.join(root, "data", "db-export")


def _user_db(root):
    return os.path.join(_out_dir(root), "reference.sqlite")


def _db_rows_hash(db_path):
    """A canonical hash over every table's full ordered rows + sqlite_sequence -- the
    LOGICAL content of one DB (the 4d-restore oracle: the row-level restore is logically
    byte-identical but physically rewrites the file). Mirrors test_confirm_endpoint."""
    con = sqlite3.connect(db_path)
    try:
        tables = [r[0] for r in con.execute(
            "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
        h = hashlib.sha256()
        for t in tables:
            cols = [r[1] for r in con.execute(f'PRAGMA table_info("{t}")')]
            order = ",".join(f'"{c}"' for c in cols) or "rowid"
            h.update(f"TABLE {t}\n".encode())
            for row in con.execute(f'SELECT * FROM "{t}" ORDER BY {order}'):
                h.update(repr(row).encode() + b"\n")
        return h.hexdigest()
    finally:
        con.close()


def _state_hash(root):
    """The no-change proof for the all-or-nothing case: both DBs' LOGICAL row content
    (incl. sqlite_sequence) + the 3 db-export CSVs' raw bytes."""
    h = hashlib.sha256()
    for f in DB_FILES:
        h.update(_db_rows_hash(os.path.join(_out_dir(root), f)).encode())
    for f in SEED_FILES:
        with open(os.path.join(_export_dir(root), f), "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def _sqlite_sequences(root):
    con = sqlite3.connect(_user_db(root))
    try:
        has = con.execute("SELECT name FROM sqlite_master WHERE type='table' "
                          "AND name='sqlite_sequence'").fetchone()
        if not has:
            return {}
        return {name: seq for name, seq in
                con.execute("SELECT name, seq FROM sqlite_sequence")}
    finally:
        con.close()


def _head_sha(repo):
    return _git(repo, "rev-parse", "HEAD").stdout.strip()


def _changed_files_in_head(root):
    out = _git(root, "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD").stdout
    return sorted(p.strip() for p in out.splitlines() if p.strip())


def _head_author(root):
    name = _git(root, "log", "-1", "--format=%an").stdout.strip()
    email = _git(root, "log", "-1", "--format=%ae").stdout.strip()
    return name, email


def _pick_trio_rows(root, n):
    """The first `n` curated FUNCTION-trio rows from the checkout's USER DB (a re-verify
    edit has a trio to change). Returns (kcdx_id, verified_by) pairs at the baseline tag.
    Never a hardcoded id."""
    con = sqlite3.connect(_user_db(root))
    try:
        kdec = {r[0]: r[1] for r in con.execute(
            'SELECT id, val FROM "_dict_address_versions_kind"')}
        out = []
        for kid, vby, kindid in con.execute(
                "SELECT kcdx_id, verified_by, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL AND last_verified_at_version IS NOT NULL "
                "AND rva IS NOT NULL AND valid_through IS NULL ORDER BY kcdx_id"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                out.append((kid, vby))
                if len(out) == n:
                    break
        return out
    finally:
        con.close()


def _db_verified_by(root, kcdx_id):
    con = sqlite3.connect(_user_db(root))
    try:
        row = con.execute(
            "SELECT verified_by FROM address_versions WHERE kcdx_id = ? "
            "AND valid_through IS NULL", (kcdx_id,)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _batch_row(kid, vby):
    return {"kcdx_id": kid, "valid_from_version": GVT,
            "edits": {"verified_by": vby, "verified_date": "2099-12-31",
                      "last_verified_at_version": GVT}}


def _batch_body(rows):
    return {"author_name": AUTHOR_NAME, "author_email": AUTHOR_EMAIL,
            "version_tag": GVT, "rows": rows}


# --- the canonical acceptance signal (.claude/rules/acceptance-signal.md) -----------
def _emit_signal(results):
    """One ACCEPT-RESULT per item, one ACCEPT-SUITE aggregate last. The agent greps the
    fixed tokens; the user reads nothing."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


# ============================================================================
# SUCCESS -- a batch of N UPDATEs lands: every row holds its new value, ONE git commit
# (exact-path, subset of the db-export CSVs), request-context author, pushed.
# ============================================================================
def test_confirm_batch_commits_all_rows_and_pushes(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    parent = _head_sha(root)
    bare_before = _head_sha(bare)

    picked = _pick_trio_rows(root, 3)
    assert len(picked) == 3, "fixture lacks 3 editable function-trio rows"
    rows = [_batch_row(kid, f"BatchSigner_{i}") for i, (kid, _vby) in enumerate(picked)]

    resp = client.post("/confirm/batch", json=_batch_body(rows))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "saved", body
    assert body["version"] == GVT, body
    assert body["pushed"] is True, body

    # A NEW git commit landed; exact-path staging (subset of the 3 db-export CSVs).
    head = _head_sha(root)
    assert head != parent, "the batch Confirm must create a commit"
    changed = _changed_files_in_head(root)
    assert set(changed).issubset(set(STAGED_REL_PATHS)), \
        f"the batch commit staged files OUTSIDE the exact db-export set: {changed}"
    assert "data/db-export/address_versions_seed.csv" in changed, changed
    assert not any(p.endswith(".sqlite") for p in changed), \
        f"a .sqlite was staged -- the DB is the local originator (D1/D20): {changed}"

    # EVERY row holds its OWN distinct new value (not one value smeared across all).
    for i, (kid, _vby) in enumerate(picked):
        assert _db_verified_by(root, kid) == f"BatchSigner_{i}", \
            f"batch row kid={kid} did not land its value in the DB"

    # The request-context identity authored the batch commit (D17).
    assert _head_author(root) == (AUTHOR_NAME, AUTHOR_EMAIL), _head_author(root)
    # The push reached the throwaway bare remote.
    assert _head_sha(bare) != bare_before, "the batch did not push"
    assert _head_sha(bare) == head, "the bare remote HEAD != the committed HEAD"


# ============================================================================
# ALL-OR-NOTHING (the D21/D32 load-bearing case) -- a batch whose ONE row is invalid
# returns FAILED and rolls EVERYTHING back: the DB + db-export byte-identical INCLUDING
# sqlite_sequence, NO new commit, the remote did not advance, the EARLIER valid rows are
# NOT committed. This FAILS a per-row-commit fake-batch (which would land rows 0..K-1).
# ============================================================================
def test_confirm_batch_one_invalid_row_rolls_back_whole_batch(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    state_before = _state_hash(root)
    seq_before = _sqlite_sequences(root)
    head_before = _head_sha(root)
    bare_before = _head_sha(bare)

    picked = _pick_trio_rows(root, 3)
    assert len(picked) == 3, "fixture lacks 3 editable function-trio rows"
    # Rows 0,1 are valid; row 2 carries an out-of-enum evidence_kind the shared
    # validator HARD-rejects over the whole prospective state. A per-row-commit impl
    # would commit 0,1 then die on 2 -> the DB would change. All-or-nothing must not.
    pre_vbys = {kid: _db_verified_by(root, kid) for kid, _vby in picked}
    rows = [_batch_row(picked[0][0], "BatchValid0"),
            _batch_row(picked[1][0], "BatchValid1"),
            _batch_row(picked[2][0], "BatchBad2")]
    rows[2]["edits"]["evidence_kind"] = "not_a_real_tier"

    resp = client.post("/confirm/batch", json=_batch_body(rows))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "failed", body
    assert body["committed"] is False, body
    assert body["retry"] is False, body
    assert body["detail"], body

    # ALL-OR-NOTHING: nothing landed -- DB + db-export byte-identical incl. sqlite_sequence,
    # no commit, no push. The earlier VALID rows did NOT commit (the fake-batch killer).
    assert _state_hash(root) == state_before, \
        "a batch with one invalid row CHANGED the DB/db-export -- the valid rows " \
        "committed (NOT all-or-nothing; a per-row-commit fake-batch)"
    assert _sqlite_sequences(root) == seq_before, "sqlite_sequence changed on a failed batch"
    assert _head_sha(root) == head_before, "a commit landed on a failed batch"
    assert _head_sha(bare) == bare_before, "the remote advanced on a failed batch"
    # Explicit per-row proof the valid rows did NOT take their new value.
    for kid, _vby in picked:
        assert _db_verified_by(root, kid) == pre_vbys[kid], \
            f"a valid row kid={kid} committed despite the invalid sibling (NOT atomic)"


# ============================================================================
# The canonical-signal emitter -- runs the two load-bearing assertions over the real app
# and emits ACCEPT-RESULT / ACCEPT-SUITE so the agent reads ONE verdict line, not a log.
# (The standalone tests above are the pytest gate; this maps them to the acceptance grammar.)
# ============================================================================
def test_confirm_batch_acceptance_signal(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    results = []

    # ACCEPT item 1: a 2-row batch lands BOTH rows on ONE commit.
    picked = _pick_trio_rows(root, 2)
    if len(picked) < 2:
        pytest.skip("fixture lacks 2 editable function-trio rows")
    rows = [_batch_row(picked[0][0], "AcceptSigner0"),
            _batch_row(picked[1][0], "AcceptSigner1")]
    parent = _head_sha(root)
    r = client.post("/confirm/batch", json=_batch_body(rows)).json()
    ok1 = (r.get("status") == "saved"
           and _db_verified_by(root, picked[0][0]) == "AcceptSigner0"
           and _db_verified_by(root, picked[1][0]) == "AcceptSigner1"
           and _head_sha(root) != parent)
    results.append(("batch-all-rows-one-commit", ok1,
                    None if ok1 else f"status={r.get('status')} body={r}"))

    # ACCEPT item 2: a 1-bad-row batch rolls the WHOLE batch back (nothing lands).
    state_before = _state_hash(root)
    head_before = _head_sha(root)
    bad = [_batch_row(picked[0][0], "WouldLand"),
           _batch_row(picked[1][0], "WouldAlsoLand")]
    bad[1]["edits"]["evidence_kind"] = "not_a_real_tier"
    r2 = client.post("/confirm/batch", json=_batch_body(bad)).json()
    ok2 = (r2.get("status") == "failed"
           and _state_hash(root) == state_before
           and _head_sha(root) == head_before)
    results.append(("batch-all-or-nothing-rollback", ok2,
                    None if ok2 else f"status={r2.get('status')}; state changed"))

    _emit_signal(results)
    failures = [(aid, d) for aid, ok, d in results if not ok]
    assert not failures, "batch confirm acceptance drift:\n  " + \
        "\n  ".join(f"{aid}: {d}" for aid, d in failures)
