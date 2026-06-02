"""test_db_editor_insert.py -- the db_editor INSERT entry points (maintainer-tool
Phase 1, step 4): create_version (Job 6 / US-6) + create_entity (Job 1 / US-7).

WHAT THIS PROVES
----------------
db_editor.create_version / create_entity are the headless, in-process entry points
the GUI calls to land a NEW row -- they drive the EXISTING validated atomic applier
(import_to_sqlite.apply_seeds, design D13) over a prospective seed they build by
exporting the current DB + APPENDING the new row(s). This test exercises the REAL
db_editor -> REAL apply -> REAL reference DBs on the mini-dump fixture (no wrapper,
no stubbed gate). Cases:

  1. NEW VERSION INSERT lands atomically: a new address_versions row for an EXISTING
     entity, with the supplied valid_from_version + prefilled columns, lands in BOTH
     DBs; the prior row's open interval is closed (apply's add-versions-row shape);
     the returned dict surfaces the AP18 new-row flag; the nothing-changed signal
     (D12) is correct -- it FIRES when the new row equals its source except
     valid_from_version, and does NOT fire when a column differs.

  2. NEW ENTITY INSERT lands atomically: create_entity assigns the next free kcdx_id
     (= highest existing names id + 1, append-only) + lands BOTH the names row and
     the first versions row in BOTH DBs; the returned dict surfaces the AP18 flag +
     the assigned id; the id is NOT one already present (append-only, no recycle).

  3. DUPLICATE (kcdx_id, valid_from_version) tuple aborts with NO write: the shared
     validator rejects the prospective seed (a RuntimeError out of apply_seeds before
     any DB open) and BOTH DBs are byte-identical to the pre-action snapshot.

  4. MISSING REQUIRED COLUMN aborts with NO write: a new entity whose first row omits
     module / kind / valid_from_version is rejected by the shared validator and BOTH
     DBs are byte-identical to the snapshot.

WHY THE BRIDGE IS SOUND (no separate write path -- D13)
-------------------------------------------------------
create_version / create_entity write NOTHING under data/seeds/ -- each exports the
current DB to a TEMP seed dir, APPENDS the prospective row(s) there
(seed_csv_edit.append_row, diff-preserved via csv_exporter's writer), and drives
apply_seeds with the importer's seed-path constants pointed at the temp dir (the
round_trip.py / apply-oracle convention). The validator + the per-DB BEGIN/COMMIT
are the applier's; the tuple-uniqueness + required-column + FK rules are the
validator's HARD ERRORs -- this test asserts the post-state + the no-write-on-invalid,
never a reimplemented rule.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism (test_db_editor_update.py + test_apply_add_entity.py):
a module-scoped baseline rebuild from the committed seeds (built ONCE off the
mini-dump excerpt), copied per-test so each test starts from a clean DB pair. The
new-VERSION case needs an EXISTING entity to append a version to; the new-ENTITY
case picks a NON-function rva past the dump's max function entry so the function-kind
baseline-present gate is not the thing under test (it has its own oracle).

RUN
---
    python tests/test_db_editor_insert.py
    pytest tests/test_db_editor_insert.py
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
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import db_editor  # noqa: E402

GVT = imp.GAME_VERSION_TAG   # "1.5.1164953"
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
# DB readers.
# --------------------------------------------------------------------------
def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _av_row_decoded(db_path, kcdx_id, valid_from_tag):
    """The curated address_versions row for (kcdx_id, valid_from-tag) as a dict, id
    + module_id excluded, dict columns decoded, version-id columns normalized to
    their game_versions tag -- content, not internal numbering."""
    con = sqlite3.connect(db_path)
    try:
        vf = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                         (valid_from_tag,)).fetchone()[0]
        cols = [c[1] for c in con.execute('PRAGMA table_info("address_versions")')]
        row = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} '
            f'FROM address_versions WHERE kcdx_id = ? AND valid_from = ?',
            (kcdx_id, vf)).fetchone()
        if row is None:
            return None
        d = dict(zip(cols, row))
        for dc in ("kind", "caller_arg_agreement", "evidence_kind"):
            if dc in d and d[dc] is not None:
                d[dc] = _dict_id_to_val(con, "address_versions", dc).get(d[dc])
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        for vc in ("valid_from", "valid_through", "last_verified_at_version"):
            if vc in d and d[vc] is not None:
                d[vc] = gv.get(d[vc], d[vc])
        d.pop("id", None)
        d.pop("module_id", None)
        return d
    finally:
        con.close()


def _names_row(db_path, kcdx_id):
    con = sqlite3.connect(db_path)
    try:
        cols = [c[1] for c in con.execute('PRAGMA table_info("address_names")')]
        row = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} '
            f'FROM address_names WHERE id = ?', (kcdx_id,)).fetchone()
        return dict(zip(cols, row)) if row else None
    finally:
        con.close()


def _count_av_rows(db_path, kcdx_id):
    con = sqlite3.connect(db_path)
    try:
        return con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ?",
            (kcdx_id,)).fetchone()[0]
    finally:
        con.close()


def _all_names_ids(db_path):
    con = sqlite3.connect(db_path)
    try:
        return sorted(r[0] for r in con.execute("SELECT id FROM address_names"))
    finally:
        con.close()


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


_SNAP_TABLES = ("address_versions", "address_names", "survival")


def _snapshot(out_dir):
    """Per-table content hash of both DBs -- the no-write fingerprint asserted
    byte-identical after an invalid INSERT."""
    out = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        for t in _SNAP_TABLES:
            out[(which, t)] = _hash_table(dbp, t)
    return out


# --------------------------------------------------------------------------
# Edit-target pickers from the BUILT DB (use whatever the fixture carries).
# --------------------------------------------------------------------------
def _pick_existing_nonfunction_entity(db_path, *, null_trio=False):
    """An EXISTING curated NON-function entity (so create_version on it does not hit
    the function-kind baseline-present gate -- that has its own oracle). Returns
    (kcdx_id, valid_from_tag, source_columns) where source_columns is the row's
    authored cells as the seed carries them (module/kind/rva/signature/the trio --
    the create_version prefill source).

    `null_trio=True` restricts to a row whose audit trio is NULL. The
    nothing-changed (D12) FIRES case copies a source row to a NEW valid_from_version
    identical on every other column -- but a NEW (newer) valid_from with the SOURCE'S
    last_verified_at_version=<older tag> would violate the validator's
    `last_verified >= valid_from` HARD ERROR. A NULL-trio source has no
    last_verified, so its identical copy at a newer tag is itself apply-valid -- the
    clean way to assert the D12 signal without entangling an unrelated validator
    rule."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        ekdec = _dict_id_to_val(con, "address_versions", "evidence_kind")
        for (kid, vf, kindid, rva, sig, lvv, vby, vdt, ek) in con.execute(
                "SELECT av.kcdx_id, av.valid_from, av.kind, av.rva, av.signature, "
                "av.last_verified_at_version, av.verified_by, av.verified_date, "
                "av.evidence_kind FROM address_versions av "
                "WHERE av.kcdx_id IS NOT NULL"):
            knd = kdec.get(kindid)
            if knd in ("function", "function_variadic", "function_no_sig"):
                continue
            if null_trio and lvv is not None:
                continue
            mod = con.execute(
                "SELECT m.name FROM address_versions av JOIN modules m "
                "ON av.module_id = m.id WHERE av.kcdx_id = ? AND av.valid_from = ?",
                (kid, vf)).fetchone()[0]
            cols = {
                "module": mod,
                "kind": knd,
                "rva": ("0x%08X" % rva) if rva is not None else "",
                "signature": sig or "",
                "last_verified_at_version": gv.get(lvv) if lvv is not None else "",
                "verified_by": vby or "",
                "verified_date": vdt or "",
                "evidence_kind": ekdec.get(ek) if ek is not None else "",
            }
            return (kid, gv.get(vf), cols)
        return None
    finally:
        con.close()


def _seed_source_row(db_path, kcdx_id, valid_from_tag):
    """The FULL authored seed cells of the (kcdx_id, valid_from) row, read from the
    exported seed -- the complete prefill source US-6 describes ('prefills ALL
    columns from a chosen source row'). Returns {column: cell_string} with the
    identity-key columns dropped (create_version takes those as args). This is the
    faithful prefill: every authored column the source carries, so an unchanged copy
    actually matches the source on every compared column (the D12 signal's input)."""
    exp = tempfile.mkdtemp(prefix="db_editor_ins_src_")
    try:
        imp_export = __import__("seeds_shared", fromlist=["export_seeds"])
        imp_export.export_seeds(db_path, exp)
        from seeds_shared.csv_exporter import ADDRESS_VERSIONS_SEED_NAME
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
    """An rva past the dump's max bulk function ENTRY -- a non-function-kind row at
    it MINTs (fingerprint NULL) without tripping the function-kind baseline gate
    (mirrors test_apply_add_entity._a_non_function_rva). Used for the new-ENTITY
    case so the test exercises the INSERT machinery, not the function-promote gate."""
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


# --------------------------------------------------------------------------
# Module-scoped baseline.
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
        root = tempfile.mkdtemp(prefix="db_editor_ins_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        _BASELINE.update({"root": root, "out": out})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    out = tempfile.mkdtemp(prefix="db_editor_ins_run_")
    for f in ("reference.sqlite", "reference-dev.sqlite"):
        shutil.copy2(os.path.join(b["out"], f), os.path.join(out, f))
    return out


try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


# --------------------------------------------------------------------------
# Case 1: new VERSION INSERT -- AP18 flag + nothing-changed (D12) signals correct.
#
# SCOPE NOTE (a surfaced fixture limit, NOT a db_editor defect -- the same blocker
# test_apply_add_entity §4 documents): the fixture carries exactly ONE game_versions
# row (GVT). The apply diff materializes only baseline-version rows -- a new version
# at a NEW tag is SILENTLY SKIPPED (policy.md "Surprise the maintainer should know
# about"; added_versions_row counts 0), and a new version at the SAME tag is a
# DUPLICATE-tuple HARD ERROR (Case 3). So a new-version row that actually
# MATERIALIZES a second DB row (the interval-close + insert) is not constructible
# until a 2nd game_versions row exists -- exactly where test_apply_add_entity defers
# its add-versions-row materialization oracle. (Probed live: create_version at a 1.6
# tag validates + applies cleanly with added_versions_row=0, no row written.)
#
# This case therefore asserts what THIS step owns and the fixture CAN exercise: the
# new-version row is accepted by the shared validator (no raise on a valid prefilled
# row), the AP18 new-row flag is surfaced, and the D12 nothing-changed signal is
# correct in BOTH directions (fires on an identical-except-valid_from copy; does NOT
# fire when a column differs). The new-entity case (Case 2) is the step's true
# "lands atomically in both DBs" oracle (add-entity materializes at the baseline
# tag). The add-versions-row materialization oracle flips on when a 2nd game version
# lands.
_NEW_TAG = "1.6.2000000"


def _new_version_signals(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        # A NULL-trio source so the identical copy at a NEWER tag is itself
        # apply-valid (a non-null last_verified at the OLDER tag would violate
        # last_verified >= valid_from -- unrelated to the D12 signal under test).
        target = _pick_existing_nonfunction_entity(user_db, null_trio=True)
        assert target is not None, \
            "no curated non-function NULL-trio entity in the fixture"
        kid, vf_tag, _ = target
        # The COMPLETE prefill (every authored column the source row carries) -- the
        # GUI prefills all of them (US-6); a partial prefill would make an unchanged
        # copy differ on the omitted columns and the D12 signal correctly NOT fire.
        src_cols = _seed_source_row(user_db, kid, vf_tag)
        assert src_cols is not None, "could not read the source seed row"

        # (a) nothing-changed FIRES: a new version identical to the source on every
        # authored column except valid_from_version. create_version does not raise
        # (the prospective seed validates), surfaces the AP18 flag, and reports the
        # D12 steering signal.
        ret = db_editor.create_version(
            out, DLL_PATH, kid, _NEW_TAG, dict(src_cols))
        assert ret["ap18_new_row"] is True, \
            "AP18 new-row flag not surfaced on a new version"
        assert ret["addition_kind"] == "version", \
            f"addition_kind != 'version': {ret['addition_kind']!r}"
        assert ret["nothing_changed"] is True, (
            "nothing-changed did NOT fire on a new version identical to its source "
            "except valid_from_version (D12 steering signal missing)")

        # (b) nothing-changed does NOT fire: a new version with a CHANGED column
        # (a different signature). The signal must distinguish a real new version
        # from a duplicate-but-for-the-tag, or the GUI would mis-steer.
        out2 = _fresh_db(b)
        try:
            changed = dict(src_cols)
            changed["signature"] = "void (ptr changed_for_test)"
            ret2 = db_editor.create_version(
                out2, DLL_PATH, kid, _NEW_TAG, changed)
            assert ret2["nothing_changed"] is False, (
                "nothing-changed FIRED on a new version whose signature differs "
                "from the source (false positive -- D12 would mis-steer)")
            assert ret2["ap18_new_row"] is True
        finally:
            shutil.rmtree(out2, ignore_errors=True)
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: new ENTITY INSERT lands atomically; next-free id; AP18 flag.
# --------------------------------------------------------------------------
def _new_entity_lands(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        before_ids = _all_names_ids(user_db)
        expected_id = max(before_ids) + 1

        rva = _a_non_function_rva()
        ret = db_editor.create_entity(
            out, DLL_PATH, "db_editor_oracle_new_entity",
            first_version_columns={
                "valid_from_version": GVT,
                "module": "WHGame.dll",
                "kind": "data_slot",
                "rva": "0x%08X" % rva,
                # A brand-new unverified row: the audit trio is all-null.
            })

        assert ret["ap18_new_row"] is True, \
            "AP18 new-row flag not surfaced on a new entity"
        assert ret["addition_kind"] == "entity", \
            f"addition_kind != 'entity': {ret['addition_kind']!r}"
        assert ret["kcdx_id"] == expected_id, (
            f"next-free kcdx_id wrong: got {ret['kcdx_id']}, "
            f"expected {expected_id} (highest existing {max(before_ids)} + 1)")
        assert ret["kcdx_id"] not in before_ids, \
            "assigned a kcdx_id that already exists (append-only violated)"

        # BOTH rows landed in BOTH DBs: the names row + the first versions row.
        for label, dbp in (("user", user_db), ("dev", dev_db)):
            nrow = _names_row(dbp, expected_id)
            assert nrow is not None, f"[{label}] names row not inserted"
            assert nrow["name"] == "db_editor_oracle_new_entity", \
                f"[{label}] names row name wrong: {nrow['name']!r}"
            vrow = _av_row_decoded(dbp, expected_id, GVT)
            assert vrow is not None, f"[{label}] first versions row not inserted"
            assert vrow["kind"] == "data_slot", \
                f"[{label}] versions row kind wrong: {vrow['kind']!r}"
            assert _count_av_rows(dbp, expected_id) == 1, \
                f"[{label}] expected exactly one versions row for the new entity"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: duplicate (kcdx_id, valid_from_version) tuple aborts with NO write.
# --------------------------------------------------------------------------
def _duplicate_tuple_aborts(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        target = _pick_existing_nonfunction_entity(user_db)
        assert target is not None, "no curated non-function entity in the fixture"
        kid, vf_tag, src_cols = target

        snap = _snapshot(out)
        raised = None
        try:
            # Append a NEW version whose valid_from_version == the entity's EXISTING
            # row -> a duplicate (kcdx_id, valid_from_version) tuple. The shared
            # validator (apply_seeds' read_address_versions_seed) rejects it BEFORE
            # any DB open -> RuntimeError, no write.
            db_editor.create_version(out, DLL_PATH, kid, vf_tag, dict(src_cols))
        except (RuntimeError, db_editor.DbEditError) as e:
            raised = e
        assert raised is not None, "duplicate tuple did not abort"
        assert isinstance(raised, RuntimeError), (
            f"duplicate tuple raised {type(raised).__name__}, expected the "
            f"validator's RuntimeError: {raised}")
        assert _snapshot(out) == snap, \
            "a DB changed despite the duplicate-tuple abort (no write expected)"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: missing required column aborts with NO write.
# --------------------------------------------------------------------------
def _missing_required_aborts(b):
    out = _fresh_db(b)
    try:
        # A new entity whose first row omits the REQUIRED module + kind (and is at a
        # valid_from_version). The shared validator's required-column HARD ERROR
        # rejects the prospective seed BEFORE any DB open -> RuntimeError, no write.
        snap = _snapshot(out)
        raised = None
        try:
            db_editor.create_entity(
                out, DLL_PATH, "db_editor_oracle_missing_required",
                first_version_columns={"valid_from_version": GVT})
        except (RuntimeError, db_editor.DbEditError) as e:
            raised = e
        assert raised is not None, "missing required column did not abort"
        assert isinstance(raised, RuntimeError), (
            f"missing required raised {type(raised).__name__}, expected the "
            f"validator's RuntimeError: {raised}")
        assert _snapshot(out) == snap, \
            "a DB changed despite the missing-required abort (no write expected)"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_new_version_signals(baseline):  # noqa: F811
    _new_version_signals(baseline)


def test_new_entity_lands_atomically(baseline):  # noqa: F811
    _new_entity_lands(baseline)


def test_duplicate_tuple_aborts_with_no_write(baseline):  # noqa: F811
    _duplicate_tuple_aborts(baseline)


def test_missing_required_column_aborts_with_no_write(baseline):  # noqa: F811
    _missing_required_aborts(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _new_version_signals(b)
        print("PASS test_new_version_signals")
        _new_entity_lands(b)
        print("PASS test_new_entity_lands_atomically")
        _duplicate_tuple_aborts(b)
        print("PASS test_duplicate_tuple_aborts_with_no_write")
        _missing_required_aborts(b)
        print("PASS test_missing_required_column_aborts_with_no_write")
        print("\nall db_editor insert oracle tests passed")
    finally:
        _cleanup_baseline()
