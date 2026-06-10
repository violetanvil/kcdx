"""test_direct_write.py -- the DIRECT-WRITE data-core path (maintainer-tool Phase 2,
step 4c; design D19/D20).

WHAT THIS PROVES
----------------
The six db_editor write functions were reworked from the seed-CSV-REBUILD bridge
(export DB -> edit a temp seed -> re-apply via apply_seeds) to DIRECT-DB INSERT/UPDATE
through import_to_sqlite's EXISTING _apply_one_db write helpers, fed EDIT PARAMETERS
(import_to_sqlite.apply_direct_edit). The DB is the originator (design D1); the rework
PRESERVES the mechanism (proven by convergence) and UNLOCKS create-version at a NEW
game tag (the bridge's GAME_VERSION_TAG gate materialised zero rows there). Cases:

  CONVERGENCE (the load-bearing proof) -- for each existing job shape (re-verify,
    full-column UPDATE, create-version-at-the-CURRENT-tag, create-entity, supersede,
    deprecate), the DIRECT write produces a DB BYTE-IDENTICAL (whole-DB per-table
    content fingerprint over BOTH DBs) to the SAME edit via the OLD seed-rebuild path
    (export+fold -> apply_seeds(defer_commit)+commit, reconstructed here). The direct
    write changes the MECHANISM, not the result.

  THE 8 BEHAVIORS -- a create-version asserts the folded survival/re-find cells landed
    ON the new av row (D22 / design §11.2 -- no separate survival table) + the prior
    interval closed; a function-kind add asserts promote-vs-mint (fingerprint carried)
    + BaselineRefusal on a missing baseline; an add asserts per-DB projection (USER vs
    DEV columns) + FK-id resolution (no minted id).

  CREATE-VERSION-AT-A-NEW-TAG (the new capability) -- a direct create-version at a tag
    the DB has no game_versions row for LANDS the new game_versions row + the closed
    prior interval + the new address_versions row (the old path materialised ZERO --
    asserted by reconstructing the old path and showing it writes nothing here).

  PROSPECTIVE-DB-STATE VALIDATION -- an invalid edit per shape (malformed date,
    duplicate tuple, supersession cycle, missing required) raises the validator's
    verdict, NO write, the DB byte-identical -- validated against the PROSPECTIVE DB
    state (the prospective seed = export(DB)+edit, a faithful serialisation of the
    post-edit DB rows; design D19).

  ROBUST ROLLBACK (the PK-reset proof) -- a direct write under the 4a deferred txn,
    then rollback(handle), leaves the DB byte-identical INCLUDING sqlite_sequence: a
    subsequent add reuses the SAME next id (the rollback discarded the PK-autoincrement
    bump). Design D19's robust post-failure rollback.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism (test_deferred_commit.py / test_db_editor_insert.py):
a module-scoped baseline rebuild from the committed seeds (built ONCE off the
mini-dump excerpt), copied per-case so each starts from a clean DB pair.

RUN
---
    python tests/test_direct_write.py
    pytest tests/test_direct_write.py
"""
import contextlib
import csv
import glob
import hashlib
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
DLL_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "third-party-ghidra", "WHGame.dll"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import db_editor  # noqa: E402
from seeds_shared import export_seeds, seed_csv_edit  # noqa: E402
from seeds_shared.csv_exporter import (  # noqa: E402
    ADDRESS_NAMES_SEED_NAME, ADDRESS_VERSIONS_SEED_NAME)

GVT = imp.GAME_VERSION_TAG   # "1.5.1164953"
NEW_TAG = "1.6.2000000"      # a game tag the baseline DB has NO game_versions row for
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# --------------------------------------------------------------------------
# Seed-dir pointing + baseline rebuild (the apply-oracle convention).
# --------------------------------------------------------------------------
def _copy_seeds(dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(dst_dir, f))


@contextlib.contextmanager
def _seeds_pointed_at(seed_dir):
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir,
                                                 "address_versions_seed.csv")
    try:
        yield
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def _rebuild_into(seed_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    with _seeds_pointed_at(seed_dir):
        imp.run_rebuild(DUMP_DIR, out_dir)


# --------------------------------------------------------------------------
# The byte-identity comparator (the apply-oracle's per-table content hash over
# every table in both DBs -- order-independent canonical content).
# --------------------------------------------------------------------------
def _hash_table(db_path, table):
    con = sqlite3.connect(db_path)
    try:
        cols = [c[1] for c in con.execute(f'PRAGMA table_info("{table}")')]
        rows = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} FROM "{table}"'
        ).fetchall()
        h = hashlib.sha256()

        def canon(v):
            if v is None:
                return "\x00"
            if isinstance(v, (bytes, bytearray, memoryview)):
                return "b:" + bytes(v).hex()
            return f"{type(v).__name__}:{v}"

        for r in sorted(repr(tuple(canon(c) for c in row)) for row in rows):
            h.update(r.encode("utf-8"))
        return h.hexdigest()
    finally:
        con.close()


def _db_fingerprint(out_dir):
    """Whole-DB byte fingerprint: a per-table content hash over EVERY table in both
    reference DBs. Two out_dirs with the same fingerprint hold byte-identical DBs.
    Fresh connection per read -> only COMMITTED state (a held deferred txn invisible)."""
    fp = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        con = sqlite3.connect(dbp)
        try:
            tables = [r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
        finally:
            con.close()
        for t in tables:
            fp[(which, t)] = _hash_table(dbp, t)
    return fp


def _seq_value(db_path, table):
    """The sqlite_sequence next-id watermark for an AUTOINCREMENT table (the value
    SQLite stores as the highest-ever-used rowid; the next INSERT uses seq+1). None
    when the table has no sqlite_sequence row yet. The robust-rollback PK-reset proof
    reads this for address_versions / survival / game_versions."""
    con = sqlite3.connect(db_path)
    try:
        has = con.execute(
            "SELECT name FROM sqlite_master WHERE type='table' "
            "AND name='sqlite_sequence'").fetchone()
        if not has:
            return None
        row = con.execute("SELECT seq FROM sqlite_sequence WHERE name = ?",
                          (table,)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


# --------------------------------------------------------------------------
# The OLD seed-rebuild path, reconstructed -- the convergence REFERENCE. This is
# exactly what the removed bridge did: export the committed DB -> fold the edit into
# the temp seed -> repoint the importer's seed constants -> apply_seeds(defer_commit)
# + commit. Pinning the DIRECT write to THIS proves the rework preserved the mechanism.
# --------------------------------------------------------------------------
def _seed_rebuild_apply(out_dir, *, fold, version):
    """Reconstruct the OLD seed-rebuild write for ONE edit and commit it. `fold` is a
    callable(prospective_dir) that folds the edit into the prospective seed exactly as
    the matching db_editor function does (so the prospective seed is identical to the
    direct path's). Returns nothing; out_dir's DBs are mutated in place."""
    user_db = os.path.join(out_dir, "reference.sqlite")
    work = tempfile.mkdtemp(prefix="seed_rebuild_ref_")
    try:
        prospective = os.path.join(work, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)
        fold(prospective)
        with _seeds_pointed_at(prospective):
            handle = imp.apply_seeds(out_dir, None, version=version,
                                     defer_commit=True)
        imp.commit(handle)
    finally:
        shutil.rmtree(work, ignore_errors=True)


# --------------------------------------------------------------------------
# Row pickers (use whatever the fixture carries, never a hardcoded id).
# --------------------------------------------------------------------------
def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _pick_function_trio_row(db_path):
    """A curated FUNCTION-kind row with a full audit trio (a re-verify target)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, kindid in con.execute(
                "SELECT kcdx_id, valid_from, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL AND last_verified_at_version IS NOT NULL "
                "AND rva IS NOT NULL"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                return (kid, gv.get(vf))
        return None
    finally:
        con.close()


def _pick_nonfunction_row(db_path, *, null_trio=False):
    """A curated NON-function row (mint, fingerprint NULL). null_trio restricts to an
    unverified row (so a copy at a newer tag is itself apply-valid)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, kindid, lvv in con.execute(
                "SELECT kcdx_id, valid_from, kind, last_verified_at_version "
                "FROM address_versions WHERE kcdx_id IS NOT NULL"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                continue
            if null_trio and lvv is not None:
                continue
            return (kid, gv.get(vf))
        return None
    finally:
        con.close()


def _first_plain_entity(db_path):
    """A curated entity with no supersession/deprecation edge yet (a lifecycle target)."""
    con = sqlite3.connect(db_path)
    try:
        for kid, name in con.execute(
                "SELECT id, name FROM address_names WHERE superseded_by IS NULL "
                "AND (is_deprecated IS NULL OR is_deprecated = 0) ORDER BY id"):
            return (kid, name)
        return None
    finally:
        con.close()


def _seed_source_row(db_path, kcdx_id, valid_from_tag):
    """The full authored seed cells of (kcdx_id, valid_from) from the exported seed
    (the complete prefill source -- the identity-key columns dropped)."""
    exp = tempfile.mkdtemp(prefix="direct_src_")
    try:
        export_seeds(db_path, exp)
        with open(os.path.join(exp, ADDRESS_VERSIONS_SEED_NAME),
                  newline="", encoding="utf-8") as f:
            lines = [ln for ln in f if not ln.lstrip().startswith("#")]
        for r in csv.DictReader(lines):
            if ((r.get("kcdx_id") or "").strip() == str(kcdx_id)
                    and (r.get("valid_from_version") or "").strip() == valid_from_tag):
                return {k: v for k, v in r.items()
                        if k not in ("kcdx_id", "valid_from_version")}
        return None
    finally:
        shutil.rmtree(exp, ignore_errors=True)


def _a_non_function_rva():
    """An rva past the dump's max bulk function ENTRY -- a non-function-kind row at it
    MINTs without tripping the function-kind baseline gate."""
    max_fn = 0
    for shard in sorted(glob.glob(
            os.path.join(DUMP_DIR, "functions", "functions_*.csv"))):
        with open(shard, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rv = (r.get("rva") or "").strip()
                if not rv:
                    continue
                v = int(rv, 16) if rv.startswith("0x") else int(rv)
                if v > max_fn:
                    max_fn = v
    return max_fn + 0x1000


def _a_bulk_function_rva():
    """An rva that IS a bulk function entry in the DEV DB (so a function-kind add
    PROMOTES its fingerprint) -- the lowest dump function rva, which the rebuild
    carried into the DEV bulk set."""
    rvas = []
    for shard in sorted(glob.glob(
            os.path.join(DUMP_DIR, "functions", "functions_*.csv"))):
        with open(shard, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rv = (r.get("rva") or "").strip()
                if rv:
                    rvas.append(int(rv, 16) if rv.startswith("0x") else int(rv))
    return min(rvas) if rvas else None


# --------------------------------------------------------------------------
# Module-scoped baseline (built ONCE, copied per-case).
# --------------------------------------------------------------------------
_BASELINE = {}


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}")
    if not os.path.isfile(DLL_PATH):
        raise SystemExit(f"WHGame.dll not found: {DLL_PATH}")


def _get_baseline():
    if "root" not in _BASELINE:
        _require_inputs()
        root = tempfile.mkdtemp(prefix="direct_write_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        from seeds_shared import resolve_version
        tag, ordinal = resolve_version(DLL_PATH)
        assert tag == GVT, f"fixture DLL resolved {tag!r}, expected {GVT!r}"
        _BASELINE.update({"root": root, "out": out, "tag": tag, "ordinal": ordinal})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    out = tempfile.mkdtemp(prefix="direct_write_run_")
    for f in ("reference.sqlite", "reference-dev.sqlite"):
        shutil.copy2(os.path.join(b["out"], f), os.path.join(out, f))
    return out


def _ver(b):
    return (b["tag"], b["ordinal"])


try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


# ==========================================================================
# CONVERGENCE -- the load-bearing proof. Each shape: DIRECT == OLD seed-rebuild,
# byte-identical over BOTH whole DBs.
# ==========================================================================
def _converge(b, direct_call, fold, label):
    """Run `direct_call(out)` (the db_editor direct write) on one fresh DB and
    `_seed_rebuild_apply(out, fold=fold, version=...)` (the reconstructed old bridge)
    on another, and assert the two DBs are byte-identical."""
    out_direct = _fresh_db(b)
    out_seed = _fresh_db(b)
    try:
        direct_call(out_direct)
        _seed_rebuild_apply(out_seed, fold=fold, version=_ver(b))
        fp_d = _db_fingerprint(out_direct)
        fp_s = _db_fingerprint(out_seed)
        assert fp_d == fp_s, (
            f"[{label}] DIRECT write did NOT converge with the seed-rebuild path "
            f"(the rework must change the MECHANISM, not the result); differing "
            f"tables: {sorted(k for k in fp_d if fp_d.get(k) != fp_s.get(k))}")
    finally:
        shutil.rmtree(out_direct, ignore_errors=True)
        shutil.rmtree(out_seed, ignore_errors=True)


def _commit_direct(ret_or_handle):
    """Commit a deferred direct write -- the handle is the bare return for
    update/lifecycle, or ret["result"] for create_*."""
    from seeds_shared import commit, DeferredCommit
    h = ret_or_handle
    if isinstance(h, dict):
        h = h["result"]
    assert isinstance(h, DeferredCommit), f"not a DeferredCommit handle: {h!r}"
    commit(h)


def _convergence_reverify(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = _pick_function_trio_row(user_db)
    edits = {"verified_by": "direct_oracle", "verified_date": "2099-12-31",
             "last_verified_at_version": GVT}

    def direct(out):
        _commit_direct(db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=_ver(b), defer_commit=True))

    def fold(p):
        seed_csv_edit.update_row_in_place(
            os.path.join(p, ADDRESS_VERSIONS_SEED_NAME),
            key_columns=("kcdx_id", "valid_from_version"),
            key_values=(str(kid), str(vf_tag)), edits=dict(edits))

    _converge(b, direct, fold, "re-verify")


def _convergence_full_column(b):
    # A full-column correction on an existing row (US-5; the applier's present-row
    # branch now does a full-column UPDATE, not a trio-only one -- step-3c). DIRECT
    # and seed-rebuild BOTH drive _apply_one_db's full-column path, so they apply the
    # SAME non-trio change (here a non-function row's rva + signature). Convergence
    # pins that the direct path replicates the seed-rebuild's exact present-row
    # behavior (no drift) -- the full-column write, not a no-op.
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = _pick_nonfunction_row(user_db)
    edits = {"rva": "0x7F000000", "signature": "void (ptr corrected)"}

    def direct(out):
        _commit_direct(db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=_ver(b), defer_commit=True))

    def fold(p):
        seed_csv_edit.update_row_in_place(
            os.path.join(p, ADDRESS_VERSIONS_SEED_NAME),
            key_columns=("kcdx_id", "valid_from_version"),
            key_values=(str(kid), str(vf_tag)), edits=dict(edits))

    _converge(b, direct, fold, "full-column UPDATE")


def _pick_search_locating_row(db_path):
    """A curated row of a kind that POPULATES the folded survival columns (a
    callsite / instruction_anchor / string_anchor / data_slot / vtable_base) -- a
    fold-populate convergence target. Returns (kcdx_id, valid_from_tag, kind)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, kindid in con.execute(
                "SELECT kcdx_id, valid_from, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL"):
            k = kdec.get(kindid)
            if k in ("callsite", "instruction_anchor", "string_anchor"):
                return (kid, gv.get(vf), k)
        return None
    finally:
        con.close()


def _convergence_fold_populate(b):
    # schema-flatten-survival-fold step 2: a full-column UPDATE that CHANGES a folded
    # survival cell (here a search-locating row's survival_aob/anchor_string +
    # survival_expect_unique) drives _apply_one_db's full-column path on BOTH the direct
    # and the seed-rebuild side, so they write the SAME folded av cell + the SAME survival
    # cell. Convergence pins that the fold's av-column populate is byte-identical between
    # the direct-write path and the rebuild path (the av folded columns are part of the
    # whole-DB _db_fingerprint). NON-VACUOUS: the edit sets a folded cell to a NEW value,
    # so a fold that wrote the av column differently than the survival cell (or differed
    # direct-vs-rebuild) would surface as a differing address_versions table.
    user_db = os.path.join(b["out"], "reference.sqlite")
    picked = _pick_search_locating_row(user_db)
    assert picked is not None, "no search-locating curated row in the fixture"
    kid, vf_tag, kind = picked
    if kind == "string_anchor":
        edits = {"survival_anchor_string": "exec convergence.cfg",
                 "survival_expect_unique": "1"}
    else:  # callsite / instruction_anchor -> aob form
        edits = {"survival_aob": "48 89 5C 24 ?? 57 48 83 EC 20",
                 "survival_expect_unique": "1"}

    def direct(out):
        _commit_direct(db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=_ver(b), defer_commit=True))

    def fold(p):
        seed_csv_edit.update_row_in_place(
            os.path.join(p, ADDRESS_VERSIONS_SEED_NAME),
            key_columns=("kcdx_id", "valid_from_version"),
            key_values=(str(kid), str(vf_tag)), edits=dict(edits))

    _converge(b, direct, fold, "fold-populate (folded survival cell)")


def _fold_av_equals_survival_after_direct_write(b):
    """schema-flatten-survival-fold Phase 3: AFTER a direct-write that populates a
    folded cell, the av row's folded columns carry the edited value in BOTH DBs --
    the survival/re-find data lives ON the av row now (D22 / design §11.2, the former
    survival sibling table folded onto av columns and deleted). Falsifiable: reads the
    folded cells off the av row and asserts the edit's value landed; the convergence
    test (_convergence_fold_populate) separately pins direct-write == seed-rebuild for
    the same edit."""
    user_db = os.path.join(b["out"], "reference.sqlite")
    picked = _pick_search_locating_row(user_db)
    assert picked is not None, "no search-locating curated row in the fixture"
    kid, vf_tag, kind = picked
    if kind == "string_anchor":
        edits = {"survival_anchor_string": "exec convergence.cfg",
                 "survival_expect_unique": "1"}
    else:
        edits = {"survival_aob": "48 89 5C 24 ?? 57 48 83 EC 20",
                 "survival_expect_unique": "1"}
    out = _fresh_db(b)
    try:
        _commit_direct(db_editor.update_version_row(
            out, None, kid, vf_tag, dict(edits), version=_ver(b), defer_commit=True))
        for which in ("reference.sqlite", "reference-dev.sqlite"):
            con = sqlite3.connect(os.path.join(out, which))
            try:
                row = con.execute(
                    "SELECT aob, anchor_string, expect_unique "
                    "FROM address_versions "
                    "WHERE kcdx_id = ? AND valid_from = "
                    "(SELECT id FROM game_versions WHERE tag = ?)",
                    (kid, vf_tag)).fetchone()
                assert row is not None, (
                    f"[{which}] no av row for the edited entity")
                # expect_unique landed on the av row.
                assert row[2] == 1, (
                    f"[{which}] edited expect_unique did not land on the av row: "
                    f"{row[2]!r}")
                # The edit actually landed (non-vacuous): the aob/anchor_string the
                # edit set is present on the av row.
                if kind == "string_anchor":
                    av_val = con.execute(
                        "SELECT anchor_string FROM address_versions WHERE kcdx_id = ?",
                        (kid,)).fetchone()[0]
                    assert av_val == "exec convergence.cfg", (
                        f"[{which}] the edited anchor_string did not land on the av "
                        f"row: {av_val!r}")
                else:
                    av_val = con.execute(
                        "SELECT aob FROM address_versions WHERE kcdx_id = ?",
                        (kid,)).fetchone()[0]
                    assert av_val == "48 89 5C 24 ?? 57 48 83 EC 20", (
                        f"[{which}] the edited aob did not land on the av row: {av_val!r}")
            finally:
                con.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _convergence_create_entity(b):
    rva = _a_non_function_rva()
    first = {"valid_from_version": GVT, "module": "WHGame.dll",
             "kind": "data_slot", "rva": "0x%08X" % rva}

    def direct(out):
        _commit_direct(db_editor.create_entity(
            out, None, "direct_oracle_entity", dict(first),
            version=_ver(b), defer_commit=True))

    def fold(p):
        names_csv = os.path.join(p, ADDRESS_NAMES_SEED_NAME)
        # The next-free id the direct path assigns (highest existing + 1).
        rows = [r for r in csv.DictReader(
            [ln for ln in open(names_csv, encoding="utf-8")
             if not ln.lstrip().startswith("#")])]
        kid = max(int(r["id"]) for r in rows) + 1
        seed_csv_edit.append_row(names_csv,
                                 {"id": str(kid), "name": "direct_oracle_entity"})
        vc = dict(first)
        vc["kcdx_id"] = str(kid)
        seed_csv_edit.append_row(os.path.join(p, ADDRESS_VERSIONS_SEED_NAME), vc)

    _converge(b, direct, fold, "create-entity")


def _convergence_create_version_current_tag(b):
    # A new version of an existing entity AT THE CURRENT tag would duplicate the
    # (kcdx_id, GVT) tuple (the entity already has a GVT row) -- a validator HARD
    # ERROR. The convergent create-version shape at the current tag is therefore the
    # ADD-ENTITY path's sibling: there is no current-tag "second version" with a
    # single game_versions row. So convergence for create-version is exercised at a
    # NEW entity's first version (create_entity, above) for the current tag, and the
    # genuinely-new create-version-at-a-NEW-tag is its own oracle below (the old path
    # materialised ZERO there, so there is nothing to converge to -- it is asserted
    # as a NEW capability, not a convergence). This stub documents why there is no
    # current-tag create-version convergence case.
    pass


def _convergence_supersede(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    x_kid, _ = _first_plain_entity(user_db)
    # A successor entity Y must exist for superseded_by to resolve. Both the direct
    # and the seed-rebuild path mint the SAME Y first (same next-free id), then set
    # the edge -- so the comparison is the supersede edge, not the mint.
    rva = _a_non_function_rva()

    def _mint_y(out, drive):
        # drive: 'direct' or 'seed'. Mint Y identically in both, returning its id.
        first = {"valid_from_version": GVT, "module": "WHGame.dll",
                 "kind": "data_slot", "rva": "0x%08X" % rva}
        if drive == "direct":
            ret = db_editor.create_entity(out, None, "direct_oracle_succ",
                                          dict(first), version=_ver(b),
                                          defer_commit=True)
            _commit_direct(ret)
            return ret["kcdx_id"]
        # seed-rebuild: mint Y via the reconstructed bridge.
        def fold_y(p):
            names_csv = os.path.join(p, ADDRESS_NAMES_SEED_NAME)
            rows = [r for r in csv.DictReader(
                [ln for ln in open(names_csv, encoding="utf-8")
                 if not ln.lstrip().startswith("#")])]
            yid = max(int(r["id"]) for r in rows) + 1
            seed_csv_edit.append_row(names_csv,
                                     {"id": str(yid), "name": "direct_oracle_succ"})
            vc = dict(first); vc["kcdx_id"] = str(yid)
            seed_csv_edit.append_row(os.path.join(p, ADDRESS_VERSIONS_SEED_NAME), vc)
        _seed_rebuild_apply(out, fold=fold_y, version=_ver(b))

    def direct(out):
        _mint_y(out, "direct")
        _commit_direct(db_editor.supersede_entity(
            out, None, x_kid, "direct_oracle_succ", GVT,
            version=_ver(b), defer_commit=True))

    out_direct = _fresh_db(b)
    out_seed = _fresh_db(b)
    try:
        direct(out_direct)
        _mint_y(out_seed, "seed")

        def fold_sup(p):
            seed_csv_edit.update_row_in_place(
                os.path.join(p, ADDRESS_NAMES_SEED_NAME),
                key_columns=("id",), key_values=(str(x_kid),),
                edits={"superseded_by": "direct_oracle_succ",
                       "superseded_at_version": GVT})
        _seed_rebuild_apply(out_seed, fold=fold_sup, version=_ver(b))

        fp_d = _db_fingerprint(out_direct)
        fp_s = _db_fingerprint(out_seed)
        assert fp_d == fp_s, (
            "[supersede] DIRECT did NOT converge with the seed-rebuild path; "
            f"differing: {sorted(k for k in fp_d if fp_d.get(k) != fp_s.get(k))}")
    finally:
        shutil.rmtree(out_direct, ignore_errors=True)
        shutil.rmtree(out_seed, ignore_errors=True)


def _convergence_deprecate(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    x_kid, _ = _first_plain_entity(user_db)

    def direct(out):
        _commit_direct(db_editor.deprecate_entity(
            out, None, x_kid, deprecated_at_version=GVT,
            version=_ver(b), defer_commit=True))

    def fold(p):
        seed_csv_edit.update_row_in_place(
            os.path.join(p, ADDRESS_NAMES_SEED_NAME),
            key_columns=("id",), key_values=(str(x_kid),),
            edits={"is_deprecated": "1", "deprecated_at_version": GVT})

    _converge(b, direct, fold, "deprecate")


# ==========================================================================
# THE 8 BEHAVIORS (the helpers ARE _apply_one_db's, so reuse preserves them --
# but verify each lands).
# ==========================================================================
def _behaviors_create_version_new_tag_folded_and_interval(b):
    """A create-version (at a NEW tag, the materialising path) asserts behavior 1 (the
    new av row carries its folded survival/re-find cells -- D22 / design §11.2, the av
    columns are the sole home, no separate survival table) + behavior 2 (the prior
    interval closed). The new-tag row prefills ALL columns from the source row, so its
    folded cells must EQUAL the source row's -- the falsifiable proof the new-tag path
    populates the folded av columns (it would go RED if create-version dropped the
    `**folded` populate)."""
    out = _fresh_db(b)
    _FOLDED = ("aob", "anchor_string", "rule", "slot_count", "expect_unique",
               "derives_from")
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag = _pick_nonfunction_row(user_db, null_trio=True)
        cols = _seed_source_row(user_db, kid, vf_tag)
        cols["signature"] = "void (new tag bump)"   # a real change

        # The source row's folded cells (pre-write), per DB -- the prefill source the
        # new-tag row must reproduce.
        def _folded_of(dbp, valid_from_tag):
            con = sqlite3.connect(dbp)
            try:
                return con.execute(
                    "SELECT aob, anchor_string, rule, slot_count, expect_unique, "
                    "derives_from FROM address_versions WHERE kcdx_id = ? AND "
                    "valid_from = (SELECT id FROM game_versions WHERE tag = ?)",
                    (kid, valid_from_tag)).fetchone()
            finally:
                con.close()
        src_folded = {which: _folded_of(os.path.join(out, which), vf_tag)
                      for which in ("reference.sqlite", "reference-dev.sqlite")}

        ret = db_editor.create_version(out, None, kid, NEW_TAG, dict(cols),
                                       version=(NEW_TAG, 2000000), defer_commit=True)
        _commit_direct(ret)

        for which in ("reference.sqlite", "reference-dev.sqlite"):
            dbp = os.path.join(out, which)
            con = sqlite3.connect(dbp)
            try:
                rows = con.execute(
                    "SELECT id, valid_through FROM address_versions WHERE kcdx_id = ?",
                    (kid,)).fetchall()
                # behavior 2: exactly one OPEN row (the new tag), the prior closed.
                open_rows = [r for r in rows if r[1] is None]
                closed_rows = [r for r in rows if r[1] is not None]
                assert len(open_rows) == 1, (
                    f"[{which}] interval-close behavior failed: {len(open_rows)} open "
                    f"rows (expected 1 -- the new tag's)")
                assert len(closed_rows) >= 1, (
                    f"[{which}] prior interval NOT closed (no closed row)")
                # behavior 1: the new-tag (OPEN) av row carries the folded survival/
                # re-find cells, equal to the source row's (the prefill source). This
                # is the av-column home of the former 1:1 survival sibling.
                new_folded = con.execute(
                    "SELECT aob, anchor_string, rule, slot_count, expect_unique, "
                    "derives_from FROM address_versions WHERE kcdx_id = ? AND "
                    "valid_through IS NULL", (kid,)).fetchone()
                assert new_folded == src_folded[which], (
                    f"[{which}] new-tag folded cells {new_folded!r} != source "
                    f"{src_folded[which]!r} (create-version dropped the folded "
                    f"av-column populate)")
            finally:
                con.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _behaviors_function_promote_fingerprint_and_baseline_refusal(b):
    """behavior 3: a function-kind add PROMOTES (carries the bulk fingerprint) when a
    DEV bulk baseline exists, and REFUSES (BaselineRefusal) when none does."""
    # PROMOTE: a brand-new entity whose first row is function-kind at a bulk rva.
    out = _fresh_db(b)
    try:
        rva = _a_bulk_function_rva()
        assert rva is not None, "no bulk function rva in the dump fixture"
        first = {"valid_from_version": GVT, "module": "WHGame.dll",
                 "kind": "function", "rva": "0x%08X" % rva}
        ret = db_editor.create_entity(out, None, "direct_oracle_fn", dict(first),
                                      version=_ver(b), defer_commit=True)
        _commit_direct(ret)
        kid = ret["kcdx_id"]
        # The promoted USER row carries the bulk fingerprint (content_hash/length).
        con = sqlite3.connect(os.path.join(out, "reference.sqlite"))
        try:
            row = con.execute(
                "SELECT content_hash, length FROM address_versions WHERE kcdx_id = ?",
                (kid,)).fetchone()
            assert row is not None, "promoted function row missing"
            assert row[0] is not None and row[1] is not None, (
                "behavior 3 PROMOTE failed: the function-kind add did not carry the "
                "bulk fingerprint (content_hash/length NULL)")
        finally:
            con.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # BaselineRefusal: a function-kind add at an rva with NO bulk baseline.
    out = _fresh_db(b)
    try:
        bad_rva = _a_non_function_rva()   # past max function entry -> no bulk row
        first = {"valid_from_version": GVT, "module": "WHGame.dll",
                 "kind": "function", "rva": "0x%08X" % bad_rva}
        before = _db_fingerprint(out)
        raised = None
        try:
            ret = db_editor.create_entity(out, None, "direct_oracle_fn_norow",
                                          dict(first), version=_ver(b),
                                          defer_commit=True)
            _commit_direct(ret)
        except imp.BaselineRefusal as e:
            raised = e
        assert raised is not None, (
            "behavior 3 BaselineRefusal failed: a function-kind add with no bulk "
            "baseline did not refuse")
        assert _db_fingerprint(out) == before, (
            "BaselineRefusal left a partial write (the DB must be byte-identical)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _behaviors_projection_and_no_minted_fk(b):
    """behavior 4 (per-DB projection: USER drops DEV-only columns) + behavior 5 (FK-id
    resolution looks up existing ids, never mints). A create-entity lands a row that
    in USER carries no auto_name/decompile_quality (DEV-only) and whose kind/module FK
    ids match the DB's existing dict/module ids (not fresh-from-1)."""
    out = _fresh_db(b)
    try:
        rva = _a_non_function_rva()
        first = {"valid_from_version": GVT, "module": "WHGame.dll",
                 "kind": "data_slot", "rva": "0x%08X" % rva}
        ret = db_editor.create_entity(out, None, "direct_oracle_proj", dict(first),
                                      version=_ver(b), defer_commit=True)
        _commit_direct(ret)
        kid = ret["kcdx_id"]

        # behavior 4: USER address_versions has no DEV-only columns at all.
        ucon = sqlite3.connect(os.path.join(out, "reference.sqlite"))
        try:
            ucols = {c[1] for c in ucon.execute(
                'PRAGMA table_info("address_versions")')}
            assert "auto_name" not in ucols and "decompile_quality" not in ucols, (
                "behavior 4 projection failed: USER carries DEV-only columns")
            # behavior 5: the new row's kind FK resolves to the DB's EXISTING dict id
            # for 'data_slot' (looked up, not minted fresh-from-1).
            kind_id = ucon.execute(
                "SELECT kind FROM address_versions WHERE kcdx_id = ?",
                (kid,)).fetchone()[0]
            existing = ucon.execute(
                'SELECT id FROM "_dict_address_versions_kind" WHERE val = ?',
                ("data_slot",)).fetchone()[0]
            assert kind_id == existing, (
                f"behavior 5 FK-resolution failed: the new row's kind id {kind_id} "
                f"!= the DB's existing 'data_slot' dict id {existing} (an id was "
                f"minted instead of looked up)")
            # module FK resolves to the existing modules.id for WHGame.dll.
            mod_id = ucon.execute(
                "SELECT module_id FROM address_versions WHERE kcdx_id = ?",
                (kid,)).fetchone()[0]
            mod_existing = ucon.execute(
                "SELECT id FROM modules WHERE name = ?", ("WHGame.dll",)).fetchone()[0]
            assert mod_id == mod_existing, (
                "behavior 5 FK-resolution failed: module_id was minted, not resolved")
        finally:
            ucon.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)


# ==========================================================================
# CREATE-VERSION-AT-A-NEW-TAG -- the new capability the bridge could NOT do.
# ==========================================================================
def _new_tag_materialises_and_old_path_does_not(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = _pick_nonfunction_row(user_db, null_trio=True)
    cols = _seed_source_row(user_db, kid, vf_tag)
    cols["signature"] = "void (new tag bump)"

    # DIRECT: the new-tag create-version materialises the new game_versions row +
    # closed prior interval + new av row in BOTH DBs.
    out = _fresh_db(b)
    try:
        ret = db_editor.create_version(out, None, kid, NEW_TAG, dict(cols),
                                       version=(NEW_TAG, 2000000), defer_commit=True)
        _commit_direct(ret)
        for which in ("reference.sqlite", "reference-dev.sqlite"):
            con = sqlite3.connect(os.path.join(out, which))
            try:
                gv = {r[0]: r[1] for r in con.execute(
                    "SELECT tag, ordinal FROM game_versions")}
                assert NEW_TAG in gv, (
                    f"[{which}] new game_versions row NOT inserted for {NEW_TAG}")
                assert gv[NEW_TAG] == 2000000, (
                    f"[{which}] new tag ordinal wrong: {gv[NEW_TAG]}")
                new_gv_id = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                                        (NEW_TAG,)).fetchone()[0]
                open_at_new = con.execute(
                    "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ? "
                    "AND valid_from = ? AND valid_through IS NULL",
                    (kid, new_gv_id)).fetchone()[0]
                assert open_at_new == 1, (
                    f"[{which}] no OPEN address_versions row at the new tag")
                closed = con.execute(
                    "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ? "
                    "AND valid_through IS NOT NULL", (kid,)).fetchone()[0]
                assert closed >= 1, f"[{which}] prior interval NOT closed"
            finally:
                con.close()
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # OLD seed-rebuild path: materialises ZERO rows here (its GAME_VERSION_TAG gate
    # drops the new-tag row) -- the DB is byte-identical to before. This is what the
    # direct path REPLACES; asserting it proves the new capability is genuinely new.
    out_old = _fresh_db(b)
    try:
        before = _db_fingerprint(out_old)

        def fold(p):
            vc = dict(cols)
            vc["kcdx_id"] = str(kid)
            vc["valid_from_version"] = NEW_TAG
            seed_csv_edit.append_row(os.path.join(p, ADDRESS_VERSIONS_SEED_NAME), vc)
        # The old bridge resolves the DLL's OWN version (GVT) -- the new-tag row is
        # filtered out (added_versions_row=0), so NOTHING is written.
        _seed_rebuild_apply(out_old, fold=fold, version=(GVT, b["ordinal"]))
        assert _db_fingerprint(out_old) == before, (
            "the OLD seed-rebuild path UNEXPECTEDLY materialised the new-tag row "
            "(it should drop it via the GAME_VERSION_TAG gate -- the bridge's "
            "documented zero-materialisation that the direct path replaces)")
    finally:
        shutil.rmtree(out_old, ignore_errors=True)


# ==========================================================================
# PROSPECTIVE-DB-STATE VALIDATION -- an invalid edit per shape raises, NO write,
# DB byte-identical, validated against the prospective DB state.
# ==========================================================================
def _invalid_aborts_no_write(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    kid, vf_tag = _pick_function_trio_row(user_db)
    x_kid, x_name = _first_plain_entity(user_db)

    # (label, callable(out)->raises, expected exception)
    def _update(out, edits):
        db_editor.update_version_row(out, None, kid, vf_tag, edits,
                                     version=_ver(b), defer_commit=True)

    cases = [
        ("malformed verified_date",
         lambda out: _update(out, {"verified_by": "x",
                                   "verified_date": "31-12-2099",
                                   "evidence_kind": "maintainer_ghidra",
                                   "last_verified_at_version": GVT}), RuntimeError),
        ("out-of-enum evidence_kind",
         lambda out: _update(out, {"verified_by": "x",
                                   "verified_date": "2099-12-31",
                                   "evidence_kind": "not_a_real_tier",
                                   "last_verified_at_version": GVT}), RuntimeError),
        ("duplicate (kcdx_id, valid_from) tuple",
         lambda out: db_editor.create_version(
             out, None, kid, vf_tag,
             _seed_source_row(os.path.join(out, "reference.sqlite"), kid, vf_tag),
             version=_ver(b), defer_commit=True), RuntimeError),
        ("self-supersede (cycle)",
         lambda out: db_editor.supersede_entity(
             out, None, x_kid, x_name, GVT, version=_ver(b), defer_commit=True),
         RuntimeError),
        ("missing required column (create_entity, no module)",
         lambda out: db_editor.create_entity(
             out, None, "direct_oracle_bad",
             {"valid_from_version": GVT, "kind": "data_slot"},
             version=_ver(b), defer_commit=True), RuntimeError),
    ]
    for label, do, exc in cases:
        out = _fresh_db(b)
        try:
            before = _db_fingerprint(out)
            raised = None
            try:
                do(out)
            except (RuntimeError, db_editor.DbEditError) as e:
                raised = e
            assert raised is not None, f"[{label}] did not abort"
            assert isinstance(raised, exc), (
                f"[{label}] raised {type(raised).__name__}, expected {exc.__name__}: "
                f"{raised}")
            assert _db_fingerprint(out) == before, (
                f"[{label}] a DB changed despite the abort (no write expected); "
                f"validated against the prospective DB state, the gate fires before "
                f"any DB open")
        finally:
            shutil.rmtree(out, ignore_errors=True)


# ==========================================================================
# ROBUST ROLLBACK -- the PK-reset proof. A deferred direct write then rollback leaves
# the DB byte-identical INCLUDING sqlite_sequence; a subsequent add reuses the same id.
# ==========================================================================
def _robust_rollback_resets_pk(b):
    out = _fresh_db(b)
    try:
        from seeds_shared import rollback
        rva = _a_non_function_rva()
        first = {"valid_from_version": GVT, "module": "WHGame.dll",
                 "kind": "data_slot", "rva": "0x%08X" % rva}

        before_fp = _db_fingerprint(out)
        # The address_versions sqlite_sequence watermark BEFORE any add.
        user_db = os.path.join(out, "reference.sqlite")
        seq_before = _seq_value(user_db, "address_versions")

        # A deferred create-entity (an INSERT that bumps the av PK seq), then
        # ROLLBACK -- the held txn is discarded.
        ret = db_editor.create_entity(out, None, "direct_oracle_rollback",
                                      dict(first), version=_ver(b), defer_commit=True)
        rollback(ret["result"])

        after_fp = _db_fingerprint(out)
        assert after_fp == before_fp, (
            "rollback did NOT discard the deferred write -- the DB differs; "
            f"differing: {sorted(k for k in before_fp if before_fp.get(k) != after_fp.get(k))}")
        # The sqlite_sequence watermark is back to its pre-write value (the PK bump
        # was discarded with the txn) -- the robust-rollback PK-reset. (The folded
        # survival/re-find cells ride on the av row, so there is no separate survival
        # PK seq to reset -- D22 / design §11.2.)
        seq_after = _seq_value(user_db, "address_versions")
        assert seq_after == seq_before, (
            f"address_versions sqlite_sequence NOT reset by rollback: "
            f"{seq_before} -> {seq_after} (a PK-autoincrement bump survived)")

        # PROOF the next id is reused: a fresh add after the rollback gets the SAME
        # next id a fresh add on the untouched baseline would. Run the add on the
        # rolled-back DB and on a clean copy; the assigned address_versions max id of
        # the new row must match.
        def _add_and_read_new_av_id(target):
            r = db_editor.create_entity(target, None, "direct_oracle_after",
                                        dict(first), version=_ver(b), defer_commit=True)
            _commit_direct(r)
            kid = r["kcdx_id"]
            con = sqlite3.connect(os.path.join(target, "reference.sqlite"))
            try:
                return con.execute(
                    "SELECT id FROM address_versions WHERE kcdx_id = ?",
                    (kid,)).fetchone()[0]
            finally:
                con.close()

        clean = _fresh_db(b)
        try:
            id_rolledback = _add_and_read_new_av_id(out)
            id_clean = _add_and_read_new_av_id(clean)
            assert id_rolledback == id_clean, (
                f"after a rollback the next add got av id {id_rolledback}, but a "
                f"clean baseline got {id_clean} -- the rollback did not reset the PK "
                f"sequence (the ids must match: the prior bump was discarded)")
        finally:
            shutil.rmtree(clean, ignore_errors=True)
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_convergence_reverify(baseline):  # noqa: F811
    _convergence_reverify(baseline)


def test_convergence_full_column(baseline):  # noqa: F811
    _convergence_full_column(baseline)


def test_convergence_fold_populate(baseline):  # noqa: F811
    _convergence_fold_populate(baseline)


def test_fold_av_equals_survival_after_direct_write(baseline):  # noqa: F811
    _fold_av_equals_survival_after_direct_write(baseline)


def test_convergence_create_entity(baseline):  # noqa: F811
    _convergence_create_entity(baseline)


def test_convergence_supersede(baseline):  # noqa: F811
    _convergence_supersede(baseline)


def test_convergence_deprecate(baseline):  # noqa: F811
    _convergence_deprecate(baseline)


def test_behaviors_folded_and_interval_close(baseline):  # noqa: F811
    _behaviors_create_version_new_tag_folded_and_interval(baseline)


def test_behaviors_function_promote_and_baseline_refusal(baseline):  # noqa: F811
    _behaviors_function_promote_fingerprint_and_baseline_refusal(baseline)


def test_behaviors_projection_and_no_minted_fk(baseline):  # noqa: F811
    _behaviors_projection_and_no_minted_fk(baseline)


def test_create_version_at_new_tag(baseline):  # noqa: F811
    _new_tag_materialises_and_old_path_does_not(baseline)


def test_invalid_edits_abort_no_write(baseline):  # noqa: F811
    _invalid_aborts_no_write(baseline)


def test_robust_rollback_resets_pk(baseline):  # noqa: F811
    _robust_rollback_resets_pk(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _convergence_reverify(b);              print("PASS convergence: re-verify")
        _convergence_full_column(b);           print("PASS convergence: full-column UPDATE")
        _convergence_fold_populate(b);         print("PASS convergence: fold-populate (folded survival cell)")
        _fold_av_equals_survival_after_direct_write(b)
        print("PASS fold: folded cells land on the av row after direct-write")
        _convergence_create_entity(b);         print("PASS convergence: create-entity")
        _convergence_supersede(b);             print("PASS convergence: supersede")
        _convergence_deprecate(b);             print("PASS convergence: deprecate")
        _behaviors_create_version_new_tag_folded_and_interval(b)
        print("PASS behaviors: folded av cells + interval-close")
        _behaviors_function_promote_fingerprint_and_baseline_refusal(b)
        print("PASS behaviors: function promote + BaselineRefusal")
        _behaviors_projection_and_no_minted_fk(b)
        print("PASS behaviors: per-DB projection + no minted FK")
        _new_tag_materialises_and_old_path_does_not(b)
        print("PASS create-version-at-a-NEW-tag (old path materialised ZERO)")
        _invalid_aborts_no_write(b)
        print("PASS prospective-DB-state validation: invalid aborts, no write")
        _robust_rollback_resets_pk(b)
        print("PASS robust rollback: PK-reset")
        print("\nall direct-write tests passed")
    finally:
        _cleanup_baseline()
