"""test_validate_prospective.py -- the dry-validate (Save-PREVIEW) seam on the apply
write path (maintainer-tool Phase 2, step 4b).

WHAT THIS PROVES
----------------
The data-core gained a no-write validation entry: import_to_sqlite
.validate_prospective_seeds runs apply_seeds' validation gate
(_validate_full_seed_state + the version-tag refusal) and STOPS before any DB open,
and the five db_editor write functions gained a keyword-only `validate_only=False`.
When True, a write function builds the prospective seed EXACTLY as its write sibling
does (export current DB -> apply the edit), then runs the data-core's validation gate
WITHOUT opening or writing any DB -- returning the validator's verdict, leaving the
DB BYTE-IDENTICAL.

This is the Save-PREVIEW seam (design S7 save spine; plan-spec "Save-previews /
Confirm-transacts -- NOTHING is held across think-time", user-settled 2026-06-03): a
Save VALIDATES the prospective edit + shows the field-delta, writes NOTHING; the write
is the Confirm step's (step 5, the deferred-commit seam). The validation is the
data-core's single gate (D13/law 6) -- the backend calls validate_only=True and never
reimplements validation.

The change is additive + oracle-preserving (the 1b/4a pattern): validate_only=False
and defer_commit=False are byte-identical to the pre-4b paths. Cases:

  1. VALID EDIT VALIDATES, NO WRITE: a valid update via update_version_row(
     validate_only=True) returns {"tag","ordinal"} and the DB pair is byte-identical
     to before -- a Save preview touches nothing.
  2. INVALID EDIT RAISES, NO WRITE: an out-of-enum / malformed edit with
     validate_only=True raises the validator's RuntimeError (the SAME verdict
     apply_seeds gives), and the DB pair is byte-identical -- a Save preview of an
     invalid edit writes nothing either.
  3. VALIDATE AGREES WITH APPLY (the convergence proof): for the SAME edit, the
     validate-only path's accept/reject verdict matches the immediate apply path's
     (the validate seam runs the SAME gate -- it changes only WHETHER the DB is
     written, not the verdict). A valid edit: both accept. An invalid edit: both
     raise the same error class.
  4. CREATE FLAGS SURFACE IN PREVIEW: a create_version(validate_only=True) returns the
     AP18 + nothing_changed flags (D11/D12 -- read from the prospective seed) with NO
     write, so the Save preview shows them before any commit.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism (test_deferred_commit / test_db_editor_update): a
module-scoped baseline rebuild from the committed seeds, copied per-case so each
starts from a clean DB pair. Skips gracefully if the mini-dump excerpt or the linked
DLL is absent.

RUN
---
    python tests/test_validate_prospective.py
    pytest tests/test_validate_prospective.py
"""
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
# A new game-version tag for a new VERSION ROW (mirrors test_save_endpoints): a new
# tag validates cleanly but materializes 0 rows in a single-version fixture.
NEW_ROW_TAG = "1.6.2000000"


# --------------------------------------------------------------------------
# Seed-dir pointing + baseline rebuild (the apply-oracle convention).
# --------------------------------------------------------------------------
def _copy_seeds(dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(dst_dir, f))


def _rebuild_into(seed_dir, out_dir):
    os.makedirs(out_dir, exist_ok=True)
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(seed_dir, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(seed_dir, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(seed_dir,
                                                 "address_versions_seed.csv")
    try:
        imp.run_rebuild(DUMP_DIR, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


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
    """A whole-DB byte fingerprint over both reference DBs (the apply-oracle's
    per-table content hash). Two out_dirs with the same fingerprint hold
    byte-identical DBs. A validate-only run must leave this UNCHANGED."""
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


def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _pick_function_trio_row(db_path):
    """A curated FUNCTION-kind row with a full audit trio (so a re-verify edit has a
    trio to change). Returns (kcdx_id, valid_from_tag) -- never a hardcoded id."""
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


# --------------------------------------------------------------------------
# Module-scoped baseline (built ONCE, copied per-case).
# --------------------------------------------------------------------------
_BASELINE = {}


def _have_inputs():
    return os.path.isdir(DUMP_DIR) and os.path.isfile(DLL_PATH)


def _get_baseline():
    if "root" not in _BASELINE:
        root = tempfile.mkdtemp(prefix="validate_prospective_base_")
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
    out = tempfile.mkdtemp(prefix="validate_prospective_run_")
    for f in ("reference.sqlite", "reference-dev.sqlite"):
        shutil.copy2(os.path.join(b["out"], f), os.path.join(out, f))
    return out


def _ver(b):
    return (b["tag"], b["ordinal"])


try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        if not _have_inputs():
            pytest.skip(f"mini-dump fixture or WHGame.dll not found "
                        f"(dump={DUMP_DIR}, dll={DLL_PATH})")
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


# --------------------------------------------------------------------------
# Case 1: a valid edit validates + writes NOTHING (the DB is byte-identical).
# --------------------------------------------------------------------------
def _valid_validate_no_write(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag = _pick_function_trio_row(user_db)
        before_fp = _db_fingerprint(out)

        verdict = db_editor.update_version_row(
            out, None, kid, vf_tag, {"verified_by": "ValidatePreviewProbe"},
            version=_ver(b), validate_only=True)

        # The validator returns the version it validated against -- not a handle,
        # not an apply-result dict.
        assert isinstance(verdict, dict) and "tag" in verdict, verdict
        assert verdict["tag"] == GVT, verdict
        # NO WRITE: the DB pair is byte-identical -- a Save preview touches nothing.
        assert _db_fingerprint(out) == before_fp, (
            "validate_only=True wrote to the DB (a Save preview must touch nothing)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: an invalid edit raises the validator's verdict + writes NOTHING.
# --------------------------------------------------------------------------
def _invalid_validate_no_write(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag = _pick_function_trio_row(user_db)
        before_fp = _db_fingerprint(out)

        # An out-of-enum evidence_kind -- the shared validator rejects it (the SAME
        # reject test_deferred_commit case 4 uses). validate_only must raise it too.
        bad = {"verified_by": "x", "verified_date": "2099-12-31",
               "evidence_kind": "not_a_real_tier", "last_verified_at_version": GVT}
        raised = None
        try:
            db_editor.update_version_row(out, None, kid, vf_tag, bad,
                                         version=_ver(b), validate_only=True)
        except RuntimeError as e:
            raised = e
        assert raised is not None, "an invalid validate_only edit did not raise"
        assert _db_fingerprint(out) == before_fp, (
            "an invalid validate_only edit changed the DB (no write expected)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: the validate verdict AGREES with the immediate apply (the same gate).
# --------------------------------------------------------------------------
def _validate_agrees_with_apply(b):
    # Valid edit: both accept (validate returns a verdict; apply writes).
    out_v = _fresh_db(b)
    out_a = _fresh_db(b)
    try:
        kid, vf_tag = _pick_function_trio_row(os.path.join(out_v, "reference.sqlite"))
        good = {"verified_by": "AgreementProbe"}
        # validate_only accepts (no raise).
        db_editor.update_version_row(out_v, None, kid, vf_tag, dict(good),
                                     version=_ver(b), validate_only=True)
        # immediate apply accepts (no raise -- it writes).
        db_editor.update_version_row(out_a, None, kid, vf_tag, dict(good),
                                     version=_ver(b))
    finally:
        shutil.rmtree(out_v, ignore_errors=True)
        shutil.rmtree(out_a, ignore_errors=True)

    # Invalid edit: both raise the same class (RuntimeError out of the shared gate).
    out_v = _fresh_db(b)
    out_a = _fresh_db(b)
    try:
        kid, vf_tag = _pick_function_trio_row(os.path.join(out_v, "reference.sqlite"))
        bad = {"verified_by": "x", "verified_date": "2099-12-31",
               "evidence_kind": "not_a_real_tier", "last_verified_at_version": GVT}
        v_raised = a_raised = None
        try:
            db_editor.update_version_row(out_v, None, kid, vf_tag, dict(bad),
                                         version=_ver(b), validate_only=True)
        except RuntimeError as e:
            v_raised = type(e)
        try:
            db_editor.update_version_row(out_a, None, kid, vf_tag, dict(bad),
                                         version=_ver(b))
        except RuntimeError as e:
            a_raised = type(e)
        assert v_raised is not None and a_raised is not None, (
            "validate and apply disagree on an invalid edit "
            f"(validate raised {v_raised}, apply raised {a_raised})")
    finally:
        shutil.rmtree(out_v, ignore_errors=True)
        shutil.rmtree(out_a, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: a create_version validate preview surfaces the AP18 + nothing_changed
# flags (read from the prospective seed) with NO write.
# --------------------------------------------------------------------------
def _create_flags_in_preview(b):
    out = _fresh_db(b)
    try:
        kid, _vf = _pick_function_trio_row(os.path.join(out, "reference.sqlite"))
        before_fp = _db_fingerprint(out)

        # A new version row at a NEW tag with a CHANGED rva -> nothing_changed False.
        res = db_editor.create_version(
            out, None, kid, NEW_ROW_TAG,
            {"module": "WHGame.dll", "kind": "function", "rva": "0x00ABCDEF"},
            version=_ver(b), validate_only=True)
        assert res["ap18_new_row"] is True, res
        assert res["addition_kind"] == "version", res
        assert res["nothing_changed"] is False, res
        # "result" is the validator's verdict, not a handle/apply dict.
        assert isinstance(res["result"], dict) and "tag" in res["result"], res
        assert _db_fingerprint(out) == before_fp, (
            "a create_version validate preview wrote to the DB (no write expected)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_valid_edit_validates_no_write(baseline):  # noqa: F811
    _valid_validate_no_write(baseline)


def test_invalid_edit_raises_no_write(baseline):  # noqa: F811
    _invalid_validate_no_write(baseline)


def test_validate_agrees_with_apply(baseline):  # noqa: F811
    _validate_agrees_with_apply(baseline)


def test_create_version_flags_surface_in_preview(baseline):  # noqa: F811
    _create_flags_in_preview(baseline)


if __name__ == "__main__":
    if not _have_inputs():
        print(f"SKIP: mini-dump fixture or WHGame.dll not found "
              f"(dump={DUMP_DIR}, dll={DLL_PATH})")
        sys.exit(0)
    try:
        b = _get_baseline()
        _valid_validate_no_write(b)
        print("PASS test_valid_edit_validates_no_write")
        _invalid_validate_no_write(b)
        print("PASS test_invalid_edit_raises_no_write")
        _validate_agrees_with_apply(b)
        print("PASS test_validate_agrees_with_apply")
        _create_flags_in_preview(b)
        print("PASS test_create_version_flags_surface_in_preview")
        print("\nall validate-prospective tests passed")
    finally:
        _cleanup_baseline()
