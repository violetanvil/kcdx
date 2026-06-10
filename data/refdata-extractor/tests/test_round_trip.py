"""test_round_trip.py -- the byte-identity round-trip oracle(s).

WHAT THIS PROVES
----------------
Two oracles, both driven through the REAL data-core callables in
seeds_shared.round_trip (which reuse the REAL export + REAL import -- nothing
re-implemented):

  1. round_trip() -- the CURATED build-time oracle (design S4 / S10 D2). BOTH
     directions on the mini-dump fixture, over the curated authored surface:
         import(export(DB)) == DB       (curated USER_TABLES content-hash identical)
         export(import(CSVs)) == CSVs   (the three seed CSVs byte-identical)
     The curated half rebuilds via the EXPERT-only from-dump path (run_rebuild).
     This is the GUI/build-time curated correctness gate (data_core.round_trip).

  2. round_trip_full() -- the WIDENED completeness oracle (D38; seeds-to-tracked-
     csv-migration P1.4). The FULL corpus round-trips through the D38 CSV-genesis:
     rebuild-from-CSV -> DB -> re-export -> byte-identical for BOTH the curated half
     AND the bulk half (statements / referenced_vars / call_edges + the kcdx_id-NULL
     bulk address_versions rows). The import half is run_rebuild_from_csv (NO dump,
     the 1.3 genesis); the bar is EVERY real table byte-identical -- the dropped
     curated-only exclusion. This is D38's standing completeness bar: the durable
     proof the export captures ALL the data the dump carried, so the dump-retirement
     is safe.

A divergence in either oracle is a TOOL BUG, caught here. round_trip_full() FAILS
LOUD (AP14) naming the divergent table/column.

THE GENESIS REPOINT (seeds-to-tracked-csv-migration; data/seeds RETIRED)
-----------------------------------------------------------------------
data/seeds/ is retired (D38). The curated half is now read from the TRACKED CSV
EXPORT at data/db-export/ (the three seed-shaped CSVs that survived the migration).
The curated oracle's baseline build + the widened oracle both read data/db-export/
as the curated genesis; the from-dump build's seed-path constants are repointed there
(the standard oracle convention). NO data/seeds/ reference remains.

SCALE (why the routine gate runs on a fixture, NOT the live 1.3 GB DEV DB)
-------------------------------------------------------------------------
round_trip_full()'s reference build + CSV-genesis rebuild run over the COMMITTED
MINI-DUMP excerpt (tests/fixtures/mini-dump/), which keeps the build in
milliseconds. The full DEV corpus is ~17.7M rows (statements 5.24M, referenced_vars
10.88M, call_edges 1.29M, address_versions 321K); round-tripping the real ~1.3 GB
DEV DB takes minutes and is the EXPERT-regenerate / acceptance domain, NOT the
per-commit unit gate. The oracle CALLABLE is corpus-agnostic -- it round-trips
whatever DB the dump it is handed builds; the unit gate hands it the mini-dump (fast
+ exercises every lossless edge: curated + bulk av rows, the DEV-only bulk tables,
NULL-vs-'', BLOB content_hash). A maintainer regenerating the bulk for a new game
version runs the same callable against the full dump as a one-off acceptance check
(captured in the P1.4 step deliverable), not in this gate.

RUN
---
    python -m pytest data/refdata-extractor/tests/test_round_trip.py -v
    python data/refdata-extractor/tests/test_round_trip.py
"""
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
# The small committed REAL dump excerpt (built by make_mini_dump.py) -- a fast
# rebuild; full-dump fidelity is the expert-regenerate acceptance check.
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
# D38's curated export half (the tracked CSVs that survived the data/seeds retirement).
CURATED_EXPORT_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import round_trip, RoundTripError  # noqa: E402
from seeds_shared.round_trip import round_trip_full  # noqa: E402
# The submodule object (NOT the re-exported round_trip FUNCTION of the same name on
# the package). seeds_shared.__init__ rebinds the `round_trip` attribute to the
# function, so reach the module via sys.modules to monkeypatch its export_bulk in the
# falsifiability test.
rt_mod = sys.modules["seeds_shared.round_trip"]  # noqa: E402


SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# --------------------------------------------------------------------------
# Seed-dir pointing (the test_apply_reverify.py / test_csv_exporter.py
# convention): copy the curated export CSVs into a temp dir, rebuild a curated DB
# with imp's seed-path constants repointed.
# --------------------------------------------------------------------------
def _copy_seeds(dst_dir):
    os.makedirs(dst_dir, exist_ok=True)
    for f in SEED_FILES:
        shutil.copy2(os.path.join(CURATED_EXPORT_DIR, f), os.path.join(dst_dir, f))


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
        if not os.path.isfile(os.path.join(CURATED_EXPORT_DIR, f)):
            raise SystemExit(
                f"curated export CSV not found: {f} under {CURATED_EXPORT_DIR} "
                f"(D38's curated half; the oracle reads it as the curated genesis).")


# --------------------------------------------------------------------------
# Module-scoped baseline (built once; reused). The curated USER DB + a committed
# seed copy the CURATED round-trip runs against.
# --------------------------------------------------------------------------
_BASELINE = {}


def _get_baseline():
    if "root" not in _BASELINE:
        _require_inputs()
        root = tempfile.mkdtemp(prefix="round_trip_oracle_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "db")
        _copy_seeds(seed_src)            # the curated export CSVs (the source of truth)
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
# CURATED oracle (round_trip) -- both directions over the curated authored surface.
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
    """The curated oracle must FAIL on a real divergence, not silently pass -- a
    gate that can only pass is not a gate. Mutate ONE curated cell in a COPY of the
    DB and assert round_trip raises RoundTripError on the DB direction (the rebuilt
    DB, coming from the committed seeds, will not match the tampered original)."""
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


# --------------------------------------------------------------------------
# WIDENED oracle (round_trip_full) -- the D38 full-corpus completeness bar.
# Emits the canonical acceptance signal (.claude/rules/acceptance-signal.md) so
# the agent reads one greppable result-line set.
# --------------------------------------------------------------------------
def _full_oracle_results():
    """Run round_trip_full over the mini-dump + curated export; return a list of
    (acceptance_id, ok, detail). The single load-bearing item is the full-corpus
    byte-identity -- a clean return means EVERY real table round-tripped through the
    CSV-genesis for both halves; a RoundTripError is the divergence the bar exists
    to catch."""
    results = []
    try:
        report = round_trip_full(DUMP_DIR, CURATED_EXPORT_DIR)
        results.append((
            "full-corpus-byte-identical", bool(report.get("full_identical")),
            "" if report.get("full_identical") else "round_trip_full returned "
            "without full_identical -- the oracle did not confirm the bar"))
        results.append((
            "bulk-half-byte-identical", bool(report.get("bulk_av_identical")),
            "" if report.get("bulk_av_identical") else "bulk av rows not confirmed"))
        results.append((
            "curated-half-byte-identical", bool(report.get("curated_av_identical")),
            "" if report.get("curated_av_identical") else
            "curated av rows not confirmed"))
        # The widened comparator covers the BULK DEV tables (the dropped exclusion):
        # assert they are in the compared set, not just the curated USER_TABLES.
        compared = set(report.get("real_tables_compared", ()))
        bulk_covered = {"statements", "referenced_vars", "call_edges"} <= compared
        results.append((
            "bulk-dev-tables-in-scope", bulk_covered,
            f"compared {sorted(compared)} -- the bulk DEV tables statements/"
            f"referenced_vars/call_edges must be covered (the dropped exclusion)"
            if not bulk_covered else ""))
    except RoundTripError as exc:
        # The bar caught a real lossless-failure -- the corpus did NOT round-trip.
        results.append(("full-corpus-byte-identical", False, str(exc).splitlines()[0]))
    return results


def _emit_signal(results):
    """Emit the canonical acceptance signal (.claude/rules/acceptance-signal.md) to
    stdout -- one ACCEPT-RESULT per item, one ACCEPT-SUITE aggregate last."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


def _full_divergence_is_caught():
    """FALSIFIABILITY (AP15): round_trip_full must FAIL when a BULK COLUMN does NOT
    round-trip -- a gate that can only pass is not a gate. Inject a bulk-column
    divergence by wrapping export_bulk so the bulk address_versions CSV drops one
    cell's value (the exporter "loses" a bulk `length` value), then assert
    round_trip_full raises RoundTripError naming the divergent table.

    This is the direct proof the widened oracle is not a tautology: if a bulk column
    silently failed to round-trip (the exact D38 lossless-failure the bar guards),
    the oracle catches it. A widened oracle that passed here while the bulk diverged
    would be the AP15 tautology this test forecloses."""
    real_export_bulk = rt_mod.export_bulk

    def corrupting_export_bulk(dev_db_path, out_dir):
        # Run the real bulk export, then CORRUPT one bulk av row's `length` cell in
        # the written CSV so the CSV-genesis rebuild produces a divergent bulk row.
        written = real_export_bulk(dev_db_path, out_dir)
        av_csv = os.path.join(out_dir, "address_versions.csv")
        import csv as _csv
        with open(av_csv, newline="", encoding="utf-8") as f:
            rows = list(_csv.reader(f))
        header = rows[0]
        if "length" in header and len(rows) > 1:
            li = header.index("length")
            # Set a value the dump-built reference will not match (a bulk column
            # divergence the round-trip must catch). 999999999 is outside the
            # mini-dump's real lengths.
            rows[1][li] = "999999999"
        with open(av_csv, "w", newline="", encoding="utf-8") as f:
            w = _csv.writer(f)
            w.writerows(rows)
        return written

    rt_mod.export_bulk = corrupting_export_bulk
    try:
        raised = False
        detail = ""
        try:
            round_trip_full(DUMP_DIR, CURATED_EXPORT_DIR)
        except RoundTripError as exc:
            raised = True
            detail = str(exc)
        assert raised, ("round_trip_full did NOT catch an injected bulk-column "
                        "divergence -- the widened oracle is a TAUTOLOGY (AP15): it "
                        "passes even when a bulk column fails to round-trip")
        # AP14: the failure must NAME the divergent table (address_versions), not a
        # bare 'round-trip failed'.
        assert "address_versions" in detail, (
            f"round_trip_full raised but did not name the divergent table "
            f"address_versions (AP14 -- the failure must localise); detail={detail!r}")
    finally:
        rt_mod.export_bulk = real_export_bulk


# --------------------------------------------------------------------------
# pytest entry points
# --------------------------------------------------------------------------
def test_round_trip_both_directions_byte_identical(baseline):  # noqa: F811
    _both_directions(baseline)


def test_round_trip_catches_a_divergence(baseline):  # noqa: F811
    _divergence_is_caught(baseline)


def test_round_trip_full_corpus_byte_identical():
    """round_trip_full asserts the FULL corpus (curated + bulk) round-trips through
    the D38 CSV-genesis byte-identical -- the standing completeness bar. Emits the
    canonical ACCEPT signal."""
    results = _full_oracle_results()
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "round_trip_full failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


def test_round_trip_full_catches_a_bulk_divergence():
    """FALSIFIABILITY (AP15): round_trip_full FAILS LOUD when a bulk column does not
    round-trip -- proving the widened oracle is not a tautology."""
    _full_divergence_is_caught()


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _both_directions(b)
        print("PASS test_round_trip_both_directions_byte_identical")
        _divergence_is_caught(b)
        print("PASS test_round_trip_catches_a_divergence")
        results = _full_oracle_results()
        _emit_signal(results)
        full_failed = [aid for aid, ok, _ in results if not ok]
        if full_failed:
            print("FAIL round_trip_full: " + ", ".join(full_failed))
        _full_divergence_is_caught()
        print("PASS test_round_trip_full_catches_a_bulk_divergence")
        print("\nall round-trip oracle tests passed" if not full_failed
              else "\nROUND_TRIP_FULL FAILED")
        sys.exit(1 if full_failed else 0)
    finally:
        _cleanup_baseline()
