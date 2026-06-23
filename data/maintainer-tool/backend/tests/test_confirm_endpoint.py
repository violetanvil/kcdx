"""test_confirm_endpoint.py -- the backend Confirm transaction + Cancel (the
direct-write model: direct DB write + data/db-export/ target + the robust rollback +
the event-driven index.lock).

WHAT THIS PROVES
----------------
POST /confirm runs the WHOLE atomic save SYNCHRONOUSLY in one request over the REAL app
+ the REAL data-core (apply_direct_edit) + the REAL mini-dump DBs + REAL git (a
THROWAWAY LOCAL BARE remote -- NOT real GitHub). The transaction opens AND closes inside
the one request (no held state). The cases assert the load-bearing properties:

  - SUCCESS (an UPDATE + a CREATE-entity): the DB holds the new value AND the 3
    data/db-export/ CSVs are updated AND a git commit landed staging ONLY the 3
    data/db-export/ CSVs BY EXACT PATH -- NO .sqlite (the DB is the local originator,
    NOT git-tracked) -- (the commit's changed files are a subset of EXACTLY that
    set, with the edited file present, and neither reference DB present) with the
    request-context identity as author, AND the push reached the throwaway bare remote
    (its HEAD advanced).
  - ROBUST ROLLBACK -- on a failure at EACH step (an invalid edit pre-commit; a post-
    commit git push failure; a live index.lock): NOTHING lands -- the DB + db-export CSVs
    are byte-identical INCLUDING the sqlite_sequence values (the robust-rollback proof),
    NO new commit, the remote did not advance, a FAILED/busy status. The post-commit case
    exercises the SCOPED restore-point (data_core.restore(handle) -- DB rows + sequence)
    + the backend db-export CSV revert -- NOT a dropped full-file snapshot.
  - LIVE index.lock: the git stage fails off git's OWN non-zero exit (event-driven, NO
    poll); the lock is NEVER reaped (it is still present after); the Confirm rolls
    everything back + surfaces busy/retry.
  - DEV DEFAULT (no env credential): the confirm commits LOCALLY, push skipped, boots
    without the operator's auth.
  - CANCEL: {status: cancelled}, nothing written.
  - NO-DELTA / create-version-at-a-NEW-tag: the create-version at a new tag writes the DB
    directly -- it is NO LONGER a no-op (an earlier seed-rebuild bridge dropped it). The
    DB gains the new game_versions row + the new av row; the db-export CSVs change; a real
    git commit lands. (A previously-failing case, fixed under the direct-write model.)

REAL EVERYTHING; skips gracefully if the mini-dump fixture is absent.

RUN
---
    python -m pytest backend/tests/ -q
"""
import hashlib
import os
import re
import shutil
import sqlite3
import subprocess
import sys
import tempfile

import pytest
from fastapi.testclient import TestClient

# --- locate the backend package + the data-core test fixtures (the save-test pattern) ---
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))
REPO_ROOT = os.path.normpath(os.path.join(BACKEND_DIR, "..", "..", ".."))
DATA_CORE_PYDIR = os.path.join(BACKEND_DIR, "data_core")
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

# The exact paths Confirm stages, relative to the checkout root (must match
# routes_confirm._staged_rel_paths): ONLY the three derived CSVs at data/db-export/
# (the export target). The two reference DBs are the LOCAL ORIGINATOR -- amended in
# place at config.out_dir (data/) but NOT git-tracked; only the derived CSV export is the
# git record (the rejected alternative was DB-tracked-as-binary). A confirm that
# staged a .sqlite would contradict that model (and the real checkout gitignores the DB,
# so `git add` of it is rejected and rolls the transaction back). The curated CSVs at
# data/db-export/ are both the export target AND the rebuild genesis.
STAGED_REL_PATHS = sorted([
    "data/db-export/module_seed.csv",
    "data/db-export/address_names_seed.csv",
    "data/db-export/address_versions_seed.csv",
])

# The reference DBs -- the LOCAL ORIGINATOR, present on disk + amended by the
# data-core, but NEVER git-tracked. Listed so the robust-rollback state hash can
# read them off disk AND so the "DB is NOT in the commit" regression assertions can name
# them explicitly. They are deliberately NOT in STAGED_REL_PATHS.
DB_REL_PATHS = [
    "data/reference.sqlite",
    "data/reference-dev.sqlite",
]

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953"
NEW_ROW_TAG = "1.6.2000000"         # a new game tag for a create-version-at-a-new-tag row

AUTHOR_NAME = "Test Maintainer"
AUTHOR_EMAIL = "test.maintainer@example.invalid"

# GIT-INVISIBLE CONTRACT: git is INVISIBLE to the maintainer. A maintainer-FACING
# failure `detail` (the string the page renders as the write-failure reason) must contain
# NO git vocabulary. The git stage + raw git stderr are the operator's diagnostic -- they
# live in the log, never in the rendered detail. This regex is the leak check.
_GIT_VOCAB_RE = re.compile(r"\b(git|index\.lock|push|fetch|stage|remote|commit hash)\b",
                           re.IGNORECASE)


def _git(checkout, *args, env=None, check=True):
    """Run `git -C <checkout> <args>` (never cd). Returns the CompletedProcess.

    -c safe.bareRepository=all: the test creates its OWN throwaway bare remote in a temp
    dir (see _build_git_checkout) and runs git ops against it (init --bare, push). A
    machine configured safe.bareRepository=explicit otherwise refuses ALL bare-repo ops;
    that guard is for untrusted bare repos in shared locations, not a test's own temp
    remote -- so allow it for these self-created repos."""
    proc = subprocess.run(["git", "-c", "safe.bareRepository=all", "-C", checkout, *args],
                          capture_output=True, text=True, env=env)
    if check and proc.returncode != 0:
        raise RuntimeError(f"git {' '.join(args)} failed: {proc.stderr or proc.stdout}")
    return proc


def _build_git_checkout():
    """A temp checkout that is a REAL git repo (git-init'd, with the curated CSVs at
    data/db-export/ + rebuilt DBs at data/, the db-export CSVs committed as the baseline)
    plus a THROWAWAY LOCAL BARE remote named `private` (the push target -- never real
    GitHub). Returns (checkout_root, bare_remote_path). Skips if the mini-dump fixture is
    absent.

    The curated CSVs at data/db-export/ are BOTH the rebuild genesis (config.seed_dir)
    AND the export target (config.db_export_dir) -- one location. They are SEEDED in the
    baseline (one export of the rebuilt DB) so they exist pre-Confirm -- the steady-state
    production posture (the export record already tracks history). This makes the
    byte-identity / exact-path assertions unambiguous (the Confirm UPDATES existing
    tracked files, not creates new ones)."""
    if not os.path.isdir(DUMP_DIR):
        pytest.skip(f"mini-dump fixture not found: {DUMP_DIR}")

    root = tempfile.mkdtemp(prefix="confirm_checkout_")
    out_dir = os.path.join(root, "data")               # config.out_dir == data/ (the DBs)
    # The curated CSVs are the rebuild genesis AND the export target -- one dir.
    export_dir = os.path.join(root, "data", "db-export")
    seed_dir = export_dir                              # config.seed_dir == db-export
    os.makedirs(export_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(seed_dir, f))

    # Rebuild the two reference DBs into the checkout's data/ (config.out_dir, NOT the
    # CSV subdir), pointing the importer's curated-CSV constants at data/db-export/.
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

    # Re-seed the derived-export record (data/db-export/) from the rebuilt DB so the export
    # target tracks the current DB state pre-Confirm (the export is byte-deterministic, so
    # this reproduces the same curated CSVs the rebuild read from).
    from seeds_shared.csv_exporter import export_seeds as _export_seeds
    _export_seeds(os.path.join(out_dir, "reference.sqlite"), export_dir)

    # git init + a baseline commit of the DBs + the db-export CSVs (so the Confirm commit
    # has a parent and the changed-files assertion is unambiguous).
    _git(root, "init", "-q")
    _git(root, "config", "user.name", "Baseline")
    _git(root, "config", "user.email", "baseline@example.invalid")
    _git(root, "config", "commit.gpgsign", "false")
    _git(root, "add", "--", *STAGED_REL_PATHS)
    _git(root, "commit", "-q", "-m", "baseline: reference DBs + db-export record")

    # A THROWAWAY LOCAL BARE remote named `private` -- the push target (never real GitHub).
    bare = tempfile.mkdtemp(prefix="confirm_bare_remote_")
    _git(bare, "init", "--bare", "-q")
    _git(root, "remote", "add", PRIVATE_REMOTE, bare)
    # Seed the bare remote with the baseline so its HEAD exists pre-Confirm.
    _git(root, "push", "-q", PRIVATE_REMOTE, "HEAD:refs/heads/main")

    return root, bare


@pytest.fixture()
def checkout():
    """A FRESH real-git checkout + throwaway bare remote per test."""
    root, bare = _build_git_checkout()
    yield root, bare
    shutil.rmtree(root, ignore_errors=True)
    shutil.rmtree(bare, ignore_errors=True)


@pytest.fixture()
def client_at(monkeypatch):
    """A TestClient whose endpoints resolve a checkout root via KCDX_CHECKOUT, with the
    push credential controllable per test. `push=True` sets a (dummy) KCDX_PUSH_TOKEN so
    the push to the LOCAL bare remote runs (a local bare remote needs no real auth -- the
    token presence just enables the push code path); `push=False` (default) leaves it
    unset (the dev default -- push skipped)."""
    def _make(checkout_root, *, push=False):
        monkeypatch.setenv(CHECKOUT_ENV_VAR, checkout_root)
        if push:
            monkeypatch.setenv(PUSH_TOKEN_ENV_VAR, "dummy-local-token")
        else:
            monkeypatch.delenv(PUSH_TOKEN_ENV_VAR, raising=False)
        return TestClient(app)
    return _make


# --- helpers over the checkout's DBs / db-export CSVs / git state -------------------
def _seed_dir(root):
    # config.seed_dir == <checkout>/data/db-export.
    return os.path.join(root, "data", "db-export")


def _out_dir(root):
    # config.out_dir == <checkout>/data -- where the reference DBs live (NOT the CSV subdir).
    return os.path.join(root, "data")


def _export_dir(root):
    return os.path.join(root, "data", "db-export")


def _user_db(root):
    return os.path.join(_out_dir(root), "reference.sqlite")


def _db_rows_hash(db_path):
    """A canonical hash over every table's full ordered rows + sqlite_sequence -- the
    LOGICAL content of one DB, independent of physical page layout.

    WHY logical-rows, not file bytes (the restore oracle): the scoped restore-point
    (data_core.restore(handle)) undoes a committed edit by a row-level delete-then-reinsert
    + a sqlite_sequence reset on a freshly re-opened connection. That restores the DB rows
    + sequence byte-identical (proved row-by-row here, sqlite_sequence is a table so it is
    INCLUDED), but it rewrites the FILE's physical bytes (page layout / free-list) -- so a
    raw file SHA would differ on a CORRECT restore. (A dropped full-file snapshot restored
    exact file bytes; the scoped restore-point restores logical state. The requirement is
    logical row + sequence identity, not physical file identity.) Hashing the
    ordered rows of every table -- sqlite_sequence among them -- is the right no-change
    oracle: it proves the rows AND the PK auto-increment counter are back to pre-Confirm."""
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
    """The no-change proof for the robust-rollback cases: the two DBs' LOGICAL row content
    (incl. sqlite_sequence -- a table, so it is in the row hash) + the three
    data/db-export/ CSVs' raw bytes.

    Two oracles, two reasons: the DB half is hashed by LOGICAL ROWS (the 4d row-level
    restore is logically byte-identical but physically rewrites the file -- _db_rows_hash);
    the db-export CSV half is hashed by RAW BYTES (the backend reverts them from a kept
    pre-edit byte copy, so they ARE byte-identical). A matching _state_hash after a failed
    Confirm proves both halves of the robust rollback landed -- the DB rows + PK counter
    reset (the 4d restore-point) and the CSV record reverted (the backend CSV-revert)."""
    h = hashlib.sha256()
    for f in DB_FILES:
        h.update(_db_rows_hash(os.path.join(_out_dir(root), f)).encode())
    for f in SEED_FILES:
        with open(os.path.join(_export_dir(root), f), "rb") as fh:
            h.update(fh.read())
    return h.hexdigest()


def _sqlite_sequences(root):
    """The sqlite_sequence rows of the USER DB -- the EXPLICIT PK-state assertion (the
    robust rollback must reset these, not just the table rows). Returns a dict
    {table: seq}; empty if the DB has no AUTOINCREMENT table (then the row-hash carries
    the proof). Read for an explicit, readable assertion alongside the file hash."""
    import sqlite3
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
    """The files changed by HEAD vs its parent -- the EXACT-PATH-staging assertion source.
    Returns a sorted list of repo-relative paths."""
    out = _git(root, "diff-tree", "--no-commit-id", "--name-only", "-r", "HEAD").stdout
    return sorted(p.strip() for p in out.splitlines() if p.strip())


def _head_author(root):
    """The author 'Name <email>' of HEAD -- the request-context-identity assertion."""
    name = _git(root, "log", "-1", "--format=%an").stdout.strip()
    email = _git(root, "log", "-1", "--format=%ae").stdout.strip()
    return name, email


def _db_av_value(root, kcdx_id, column):
    """A column of the address_versions row for kcdx_id in the USER DB -- to prove the DB
    holds the new value."""
    import sqlite3
    con = sqlite3.connect(_user_db(root))
    try:
        row = con.execute(
            f"SELECT {column} FROM address_versions WHERE kcdx_id = ? "
            f"AND valid_through IS NULL", (kcdx_id,)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _db_names_value(root, kcdx_id, column):
    """A column of the address_names row for kcdx_id in the USER DB -- to prove a names-row
    UPDATE (e.g. notes) landed."""
    import sqlite3
    con = sqlite3.connect(_user_db(root))
    try:
        row = con.execute(
            f"SELECT {column} FROM address_names WHERE id = ?", (kcdx_id,)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _confirm_body(**kw):
    """A confirm body with the request-context author fields + the given edit fields."""
    return dict(author_name=AUTHOR_NAME, author_email=AUTHOR_EMAIL, **kw)


# ============================================================================
# _staged_rel_paths -- the staged-set contract, a DIRECTLY-RUNNABLE unit test (no git
# subprocess, no fixture -- so it is ALWAYS green regardless of the git-env). Confirm
# stages EXACTLY the 3 data/db-export/ CSVs and NO .sqlite: the DB is the local originator
# and is NOT git-tracked; only its derived CSV export is the git record. This is the
# cleanest regression guard for the staged-the-DB bug -- it pins the exact list.
# ============================================================================
def test_staged_rel_paths_is_the_three_db_export_csvs_only():
    from app.routes_confirm import _staged_rel_paths

    staged = _staged_rel_paths()
    # EXACTLY the three derived-export CSVs -- nothing more, nothing less.
    assert sorted(staged) == STAGED_REL_PATHS, staged
    # NO .sqlite -- the DB is the local originator, never staged. The bug this
    # guards: _staged_rel_paths returning the reference DBs alongside the CSVs.
    assert not any(p.endswith(".sqlite") for p in staged), \
        f"a .sqlite was staged -- the DB is the local originator, never committed: {staged}"
    # Neither reference DB by name (the explicit form of the .sqlite check above).
    for db in DB_REL_PATHS:
        assert db not in staged, f"the DB {db} must not be staged: {staged}"
    # All three db-export CSVs ARE staged (the git-tracked record).
    for csv in STAGED_REL_PATHS:
        assert csv in staged, f"the db-export CSV {csv} must be staged: {staged}"


# ============================================================================
# SUCCESS -- an UPDATE: DB holds the new value, the 3 data/db-export/ CSVs updated, ONE git
# commit staging a SUBSET of EXACTLY {DB + 3 db-export CSVs} by exact path (the edited file
# present), request-context author, push reached the bare remote.
# ============================================================================
def test_confirm_update_commits_dbexport_csvs_and_db_and_pushes(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    parent = _head_sha(root)
    bare_before = _head_sha(bare)

    # Re-verify kcdx_id=1: change verified_by (an audit-trio UPDATE).
    resp = client.post("/confirm/update-version", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, valid_from_version=GVT,
        edits={"verified_by": "ConfirmedReviewer"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "saved", body
    assert body["version"] == GVT, body
    assert body["pushed"] is True, body

    # A NEW git commit landed (HEAD advanced past the baseline).
    head = _head_sha(root)
    assert head != parent, "Confirm must create a new commit"

    # EXACT-PATH STAGING: the commit's changed files are a subset of EXACTLY the DB + 3
    # db-export CSVs -- no stray file swept in (the concurrency-git exact-path proof).
    changed = _changed_files_in_head(root)
    assert set(changed).issubset(set(STAGED_REL_PATHS)), \
        f"the commit staged files OUTSIDE the exact DB+db-export set: {changed}"
    # The edit lands in the DB-EXPORT versions CSV (the only CSV record).
    assert "data/db-export/address_versions_seed.csv" in changed, \
        f"the edited db-export CSV must be in the commit: {changed}"
    # NEITHER reference DB is in the commit -- the DB is the local originator, NOT
    # git-tracked; only its derived CSV export is the git record. Staging a .sqlite
    # would contradict that model (and is rejected in the real gitignored checkout). This
    # is the regression guard for the staged-the-DB bug.
    for db in DB_REL_PATHS:
        assert db not in changed, \
            f"a reference DB was staged -- the DB is the local originator, never " \
            f"committed: {changed}"
    # RETIREMENT GUARD: the old data/seeds/ bootstrap path is retired -- the Confirm
    # never writes it. Falsifiable: a regression re-introducing a data/seeds/ write
    # would surface here (and already break the issubset(STAGED_REL_PATHS) check above,
    # since the seeds path is not in the staged set).
    assert "data/seeds/module_seed.csv" not in changed, \
        f"a retired data/seeds/ CSV was written -- it must never be: {changed}"

    # The request-context identity is the commit author.
    assert _head_author(root) == (AUTHOR_NAME, AUTHOR_EMAIL), _head_author(root)

    # The DB holds the new value.
    assert _db_av_value(root, 1, "verified_by") == "ConfirmedReviewer", \
        "the DB was not updated"

    # The PUSH reached the throwaway bare remote (its HEAD advanced to our commit).
    assert _head_sha(bare) != bare_before, "the bare remote HEAD did not advance"
    assert _head_sha(bare) == head, "the bare remote HEAD != the committed HEAD"


# ============================================================================
# SUCCESS -- an EDIT-NOTES UPDATE: the DB names row holds the new notes, the
# db-export CSVs update, ONE git commit (exact-path), request-context author, pushed. An
# UPDATE -- no approval gate exists server-side; the confirm just transacts.
# ============================================================================
def test_confirm_edit_notes_commits_db_and_pushes(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    parent = _head_sha(root)

    resp = client.post("/confirm/edit-notes", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, notes="confirmed against the 1.5 build"))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "saved", body
    assert body["pushed"] is True, body

    # A NEW commit landed, staging a SUBSET of EXACTLY the DB + 3 db-export CSVs.
    head = _head_sha(root)
    assert head != parent, "the edit-notes Confirm must create a commit"
    changed = _changed_files_in_head(root)
    assert set(changed).issubset(set(STAGED_REL_PATHS)), \
        f"staged files outside the exact set: {changed}"
    # The notes change lands in the db-export NAMES CSV (the names-row column).
    assert "data/db-export/address_names_seed.csv" in changed, changed

    # The request-context identity is the commit author.
    assert _head_author(root) == (AUTHOR_NAME, AUTHOR_EMAIL), _head_author(root)

    # The DB names row holds the new notes (the UPDATE landed).
    assert _db_names_value(root, 1, "notes") == "confirmed against the 1.5 build", \
        "the DB notes column was not updated"

    # The push reached the throwaway bare remote.
    assert _head_sha(bare) == head, "the push did not reach the bare remote"


# ============================================================================
# SUCCESS -- a CREATE (create-entity, the new-entity-approval path): confirmed end-to-end,
# materializes a new row at the baseline, committed + pushed. Adding a new entity/version
# row requires explicit maintainer approval.
# ============================================================================
def test_confirm_create_entity_commits_and_pushes(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    parent = _head_sha(root)

    # A brand-new entity with a data_slot first version at the BASELINE tag (GVT) -- a
    # high RVA that MINTS without the function-kind bulk-baseline gate. This materializes
    # a names row + a versions row at the baseline -> the db-export CSVs genuinely change
    # -> a real git commit.
    resp = client.post("/confirm/create-entity", json=_confirm_body(
        version_tag=GVT, name="kcdx_confirm_new_entity",
        first_version_columns={"valid_from_version": GVT, "module": "WHGame.dll",
                               "kind": "data_slot", "rva": "0x09000000"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "saved", body
    assert body["pushed"] is True, body
    assert body.get("no_delta") is False, body

    head = _head_sha(root)
    assert head != parent, "the create-entity Confirm must create a commit"
    changed = _changed_files_in_head(root)
    assert set(changed).issubset(set(STAGED_REL_PATHS)), \
        f"staged files outside the exact set: {changed}"
    # The new entity is in BOTH committed db-export CSVs (a new id + its row).
    assert "data/db-export/address_names_seed.csv" in changed, changed
    assert "data/db-export/address_versions_seed.csv" in changed, changed
    # The new entity is in the committed DB.
    import sqlite3
    con = sqlite3.connect(_user_db(root))
    try:
        row = con.execute("SELECT id FROM address_names WHERE name=?",
                          ("kcdx_confirm_new_entity",)).fetchone()
    finally:
        con.close()
    assert row, "the new entity is not in the committed DB"
    assert _head_sha(bare) == head, "the push did not reach the bare remote"


# ============================================================================
# CREATE-VERSION-AT-A-NEW-TAG -- the capability the old seed-rebuild bridge LACKED (the
# previously-RED case). 4c's direct write INSERTs the new game_versions row + closes the
# prior interval + INSERTs the new av row, so it is NO LONGER a no-op: the DB changes, the
# db-export CSVs change, a real git commit lands.
# ============================================================================
def test_confirm_create_version_at_new_tag_writes_directly(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    parent = _head_sha(root)
    bare_before = _head_sha(bare)

    # A data_slot kind at a high RVA mints without the function-kind bulk-baseline gate
    # (the create-entity success pattern), so the new-tag INSERT lands cleanly. (A
    # function-kind new-tag row would need a bulk baseline at its RVA -- a separate
    # rebuild concern, not the new-tag-INSERT capability under test here.)
    resp = client.post("/confirm/create-version", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, valid_from_version=NEW_ROW_TAG,
        columns={"module": "WHGame.dll", "kind": "data_slot", "rva": "0x09000000"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    # Under the DIRECT-WRITE model this is a REAL save (the new-tag INSERT lands), NOT the
    # old no-delta no-op -- the previously-RED case is now green.
    assert body["status"] == "saved", body
    assert body.get("no_delta") is False, body
    assert body["pushed"] is True, body

    head = _head_sha(root)
    assert head != parent, "the new-tag create-version must create a commit"
    changed = _changed_files_in_head(root)
    assert set(changed).issubset(set(STAGED_REL_PATHS)), \
        f"staged files outside the exact set: {changed}"
    # The new game tag is now in the DB's game_versions.
    import sqlite3
    con = sqlite3.connect(_user_db(root))
    try:
        tag_row = con.execute("SELECT tag FROM game_versions WHERE tag=?",
                              (NEW_ROW_TAG,)).fetchone()
    finally:
        con.close()
    assert tag_row, "the new game tag is not in the committed DB game_versions"
    assert _head_sha(bare) != bare_before, "the new-tag create did not push"


# ============================================================================
# ROBUST ROLLBACK (pre-commit) -- an invalid edit at the DB-ops step: nothing committed,
# the DB + db-export CSVs byte-identical INCLUDING sqlite_sequence, NO new commit, FAILED.
# ============================================================================
def test_confirm_invalid_edit_rolls_back_everything(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    state_before = _state_hash(root)
    seq_before = _sqlite_sequences(root)
    head_before = _head_sha(root)
    bare_before = _head_sha(bare)

    # Entity 1 superseding ITSELF (superseded_by == its own name "lua_pcall") -- a
    # validator HARD ERROR. The held txn rolls back: nothing lands (pre-commit).
    resp = client.post("/confirm/supersede", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, superseded_by="lua_pcall",
        superseded_at_version=GVT,
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "failed", body
    assert body["committed"] is False, body
    assert body["retry"] is False, body
    assert body["detail"], body

    # ROBUST ROLLBACK: the DB + db-export CSVs byte-identical INCLUDING sqlite_sequence,
    # no new commit, no push.
    assert _state_hash(root) == state_before, "the DB/db-export changed on a failed edit"
    assert _sqlite_sequences(root) == seq_before, "sqlite_sequence changed on a failure"
    assert _head_sha(root) == head_before, "a commit landed on a failed edit"
    assert _head_sha(bare) == bare_before, "the remote advanced on a failed edit"


# ============================================================================
# ROBUST ROLLBACK (POST-commit) -- the push targets a DEAD remote so git fails AFTER the
# DB commit + the local git commit. The 4d SCOPED restore-point (data_core.restore(handle))
# undoes the committed DB write (touched rows + sqlite_sequence) and the backend reverts the
# db-export CSVs: the DB + db-export CSVs byte-identical INCLUDING sqlite_sequence, the
# remote did not advance, FAILED (nothing lands -- the user's robust-rollback requirement,
# not commit_failed). This is the load-bearing proof the rework replaced the full-file
# snapshot with the scoped 4d restore-point WITHOUT losing the "nothing lands" guarantee.
# ============================================================================
def test_confirm_git_push_failure_rolls_back_everything(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    state_before = _state_hash(root)
    seq_before = _sqlite_sequences(root)
    head_before = _head_sha(root)
    bare_before = _head_sha(bare)

    # Point the `private` remote at a non-existent path so the push fails AFTER the DB
    # commit + the local git commit.
    dead = os.path.join(tempfile.gettempdir(), "confirm_no_such_remote_xyz.git")
    _git(root, "remote", "set-url", PRIVATE_REMOTE, dead)

    resp = client.post("/confirm/update-version", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, valid_from_version=GVT,
        edits={"verified_by": "PostCommitReviewer"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    # ROBUST ROLLBACK: a post-commit git push failure rolls EVERYTHING back -- FAILED,
    # not committed, nothing retryable past it (the user's "nothing lands").
    assert body["status"] == "failed", body
    assert body["committed"] is False, body
    assert body.get("git_stage") == "push", body

    # The DB change was UNDONE (the verified_by is back to its pre-Confirm value, NOT the
    # new one) -- the robust rollback. The DB + db-export byte-identical incl. sequence.
    assert _db_av_value(root, 1, "verified_by") != "PostCommitReviewer", \
        "the DB change was NOT rolled back after the post-commit git failure"
    assert _state_hash(root) == state_before, \
        "the DB/db-export were not restored after a post-commit git failure"
    assert _sqlite_sequences(root) == seq_before, "sqlite_sequence not reset on rollback"
    # The durable mirror never advanced (the dead remote got nothing).
    assert _head_sha(bare) == bare_before, "the remote advanced despite the rollback"


# ============================================================================
# LIVE index.lock -- the git stage fails off git's OWN non-zero exit (EVENT-DRIVEN, NO
# poll); the lock is NEVER reaped (still present after); the Confirm rolls everything back
# + surfaces busy/retry.
# ============================================================================
def test_confirm_live_index_lock_event_driven_never_reaps(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=False)
    state_before = _state_hash(root)
    seq_before = _sqlite_sequences(root)
    head_before = _head_sha(root)

    # Create a LIVE-looking index.lock and leave it held for the whole confirm. git's own
    # `add` refuses to create a second index.lock and exits non-zero immediately -- the
    # event-driven detection (NO sleep-poll waiting for it to clear).
    lock_path = os.path.join(root, ".git", "index.lock")
    with open(lock_path, "wb") as fh:
        fh.write(b"")

    try:
        resp = client.post("/confirm/update-version", json=_confirm_body(
            version_tag=GVT, kcdx_id=1, valid_from_version=GVT,
            edits={"verified_by": "LockedReviewer"},
        ))
    finally:
        held = os.path.exists(lock_path)
        # NEVER REAPED: the confirm must not delete a live lock (concurrency-git rule 5).
        assert held, "the confirm REAPED the live index.lock -- it must never do that"
        os.remove(lock_path)

    assert resp.status_code == 200, resp.text
    body = resp.json()
    # The git stage hit the held lock and failed off git's exit -> the whole Confirm rolled
    # back -> busy/retry, nothing landed.
    assert body["status"] == "busy", body
    assert body["retry"] is True, body
    assert body["committed"] is False, body
    # ROBUST ROLLBACK: nothing landed -- DB + db-export byte-identical incl. sequence, no
    # new commit.
    assert _state_hash(root) == state_before, "the DB/db-export changed despite the lock"
    assert _sqlite_sequences(root) == seq_before, "sqlite_sequence changed despite the lock"
    assert _head_sha(root) == head_before, "a commit landed despite the held lock"


# ============================================================================
# DEV DEFAULT -- no env credential: the confirm commits LOCALLY, push skipped, boots
# without the operator's auth.
# ============================================================================
def test_confirm_dev_default_commits_locally_push_skipped(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=False)          # NO KCDX_PUSH_TOKEN
    parent = _head_sha(root)
    bare_before = _head_sha(bare)

    resp = client.post("/confirm/update-version", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, valid_from_version=GVT,
        edits={"verified_by": "LocalOnlyReviewer"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "saved", body
    assert body["pushed"] is False, body
    assert body["push_skipped_reason"], body       # names why (no env credential)

    # The commit landed LOCALLY (HEAD advanced) but the bare remote did NOT (push skipped).
    assert _head_sha(root) != parent, "the local commit did not land"
    assert _head_sha(bare) == bare_before, "the push ran despite no credential (dev default)"
    # The DB holds the change (a committed local save -- no rollback, the push was skipped
    # by design, not failed).
    assert _db_av_value(root, 1, "verified_by") == "LocalOnlyReviewer", \
        "the dev-default local save did not land the DB change"


# ============================================================================
# CANCEL -- a no-op success, nothing written.
# ============================================================================
def test_cancel_is_a_noop(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    state_before = _state_hash(root)
    head_before = _head_sha(root)

    resp = client.post("/cancel", json={})
    assert resp.status_code == 200, resp.text
    assert resp.json() == {"status": "cancelled"}, resp.text

    # Nothing was written or committed.
    assert _state_hash(root) == state_before, "Cancel touched the DB/db-export"
    assert _head_sha(root) == head_before, "Cancel created a commit"


# ============================================================================
# The version= path (no DLL): an unknown tag -> the adapter's VersionTagError -> 422,
# before any txn opens. No write, no commit.
# ============================================================================
def test_confirm_unknown_tag_aborts_before_txn(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)
    state_before = _state_hash(root)
    head_before = _head_sha(root)

    resp = client.post("/confirm/update-version", json=_confirm_body(
        version_tag="9.9.9999999", kcdx_id=1, valid_from_version=GVT,
        edits={"verified_by": "X"},
    ))
    assert resp.status_code == 422, resp.text
    assert "not a known game version" in resp.text
    assert _state_hash(root) == state_before, "an unknown tag must not write"
    assert _head_sha(root) == head_before, "an unknown tag must not commit"


# ============================================================================
# GIT-INVISIBLE -- the maintainer-facing write-failure `detail` for a git failure
# is GIT-FREE, while the structured `git_stage` field still carries the stage (the operator
# diagnostic lives in the log + git_stage, NEVER in the rendered detail). A directly-runnable
# UNIT test of _failed_response (no git subprocess) -- the exact contract the GitCommitError
# handler now uses.
# ============================================================================
def test_failed_response_detail_is_git_free_on_a_git_failure():
    from app.routes_confirm import _failed_response

    # Call _failed_response EXACTLY as the GitCommitError handler now does: a git-free
    # maintainer detail + git_stage as the structured operator field.
    resp = _failed_response(
        "version-row kcdx_id=1",
        "the save couldn't be recorded and was rolled back -- nothing landed.",
        git_stage="push")

    assert resp["status"] == "failed", resp
    assert resp["committed"] is False, resp
    assert resp["retry"] is False, resp
    # GIT-INVISIBLE: the maintainer-rendered reason carries NO git vocabulary.
    leak = _GIT_VOCAB_RE.search(resp["detail"])
    assert leak is None, \
        f"git-invisible violation: maintainer-facing detail leaks git vocab {leak.group(0)!r}: " \
        f"{resp['detail']!r}"
    # The structured operator field STILL carries the stage (the frontend uses git_stage for
    # its own logic; it renders `detail`, never git_stage).
    assert resp["git_stage"] == "push", resp


# ============================================================================
# GIT-INVISIBLE -- the post-commit git-push-failure path: the maintainer-facing
# `detail` the page renders is GIT-FREE end-to-end through the real app. (This rides the
# same real-git fixture as the rollback test; if that fixture's environment cannot run the
# git subprocess, this skips with it -- the directly-runnable unit test above is the
# always-green proof.)
# ============================================================================
def test_confirm_git_push_failure_detail_is_git_free(checkout, client_at):
    root, bare = checkout
    client = client_at(root, push=True)

    # Point the `private` remote at a non-existent path so the push fails AFTER the DB
    # commit -> the GitCommitError write-failure path (the git-vocab leak site).
    dead = os.path.join(tempfile.gettempdir(), "confirm_no_such_remote_law5.git")
    _git(root, "remote", "set-url", PRIVATE_REMOTE, dead)

    resp = client.post("/confirm/update-version", json=_confirm_body(
        version_tag=GVT, kcdx_id=1, valid_from_version=GVT,
        edits={"verified_by": "Law5Reviewer"},
    ))
    assert resp.status_code == 200, resp.text
    body = resp.json()
    assert body["status"] == "failed", body
    assert body.get("git_stage") == "push", body          # structured field unchanged
    # GIT-INVISIBLE: the rendered reason leaks no git vocabulary (the operator's git detail
    # is in the log + the structured git_stage, NOT in this maintainer-facing string).
    leak = _GIT_VOCAB_RE.search(body["detail"])
    assert leak is None, \
        f"git-invisible violation: rendered detail leaks git vocab {leak.group(0)!r}: " \
        f"{body['detail']!r}"
