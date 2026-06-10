"""test_apply_version_seam.py -- the optional pre-resolved `version=` seam on the
apply write path (maintainer-tool Phase 2, step 1b -- A2).

WHAT THIS PROVES
----------------
apply_seeds (and the db_editor write functions that drive it) gained an OPTIONAL
keyword-only `version=(tag, ordinal)` -- a SECOND, additive way in for a caller
that already resolved the game version (the web backend per
data/maintainer-tool/design.md D15: no DLL server-side). The change is additive +
oracle-preserving: the existing `dll_path` route is byte-identical to before, and
the new `version=` route CONVERGES on the same DB. Four cases:

  1. CONVERGENCE: apply_seeds(out, dll_path=None, version=(GVT, ordinal)) over the
     mini-dump fixture produces the BYTE-IDENTICAL DB pair to
     apply_seeds(out, dll_path). The seam's whole point -- both entry routes write
     the same thing. The ordinal is read from resolve_version(DLL) (never
     hardcoded), so the version= pair is exactly what the dll_path route resolves.

  2. REFUSAL ON THE SUPPLIED TAG: version=("some-other-tag", 0) raises
     VersionRefusal -- the tag != GAME_VERSION_TAG gate fires on the supplied tag,
     not only the DLL-read one.

  3. ARG GUARD: neither dll_path nor version -> ValueError; both -> ValueError (a
     caller passing both is a programming error, never a silent pick).

  4. DB_EDITOR PASS-THROUGH: db_editor.update_version_row driven with
     version=(GVT, ordinal) + dll_path=None lands the SAME edited row as the
     dll_path form (the version= keyword threads through the whole db_editor
     chokepoint to apply_seeds).

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism (test_apply_reverify.py / test_db_editor_update.py):
a module-scoped baseline rebuild from the committed seeds (built ONCE off the
mini-dump excerpt), copied per-case so each starts from a clean DB pair. The
seed-path constants are repointed at a temp dir for an apply (the round_trip.py /
apply-oracle convention).

RUN
---
    python tests/test_apply_version_seam.py
    pytest tests/test_apply_version_seam.py
"""
import contextlib
import hashlib
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
# The small committed REAL dump excerpt (built by make_mini_dump.py) -- a fast
# rebuild; full-dump fidelity is covered by test_rebuild_oracle.py.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
# The linked DLL the .rdata version resolver reads (the apply path's version src).
DLL_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "third-party-ghidra", "WHGame.dll"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

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
# The byte-identity comparator (the apply-oracle's _hash_table, over every table).
# --------------------------------------------------------------------------
def _hash_table(db_path, table):
    """Order-independent canonical content hash of one table -- the byte-identity
    comparator the apply oracles use."""
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
    """A whole-DB byte fingerprint: a per-table content hash over EVERY table in
    both reference DBs. Two out_dirs with the same fingerprint hold byte-identical
    DBs (modulo internal autoincrement-id row order, which the content hash is
    insensitive to). The DB-identity assertion the apply oracles use, generalized
    to every table so the convergence check covers the whole write, not one slice."""
    fp = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        con = sqlite3.connect(dbp)
        try:
            tables = [r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' "
                "ORDER BY name")]
        finally:
            con.close()
        for t in tables:
            fp[(which, t)] = _hash_table(dbp, t)
    return fp


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
        root = tempfile.mkdtemp(prefix="version_seam_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        # The real (tag, ordinal) the dll_path route resolves -- read from the
        # resolver, NEVER hardcoded. The version= route uses this exact pair so a
        # divergence can only be the seam itself, not a wrong ordinal.
        from seeds_shared import resolve_version
        tag, ordinal = resolve_version(DLL_PATH)
        assert tag == GVT, f"fixture DLL resolved {tag!r}, expected {GVT!r}"
        _BASELINE.update({"root": root, "out": out,
                          "tag": tag, "ordinal": ordinal})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    """A per-case copy of the baseline DB pair (so each case starts clean)."""
    out = tempfile.mkdtemp(prefix="version_seam_run_")
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
# A one-row audit-trio edit applied to a temp seed dir (so the apply has a real
# delta to land -- a no-op apply over the unedited seeds would converge trivially).
# Mirrors test_apply_reverify._edit_one_audit_trio.
# --------------------------------------------------------------------------
def _edited_seed_dir(work_root):
    """Copy the committed seeds into work_root/edit_seed and bump ONE function-kind
    row's audit trio, so an apply over it produces a real (non-no-op) write.
    Returns the seed dir path."""
    import csv
    seed_dir = os.path.join(work_root, "edit_seed")
    _copy_seeds(seed_dir)
    path = os.path.join(seed_dir, "address_versions_seed.csv")
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        fields = rd.fieldnames
        rows = [dict(r) for r in rd]
    present_ek = sorted({(r.get("evidence_kind") or "").strip()
                         for r in rows if (r.get("evidence_kind") or "").strip()})
    edited = False
    for r in rows:
        if not (r.get("rva") or "").strip():
            continue
        if not (r.get("last_verified_at_version") or "").strip():
            continue
        cur_ek = (r.get("evidence_kind") or "").strip()
        alt_ek = next((e for e in present_ek if e != cur_ek), cur_ek)
        r["verified_by"] = "version_seam_test"
        r["verified_date"] = "2099-12-31"
        r["evidence_kind"] = alt_ek
        edited = True
        break
    assert edited, "no function-kind audit-trio row found to edit"
    with open(path, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=fields)
        wr.writeheader()
        wr.writerows(rows)
    return seed_dir


# --------------------------------------------------------------------------
# Case 1: the two entry routes CONVERGE on a byte-identical DB.
# --------------------------------------------------------------------------
def _convergence(b):
    work = tempfile.mkdtemp(prefix="version_seam_conv_")
    try:
        edit_seed = _edited_seed_dir(work)

        # Route A: the DLL path (the landed route, unchanged).
        out_dll = _fresh_db(b)
        with _seeds_pointed_at(edit_seed):
            imp.apply_seeds(out_dll, DLL_PATH)
        fp_dll = _db_fingerprint(out_dll)

        # Route B: the pre-resolved version path (the new seam), dll_path=None.
        out_ver = _fresh_db(b)
        with _seeds_pointed_at(edit_seed):
            imp.apply_seeds(out_ver, None, version=(b["tag"], b["ordinal"]))
        fp_ver = _db_fingerprint(out_ver)

        assert fp_dll == fp_ver, (
            "the version= route did NOT converge on the dll_path route's DB; "
            "differing tables: "
            f"{sorted(k for k in fp_dll if fp_dll.get(k) != fp_ver.get(k))}")
    finally:
        shutil.rmtree(work, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: the refusal gate fires on the SUPPLIED tag.
# --------------------------------------------------------------------------
def _refusal_on_supplied_tag(b):
    out = _fresh_db(b)
    try:
        raised = None
        try:
            imp.apply_seeds(out, None, version=("some-other-tag", 0))
        except imp.VersionRefusal as e:
            raised = e
        assert raised is not None, (
            "a version= tag != GAME_VERSION_TAG did not raise VersionRefusal")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: the arg guard -- neither / both raise ValueError.
# --------------------------------------------------------------------------
def _arg_guard(b):
    out = _fresh_db(b)
    try:
        # Neither dll_path nor version.
        raised = None
        try:
            imp.apply_seeds(out, None)
        except ValueError as e:
            raised = e
        assert raised is not None, "neither dll_path nor version did not raise"

        # Both dll_path AND version.
        raised = None
        try:
            imp.apply_seeds(out, DLL_PATH, version=(b["tag"], b["ordinal"]))
        except ValueError as e:
            raised = e
        assert raised is not None, "supplying BOTH dll_path and version did not raise"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: db_editor.update_version_row threads version= through to apply_seeds.
# The version= form lands the SAME edited row as the dll_path form.
# --------------------------------------------------------------------------
def _pick_function_trio_row(db_path):
    """A curated function-kind row that already has a full audit trio (so the
    re-verify edit has a trio to change). Returns (kcdx_id, valid_from_tag)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = {r[0]: r[1] for r in con.execute(
            'SELECT id, val FROM "_dict_address_versions_kind"')}
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


def _present_evidence_kinds(db_path):
    con = sqlite3.connect(db_path)
    try:
        return sorted(r[0] for r in con.execute(
            "SELECT val FROM _dict_address_versions_evidence_kind"))
    finally:
        con.close()


def _db_editor_pass_through(b):
    # The same edit landed two ways: once via dll_path, once via version=. The two
    # resulting DBs must be byte-identical (the keyword threads through the whole
    # db_editor chokepoint to apply_seeds; nothing else differs).
    pick_from = os.path.join(b["out"], "reference.sqlite")
    target = _pick_function_trio_row(pick_from)
    assert target is not None, "no function-kind audit-trio row in the fixture"
    kid, vf_tag = target
    eks = _present_evidence_kinds(pick_from)
    new_ek = eks[0] if eks else None
    edits = {
        "verified_by": "version_seam_pass_through",
        "verified_date": "2099-12-31",
        "evidence_kind": new_ek,
        "last_verified_at_version": GVT,
    }

    out_dll = _fresh_db(b)
    out_ver = _fresh_db(b)
    try:
        db_editor.update_version_row(out_dll, DLL_PATH, kid, vf_tag, dict(edits))
        db_editor.update_version_row(
            out_ver, None, kid, vf_tag, dict(edits),
            version=(b["tag"], b["ordinal"]))

        fp_dll = _db_fingerprint(out_dll)
        fp_ver = _db_fingerprint(out_ver)
        assert fp_dll == fp_ver, (
            "db_editor.update_version_row(version=) did NOT land the same row as "
            "the dll_path form; differing tables: "
            f"{sorted(k for k in fp_dll if fp_dll.get(k) != fp_ver.get(k))}")
    finally:
        shutil.rmtree(out_dll, ignore_errors=True)
        shutil.rmtree(out_ver, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_version_seam_converges_with_dll_path(baseline):  # noqa: F811
    _convergence(baseline)


def test_refusal_gate_fires_on_supplied_tag(baseline):  # noqa: F811
    _refusal_on_supplied_tag(baseline)


def test_arg_guard_rejects_neither_and_both(baseline):  # noqa: F811
    _arg_guard(baseline)


def test_db_editor_threads_version_through(baseline):  # noqa: F811
    _db_editor_pass_through(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _convergence(b)
        print("PASS test_version_seam_converges_with_dll_path")
        _refusal_on_supplied_tag(b)
        print("PASS test_refusal_gate_fires_on_supplied_tag")
        _arg_guard(b)
        print("PASS test_arg_guard_rejects_neither_and_both")
        _db_editor_pass_through(b)
        print("PASS test_db_editor_threads_version_through")
        print("\nall version-seam tests passed")
    finally:
        _cleanup_baseline()
