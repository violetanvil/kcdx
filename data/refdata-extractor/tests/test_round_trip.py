"""test_round_trip.py -- the BIDIRECTIONAL byte-identity round-trip oracle.

WHAT THIS PROVES
----------------
The round-trip contract (data/maintainer-tool/design.md S4 / S10 D2) holds BOTH
directions on the mini-dump fixture, driven through the REAL data-core callable
seeds_shared.round_trip (which reuses the REAL export + REAL import -- neither
re-implemented):

    import(export(DB)) == DB       -- export the curated DB to fresh CSVs, rebuild
                                      a DB from those CSVs (+ the mini-dump), and
                                      assert the rebuilt USER DB's CURATED tables
                                      (schema.USER_TABLES) are content-hash-identical
                                      to the original. The bulk dev-only discovery
                                      tables (call_edges / statements /
                                      referenced_vars / the _dict_* discovery
                                      lookups) are NOT compared -- they are bulk
                                      discovery data, not the authored round-trip
                                      surface (design S4).
    export(import(CSVs)) == CSVs   -- re-export the DB and assert the three seed
                                      CSVs are byte-identical to the committed seeds
                                      (the diff-preservation half: row order,
                                      #-comments, QUOTE_MINIMAL, line terminator,
                                      trailing newline -- all in one byte compare).

A divergence in either direction is a TOOL BUG, caught here -- the same oracle the
GUI save chain (design S5 / US-4) calls after every write.

WHY round_trip() AND NOT A LOCAL RE-IMPLEMENTATION
--------------------------------------------------
The export half is seeds_shared.csv_exporter.export_seeds; the import half is
import_to_sqlite.run_rebuild. The shared seeds_shared.round_trip callable packages
both and owns the DB-curated-table + CSV-byte comparison; this test DRIVES that
callable (it is what the GUI also calls), it does not re-do either half. The
DB-direction compare is at content-hash level (every cell canonicalized, ordered
rows), not a loose field check.

SEED-DIR POINTING / FIXTURE
---------------------------
Reuses the existing oracle convention (test_csv_exporter.py / test_apply_reverify.py):
a curated baseline USER DB is rebuilt ONCE from the committed seeds + the small
committed mini-dump excerpt (tests/fixtures/mini-dump/) so the build is fast; the
round_trip callable then runs against that DB + the committed seeds. round_trip
points the importer's seed-path constants itself (the same mechanism), so this
test only builds the baseline.

RUN
---
    python tests/test_round_trip.py
    pytest tests/test_round_trip.py
"""
import os
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
# The small committed REAL dump excerpt (built by make_mini_dump.py) -- a fast
# rebuild; full-dump fidelity is covered by test_rebuild_oracle.py.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import round_trip, RoundTripError  # noqa: E402


SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# --------------------------------------------------------------------------
# Seed-dir pointing (the test_apply_reverify.py / test_csv_exporter.py
# convention): copy the committed seeds into a temp dir, rebuild a curated DB
# with imp's seed-path constants repointed.
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


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}; the oracle needs the "
                         f"local refdata-1.5.1164953 mini-dump present.")
    for f in SEED_FILES:
        if not os.path.isfile(os.path.join(REAL_SEED_DIR, f)):
            raise SystemExit(f"committed seed not found: {f} under {REAL_SEED_DIR}")


# --------------------------------------------------------------------------
# Module-scoped baseline (built once; reused). The curated USER DB + a committed
# seed copy the round-trip runs against.
# --------------------------------------------------------------------------
_BASELINE = {}


def _get_baseline():
    if "root" not in _BASELINE:
        _require_inputs()
        root = tempfile.mkdtemp(prefix="round_trip_oracle_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "db")
        _copy_seeds(seed_src)            # the committed seeds (the source of truth)
        _rebuild_into(seed_src, out)     # -> reference.sqlite (curated USER DB)
        _BASELINE.update({
            "root": root,
            "user_db": os.path.join(out, "reference.sqlite"),
            "seed_src": seed_src,
        })
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


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
# Tests
# --------------------------------------------------------------------------
def _both_directions(b):
    """Drive the REAL round_trip callable: it asserts BOTH directions
    (import(export(DB))==DB on the curated tables AND export(import(CSVs))==CSVs
    byte-for-byte). A clean return == both directions held; it raises
    RoundTripError on any divergence."""
    report = round_trip(b["user_db"], b["seed_src"], DUMP_DIR)
    assert report["db_identical"], "round_trip did not confirm DB identity"
    assert report["csv_identical"], "round_trip did not confirm CSV identity"
    # The DB direction compared the curated authored tables, not the bulk dev
    # tables -- assert the comparison set is exactly schema.USER_TABLES.
    from seeds_shared.schema import USER_TABLES
    assert set(report["db_tables"]) == set(USER_TABLES), (
        f"round_trip compared {set(report['db_tables'])}, expected the curated "
        f"authored surface {set(USER_TABLES)}")
    assert set(report["csv_files"]) == set(SEED_FILES), (
        f"round_trip checked {set(report['csv_files'])}, expected {set(SEED_FILES)}")


def _divergence_is_caught(b):
    """The oracle must FAIL on a real divergence, not silently pass -- a gate that
    can only pass is not a gate. Mutate ONE curated cell in a COPY of the DB and
    assert round_trip raises RoundTripError on the DB direction (the rebuilt DB,
    coming from the committed seeds, will not match the tampered original)."""
    import sqlite3
    tampered_root = tempfile.mkdtemp(prefix="round_trip_tamper_")
    try:
        tampered_db = os.path.join(tampered_root, "reference.sqlite")
        shutil.copy2(b["user_db"], tampered_db)
        con = sqlite3.connect(tampered_db)
        try:
            # Flip one curated audit cell so the DB no longer matches what the
            # committed seeds + mini-dump rebuild to. (verified_by is a plain
            # TEXT cell present on curated rows; changing it cannot be recovered
            # from the unchanged committed seeds.)
            row = con.execute(
                "SELECT id FROM address_versions WHERE kcdx_id IS NOT NULL "
                "ORDER BY id LIMIT 1").fetchone()
            assert row is not None, "no curated row to tamper"
            con.execute(
                "UPDATE address_versions SET verified_by = 'TAMPERED_NONEXISTENT' "
                "WHERE id = ?", (row[0],))
            con.commit()
        finally:
            con.close()

        raised = False
        try:
            round_trip(tampered_db, b["seed_src"], DUMP_DIR)
        except RoundTripError:
            raised = True
        assert raised, ("round_trip did NOT catch a tampered curated cell -- the "
                        "oracle is not actually gating divergence")
    finally:
        shutil.rmtree(tampered_root, ignore_errors=True)


# pytest entry points (use the module-scoped fixture).
def test_round_trip_both_directions_byte_identical(baseline):  # noqa: F811
    _both_directions(baseline)


def test_round_trip_catches_a_divergence(baseline):  # noqa: F811
    _divergence_is_caught(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _both_directions(b)
        print("PASS test_round_trip_both_directions_byte_identical")
        _divergence_is_caught(b)
        print("PASS test_round_trip_catches_a_divergence")
        print("\nall round-trip oracle tests passed")
    finally:
        _cleanup_baseline()
