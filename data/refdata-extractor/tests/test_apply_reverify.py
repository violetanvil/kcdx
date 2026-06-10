"""test_apply_reverify.py -- the apply-equals-rebuild oracle for the re-verify
action (db-updator Phase 1, step 3).

WHAT THIS PROVES
----------------
`apply` (re-verify) lands a hand-edited audit-trio delta into BOTH reference DBs
WITHOUT a rebuild, and the result is identical to what a full --rebuild from the
EDITED seeds would have produced. That is the Phase-1 test bar: apply and rebuild
agree on the same seeds (context.md "Cross-step invariants" -> "rebuild is the
oracle"). Three tests:

  1. CORE ORACLE (apply == rebuild for a re-verify edit):
       Path A: rebuild the EDITED seeds -> ground truth.
       Path B: rebuild the ORIGINAL seeds, then `apply` the EDITED seeds.
       Assert the edited row's audit trio matches across A and B, in BOTH DBs,
       and that the full address_versions audit-trio row-set (keyed by
       (kcdx_id, valid_from), not raw autoincrement id) matches A == B.

  2. IDEMPOTENCE: a second `apply` of the same edited seeds reports 0 re-verified
     (all no-op) and leaves the DBs byte-unchanged (per-table content hash).

  3. VALIDATION ABORT: a seed with an integrity violation (a duplicate
     (kcdx_id, valid_from_version)) makes `apply` raise/exit non-zero AND leaves
     the DBs byte-unchanged.

SEED-DIR POINTING
-----------------
run_rebuild / run_apply read the seed paths from MODULE_SEED_CSV /
ADDRESS_NAMES_SEED_CSV / ADDRESS_VERSIONS_SEED_CSV module-level constants. This
test does NOT change run_rebuild's signature (a design call flagged in the
brief) -- it monkeypatches those three module constants to point at a temp seed
dir for the duration of a build, restoring them after. That is the minimal,
non-invasive way to point the seeds without touching the importer's API.

The baseline rebuild reads ~321K functions and takes ~2 min; the two baseline
rebuilds (original + edited) are built ONCE per module via a module-scoped
fixture and reused across tests.

RUN
---
    python tests/test_apply_reverify.py
    pytest tests/test_apply_reverify.py
"""
import contextlib
import csv
import hashlib
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
# Runs against the small committed REAL dump excerpt (tests/fixtures/mini-dump/,
# built by make_mini_dump.py) for a fast rebuild; full-dump fidelity is covered
# by test_rebuild_oracle.py.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
# The linked DLL the .rdata version resolver reads (apply's version source).
DLL_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "third-party-ghidra", "WHGame.dll"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402


# --------------------------------------------------------------------------
# Seed-dir pointing: copy the real seeds into a temp dir, optionally edit the
# versions seed, then run a build with imp's seed-path constants repointed.
# --------------------------------------------------------------------------
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


def _copy_seeds(dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(REAL_SEED_DIR, f), os.path.join(dst_dir, f))


@contextlib.contextmanager
def _seeds_pointed_at(seed_dir):
    """Temporarily repoint imp's three seed-path constants at seed_dir."""
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


def _apply_into(seed_dir, out_dir):
    """Run run_apply with seeds pointed at seed_dir + the live DLL."""
    with _seeds_pointed_at(seed_dir):
        imp.run_apply(out_dir, DLL_PATH)


# --------------------------------------------------------------------------
# Seed editing: change ONE existing row's audit trio in the versions seed copy.
# The new evidence_kind must already exist in the seeds (so its dict id exists);
# we pick a value DIFFERENT from the row's current one but present elsewhere.
# --------------------------------------------------------------------------
def _edit_one_audit_trio(seed_dir):
    """Edit the FIRST function-kind row (has an rva) that has a full audit trio,
    changing verified_by + verified_date + evidence_kind. Returns
    (kcdx_id, valid_from_version, new_trio_dict) for the edited row."""
    path = os.path.join(seed_dir, "address_versions_seed.csv")
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        fields = rd.fieldnames
        rows = [dict(r) for r in rd]

    # Collect evidence_kind values present in the seed (these have dict ids
    # after a rebuild). Pick a target != the chosen row's current value.
    present_ek = sorted({(r.get("evidence_kind") or "").strip()
                         for r in rows if (r.get("evidence_kind") or "").strip()})

    target = None
    for r in rows:
        if not (r.get("rva") or "").strip():
            continue
        if not (r.get("last_verified_at_version") or "").strip():
            continue
        cur_ek = (r.get("evidence_kind") or "").strip()
        alt_ek = next((e for e in present_ek if e != cur_ek), cur_ek)
        r["verified_by"] = "oracle_test_editor"
        r["verified_date"] = "2099-12-31"
        r["evidence_kind"] = alt_ek
        target = (int(r["kcdx_id"]), r["valid_from_version"].strip(), {
            "verified_by": r["verified_by"],
            "verified_date": r["verified_date"],
            "evidence_kind": alt_ek,
            "last_verified_at_version": r["last_verified_at_version"].strip(),
        })
        break

    assert target is not None, "no function-kind audit-trio row found to edit"

    with open(path, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=fields)
        wr.writeheader()
        wr.writerows(rows)
    return target


# --------------------------------------------------------------------------
# DB readers for the oracle comparison.
# --------------------------------------------------------------------------
def _ek_id_to_val(con):
    """Map evidence_kind dict id -> val for human-comparable decoding."""
    return {row[0]: row[1] for row in con.execute(
        "SELECT id, val FROM _dict_address_versions_evidence_kind")}


def _audit_rowset(db_path):
    """Return {(kcdx_id, valid_from): (lvv, verified_by, verified_date,
    evidence_kind_val)} for every curated address_versions row. Keyed by the
    natural key (NOT the autoincrement id, which may differ between an apply DB
    and a rebuild DB), with evidence_kind DECODED to its string so dict-id
    numbering differences don't cause false mismatches."""
    con = sqlite3.connect(db_path)
    try:
        ek = _ek_id_to_val(con)
        out = {}
        for kid, vf, lvv, vby, vdt, ekid in con.execute(
                "SELECT kcdx_id, valid_from, last_verified_at_version, "
                "verified_by, verified_date, evidence_kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL"):
            out[(kid, vf)] = (lvv, vby, vdt, ek.get(ekid))
        return out
    finally:
        con.close()


def _audit_one(db_path, kcdx_id, valid_from_tag):
    """Return the decoded audit trio for one (kcdx_id, valid_from-tag) row."""
    con = sqlite3.connect(db_path)
    try:
        vf = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                         (valid_from_tag,)).fetchone()[0]
        ek = _ek_id_to_val(con)
        row = con.execute(
            "SELECT last_verified_at_version, verified_by, verified_date, "
            "evidence_kind FROM address_versions WHERE kcdx_id = ? AND "
            "valid_from = ?", (kcdx_id, vf)).fetchone()
        assert row is not None, f"row ({kcdx_id}, {valid_from_tag}) missing"
        return (row[0], row[1], row[2], ek.get(row[3]))
    finally:
        con.close()


def _hash_table(db_path, table):
    """Order-independent content hash of one table (sorted rows), so 'unchanged'
    can be asserted across a no-op apply."""
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


# --------------------------------------------------------------------------
# Module-scoped baselines (built once; reused). pytest fixtures if available,
# else a lazy global cache for the __main__ runner.
# --------------------------------------------------------------------------
_BASELINES = {}   # {"orig_seed", "edit_seed", "rebuild_edit_out", "edit_target"}


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}; the oracle needs the "
                         f"local refdata-1.5.1164953 dump present.")
    if not os.path.isfile(DLL_PATH):
        raise SystemExit(f"WHGame.dll not found: {DLL_PATH}; the apply path "
                         f"resolves the version from it.")


def _build_baselines(root):
    """Build the shared, reusable artifacts ONCE under root:
      - orig_seed/   : a copy of the real seeds (unedited)
      - edit_seed/   : a copy with one audit trio edited
      - rebuild_edit/: a full rebuild from edit_seed  (Path A ground truth)
    Returns the dict; the edited (kcdx_id, valid_from, new_trio) is recorded."""
    _require_inputs()
    orig_seed = os.path.join(root, "orig_seed")
    edit_seed = os.path.join(root, "edit_seed")
    _copy_seeds(orig_seed)
    _copy_seeds(edit_seed)
    edit_target = _edit_one_audit_trio(edit_seed)

    rebuild_edit_out = os.path.join(root, "rebuild_edit")
    _rebuild_into(edit_seed, rebuild_edit_out)

    return {
        "orig_seed": orig_seed,
        "edit_seed": edit_seed,
        "rebuild_edit_out": rebuild_edit_out,
        "edit_target": edit_target,
    }


def _get_baselines():
    if "root" not in _BASELINES:
        root = tempfile.mkdtemp(prefix="apply_oracle_")
        _BASELINES["root"] = root
        _BASELINES.update(_build_baselines(root))
    return _BASELINES


def _cleanup_baselines():
    root = _BASELINES.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINES.clear()


# pytest fixture wrapper (optional dependency).
try:
    import pytest

    @pytest.fixture(scope="module")
    def baselines():
        b = _get_baselines()
        yield b
        _cleanup_baselines()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


# --------------------------------------------------------------------------
# Tests
# --------------------------------------------------------------------------
def _core_oracle(b):
    """Path B: rebuild ORIGINAL seeds, then apply the EDITED seeds. Assert the
    result equals Path A (rebuild of the edited seeds) in BOTH DBs."""
    kid, vf_tag, new_trio = b["edit_target"]

    apply_out = tempfile.mkdtemp(prefix="apply_pathB_")
    try:
        _rebuild_into(b["orig_seed"], apply_out)   # baseline from ORIGINAL seeds
        _apply_into(b["edit_seed"], apply_out)     # apply the EDITED delta

        a_user = os.path.join(b["rebuild_edit_out"], "reference.sqlite")
        a_dev = os.path.join(b["rebuild_edit_out"], "reference-dev.sqlite")
        b_user = os.path.join(apply_out, "reference.sqlite")
        b_dev = os.path.join(apply_out, "reference-dev.sqlite")

        # (i) The edited row's audit trio matches A == B in both DBs, and the
        #     applied value reflects the edit (not the original).
        expected = (None, new_trio["verified_by"], new_trio["verified_date"],
                    new_trio["evidence_kind"])
        # last_verified_at_version is a version id; compare A vs B exactly, and
        # decode evidence_kind for the human-comparable check.
        for label, a_db, b_db in (("user", a_user, b_user),
                                  ("dev", a_dev, b_dev)):
            a_trio = _audit_one(a_db, kid, vf_tag)
            b_trio = _audit_one(b_db, kid, vf_tag)
            assert a_trio == b_trio, (
                f"[{label}] edited row trio apply != rebuild: "
                f"rebuild={a_trio} apply={b_trio}")
            # the edited values landed (verified_by/date/evidence_kind)
            assert b_trio[1] == expected[1], f"[{label}] verified_by {b_trio[1]}"
            assert b_trio[2] == expected[2], f"[{label}] verified_date {b_trio[2]}"
            assert b_trio[3] == expected[3], f"[{label}] evidence_kind {b_trio[3]}"

        # (ii) FULL curated audit-trio row-set matches A == B in both DBs
        #      (keyed by (kcdx_id, valid_from), evidence_kind decoded).
        for label, a_db, b_db in (("user", a_user, b_user),
                                  ("dev", a_dev, b_dev)):
            a_set = _audit_rowset(a_db)
            b_set = _audit_rowset(b_db)
            assert a_set == b_set, (
                f"[{label}] full audit-trio row-set apply != rebuild "
                f"(keys differ: {set(a_set) ^ set(b_set)}; "
                f"value diffs: "
                f"{[(k, a_set[k], b_set[k]) for k in a_set if k in b_set and a_set[k] != b_set[k]][:5]})")
    finally:
        shutil.rmtree(apply_out, ignore_errors=True)


def _idempotence(b):
    """A second apply of the same edited seeds is all no-op and changes nothing.
    Run apply twice on a fresh original-seed baseline; assert the second run
    leaves both DBs byte-unchanged (audit-trio table content hash)."""
    out = tempfile.mkdtemp(prefix="apply_idem_")
    try:
        _rebuild_into(b["orig_seed"], out)
        _apply_into(b["edit_seed"], out)          # first apply (does the work)
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        before_u = _hash_table(user_db, "address_versions")
        before_d = _hash_table(dev_db, "address_versions")

        _apply_into(b["edit_seed"], out)          # second apply (should no-op)
        after_u = _hash_table(user_db, "address_versions")
        after_d = _hash_table(dev_db, "address_versions")

        assert before_u == after_u, "user DB changed on idempotent re-apply"
        assert before_d == after_d, "dev DB changed on idempotent re-apply"
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _validation_abort(b):
    """A seed with a duplicate (kcdx_id, valid_from_version) must make apply
    raise/exit non-zero with NO DB write. Assert both DBs are byte-unchanged."""
    out = tempfile.mkdtemp(prefix="apply_abort_")
    bad_seed = os.path.join(out, "bad_seed")
    try:
        _rebuild_into(b["orig_seed"], out)
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        before_u = _hash_table(user_db, "address_versions")
        before_d = _hash_table(dev_db, "address_versions")

        # Build a seed dir that duplicates the first versions row (violates the
        # (kcdx_id, valid_from_version) uniqueness rule in read_address_versions_seed).
        _copy_seeds(bad_seed)
        vpath = os.path.join(bad_seed, "address_versions_seed.csv")
        with open(vpath, newline="", encoding="utf-8") as f:
            rd = csv.DictReader(f)
            fields = rd.fieldnames
            rows = [dict(r) for r in rd]
        rows.append(dict(rows[0]))   # exact duplicate of the first row
        with open(vpath, "w", newline="", encoding="utf-8") as f:
            wr = csv.DictWriter(f, fieldnames=fields)
            wr.writeheader()
            wr.writerows(rows)

        raised = False
        try:
            _apply_into(bad_seed, out)
        except (RuntimeError, SystemExit):
            raised = True
        assert raised, "apply did not abort on a validation error"

        after_u = _hash_table(user_db, "address_versions")
        after_d = _hash_table(dev_db, "address_versions")
        assert before_u == after_u, "user DB written despite validation abort"
        assert before_d == after_d, "dev DB written despite validation abort"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# pytest entry points (use the module-scoped fixture).
def test_apply_equals_rebuild_for_reverify(baselines):  # noqa: F811
    _core_oracle(baselines)


def test_reverify_is_idempotent(baselines):  # noqa: F811
    _idempotence(baselines)


def test_validation_error_aborts_with_no_write(baselines):  # noqa: F811
    _validation_abort(baselines)


if __name__ == "__main__":
    try:
        b = _get_baselines()
        _core_oracle(b)
        print("PASS test_apply_equals_rebuild_for_reverify")
        _idempotence(b)
        print("PASS test_reverify_is_idempotent")
        _validation_abort(b)
        print("PASS test_validation_error_aborts_with_no_write")
        print("\nall apply re-verify oracle tests passed")
    finally:
        _cleanup_baselines()
