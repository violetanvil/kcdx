"""test_apply_deprecate_supersede.py -- the apply-equals-rebuild oracle for the
names-side actions (db-updator Phase 1, step 6): deprecate an entity, supersede X
with a new entity Y, and the acyclicity refusal.

WHAT THIS PROVES
----------------
`apply` lands a hand-edited names-side delta (an entity deprecation, or a
supersession edge whose successor is a brand-new entity) into BOTH reference DBs
WITHOUT a rebuild, and the resulting address_names rows are identical to what a
full --rebuild from the EDITED seeds would have produced -- the Phase-1 oracle
(context.md "rebuild is the oracle"). And: a seed edit that would create a
supersession cycle is refused by the validation gate with NO DB write (both
tables byte-unchanged), because the gate runs the shared acyclicity +
pair-integrity checks over the FULL seed state before any write begins.

Tests:
  1. DEPRECATE (oracle): deprecate an existing entity (set is_deprecated +
     deprecated_at_version) in an edited seed. Path A: rebuild EDITED seeds.
     Path B: rebuild ORIGINAL seeds + apply EDITED seeds. Assert the entity's
     address_names row matches A==B in BOTH DBs.
  2. SUPERSEDE (oracle): add a new entity Y, then make an existing entity X
     superseded_by Y at the baseline version. Assert X's superseded_by /
     superseded_at_version AND Y's freshly-added rows match A==B in both DBs.
     (Y lands via the existing step-4 add-entity path; supersede writes only the
     predecessor X's names-row edge.)
  3. ACYCLICITY REFUSAL: a seed edit creating a supersession cycle (X->Y->X) is
     refused by the validation gate -- apply raises, and BOTH address_names tables
     in BOTH DBs are byte-unchanged.

SEED-DIR POINTING + BASELINE FIXTURE + HELPERS
----------------------------------------------
Reuses test_apply_add_entity.py's harness verbatim (imported): _copy_seeds /
_seeds_pointed_at / _rebuild_into / _apply_into / _add_entity (kind= required) /
_names_row / _hash_table / _get_baselines / _fresh_apply_baseline, plus the
committed mini-dump DUMP_DIR. Path-B applies run against a fresh per-test copy of
the ORIGINAL-seed baseline so each test starts clean.

RUN
---
    python tests/test_apply_deprecate_supersede.py
    pytest tests/test_apply_deprecate_supersede.py
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Reuse the add-entity oracle harness wholesale: seed-dir pointing, the
# module-scoped baseline (ONE ~2s mini-dump rebuild copied per test), the seed
# read/write helpers, the _add_entity adder (kind= required), and the DB readers.
import test_apply_add_entity as base  # noqa: E402
from test_apply_add_entity import (  # noqa: E402,F401
    GVT, imp,
    _copy_seeds, _read_csv, _write_csv, _add_entity,
    _apply_into, _rebuild_into, _fresh_apply_baseline,
    _names_row, _hash_table,
    _get_baselines, _cleanup_baselines,
)


# --------------------------------------------------------------------------
# Seed editors for the names-side edits (deprecate / supersede).
# --------------------------------------------------------------------------
def _names_path(seed_dir):
    return os.path.join(seed_dir, "address_names_seed.csv")


def _first_plain_entity(seed_dir):
    """The id+name of the first names-seed entity that is NOT already deprecated
    or superseded (a clean entity to deprecate/supersede in the edit). Returns
    (id, name)."""
    _, nrows = _read_csv(_names_path(seed_dir))
    for r in nrows:
        if not (r.get("is_deprecated") or "").strip() and \
           not (r.get("superseded_by") or "").strip():
            return int(r["id"]), r["name"]
    raise SystemExit("no plain (non-deprecated, non-superseded) entity in seed")


def _set_names_fields(seed_dir, kid, updates):
    """Patch the names-seed row for kcdx_id `kid` with `updates` (col->value)."""
    nfields, nrows = _read_csv(_names_path(seed_dir))
    for r in nrows:
        if int(r["id"]) == kid:
            r.update(updates)
            break
    else:
        raise SystemExit(f"kcdx_id {kid} not in names seed")
    _write_csv(_names_path(seed_dir), nfields, nrows)


# --------------------------------------------------------------------------
# Test 1: deprecate an entity (oracle).
# --------------------------------------------------------------------------
def _deprecate_oracle(b):
    edit_seed = tempfile.mkdtemp(prefix="dep_seed_")
    rebuild_edit = tempfile.mkdtemp(prefix="dep_rebuild_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        kid, _name = _first_plain_entity(edit_seed)
        # Deprecate it at the baseline version (the only game_versions row the
        # Phase-1 seeds know). deprecation_replacement left empty (optional).
        _set_names_fields(edit_seed, kid, {
            "is_deprecated": "1",
            "deprecated_at_version": GVT,
        })

        _rebuild_into(edit_seed, rebuild_edit)          # Path A: ground truth
        apply_out = _fresh_apply_baseline(b)            # Path B: rebuild orig...
        _apply_into(edit_seed, apply_out)               # ...+ apply edited

        for label, a_db, b_db in (
                ("user", os.path.join(rebuild_edit, "reference.sqlite"),
                 os.path.join(apply_out, "reference.sqlite")),
                ("dev", os.path.join(rebuild_edit, "reference-dev.sqlite"),
                 os.path.join(apply_out, "reference-dev.sqlite"))):
            a_row = _names_row(a_db, kid)
            b_row = _names_row(b_db, kid)
            assert a_row is not None and b_row is not None, (
                f"[{label}] deprecated entity missing (rebuild={a_row is not None}"
                f", apply={b_row is not None})")
            assert b_row.get("is_deprecated"), (
                f"[{label}] apply did not set is_deprecated: {b_row}")
            assert a_row == b_row, (
                f"[{label}] deprecated names row apply != rebuild:\n"
                f"  rebuild={a_row}\n  apply  ={b_row}\n"
                f"  diff keys={[k for k in a_row if a_row.get(k)!=b_row.get(k)]}")
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        shutil.rmtree(rebuild_edit, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 2: supersede X with a NEW entity Y (oracle).
# --------------------------------------------------------------------------
def _supersede_oracle(b):
    edit_seed = tempfile.mkdtemp(prefix="sup_seed_")
    rebuild_edit = tempfile.mkdtemp(prefix="sup_rebuild_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        x_kid, _x_name = _first_plain_entity(edit_seed)
        # Add the successor Y (a non-function data_slot at a non-entry rva, so the
        # add-entity path mints it without a baseline-promote -- the simplest
        # successor; the supersede edge is what this test exercises). The add
        # picks a fresh kcdx_id and appends a names + versions row.
        y_rva = base._a_non_function_rva(edit_seed)
        y_kid = _add_entity(
            edit_seed, "oracle_supersede_successor", y_rva,
            notes="Successor Y for the supersede oracle. data slot.",
            signature="", kind="data_slot")
        y_name = "oracle_supersede_successor"
        # Make X superseded_by Y at the baseline version (the predecessor edge).
        _set_names_fields(edit_seed, x_kid, {
            "superseded_by": y_name,
            "superseded_at_version": GVT,
        })

        _rebuild_into(edit_seed, rebuild_edit)          # Path A
        apply_out = _fresh_apply_baseline(b)            # Path B
        _apply_into(edit_seed, apply_out)

        for label, a_db, b_db in (
                ("user", os.path.join(rebuild_edit, "reference.sqlite"),
                 os.path.join(apply_out, "reference.sqlite")),
                ("dev", os.path.join(rebuild_edit, "reference-dev.sqlite"),
                 os.path.join(apply_out, "reference-dev.sqlite"))):
            # Predecessor X: the superseded_by edge matches rebuild.
            ax = _names_row(a_db, x_kid)
            bx = _names_row(b_db, x_kid)
            assert bx.get("superseded_by") is not None, (
                f"[{label}] apply did not set X.superseded_by: {bx}")
            assert ax == bx, (
                f"[{label}] predecessor X names row apply != rebuild:\n"
                f"  rebuild={ax}\n  apply  ={bx}\n"
                f"  diff keys={[k for k in ax if ax.get(k)!=bx.get(k)]}")
            # Successor Y: its freshly-added names row matches rebuild.
            ay = _names_row(a_db, y_kid)
            by = _names_row(b_db, y_kid)
            assert ay is not None and by is not None, (
                f"[{label}] successor Y missing (rebuild={ay is not None}, "
                f"apply={by is not None})")
            assert ay == by, (
                f"[{label}] successor Y names row apply != rebuild:\n"
                f"  rebuild={ay}\n  apply  ={by}")
            # And X.superseded_by points at Y's id in BOTH paths.
            assert bx.get("superseded_by") == y_kid, (
                f"[{label}] apply X.superseded_by={bx.get('superseded_by')} "
                f"!= Y kcdx_id {y_kid}")
            assert ax.get("superseded_by") == y_kid, (
                f"[{label}] rebuild X.superseded_by={ax.get('superseded_by')} "
                f"!= Y kcdx_id {y_kid}")
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        shutil.rmtree(rebuild_edit, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 3: acyclicity refusal -- a supersession cycle is refused, no DB write.
# --------------------------------------------------------------------------
def _acyclicity_refusal(b):
    edit_seed = tempfile.mkdtemp(prefix="cyc_seed_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        # Build a 2-node cycle X -> Y -> X. Add Y first (a fresh entity), then set
        # X.superseded_by = Y AND Y.superseded_by = X at the baseline version. The
        # validator's check_supersession_acyclic must reject the full seed before
        # any DB write (the gate runs over the WHOLE names seed).
        x_kid, _x_name = _first_plain_entity(edit_seed)
        y_rva = base._a_non_function_rva(edit_seed)
        y_kid = _add_entity(
            edit_seed, "oracle_cycle_node_y", y_rva,
            notes="Cycle node Y for the acyclicity refusal test. data slot.",
            signature="", kind="data_slot")
        # X -> Y
        _set_names_fields(edit_seed, x_kid, {
            "superseded_by": "oracle_cycle_node_y",
            "superseded_at_version": GVT,
        })
        # Y -> X (closes the cycle). Need X's name to point back.
        _, nrows = _read_csv(_names_path(edit_seed))
        x_name = next(r["name"] for r in nrows if int(r["id"]) == x_kid)
        _set_names_fields(edit_seed, y_kid, {
            "superseded_by": x_name,
            "superseded_at_version": GVT,
        })

        apply_out = _fresh_apply_baseline(b)
        user_db = os.path.join(apply_out, "reference.sqlite")
        dev_db = os.path.join(apply_out, "reference-dev.sqlite")
        before_un = _hash_table(user_db, "address_names")
        before_dn = _hash_table(dev_db, "address_names")
        before_uv = _hash_table(user_db, "address_versions")
        before_dv = _hash_table(dev_db, "address_versions")

        raised = False
        try:
            _apply_into(edit_seed, apply_out)
        except (SystemExit, RuntimeError):
            raised = True
        assert raised, "apply did not refuse a supersession cycle"

        # Both tables in both DBs byte-unchanged (no write reached either DB).
        assert before_un == _hash_table(user_db, "address_names"), \
            "user address_names changed despite acyclicity refusal"
        assert before_dn == _hash_table(dev_db, "address_names"), \
            "dev address_names changed despite acyclicity refusal"
        assert before_uv == _hash_table(user_db, "address_versions"), \
            "user address_versions changed despite acyclicity refusal"
        assert before_dv == _hash_table(dev_db, "address_versions"), \
            "dev address_versions changed despite acyclicity refusal"
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points (reuse the add-entity module's `baselines` fixture).
# --------------------------------------------------------------------------
try:
    import pytest

    @pytest.fixture(scope="module")
    def baselines():
        bb = _get_baselines()
        yield bb
        _cleanup_baselines()
except ImportError:   # pragma: no cover
    pytest = None


def test_deprecate_oracle(baselines):  # noqa: F811
    _deprecate_oracle(baselines)


def test_supersede_oracle(baselines):  # noqa: F811
    _supersede_oracle(baselines)


def test_acyclicity_refusal(baselines):  # noqa: F811
    _acyclicity_refusal(baselines)


if __name__ == "__main__":
    try:
        bb = _get_baselines()
        _deprecate_oracle(bb)
        print("PASS test_deprecate_oracle")
        _supersede_oracle(bb)
        print("PASS test_supersede_oracle")
        _acyclicity_refusal(bb)
        print("PASS test_acyclicity_refusal")
        print("\nall apply deprecate/supersede oracle tests passed")
    finally:
        _cleanup_baselines()
