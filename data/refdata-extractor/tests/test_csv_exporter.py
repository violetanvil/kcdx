"""test_csv_exporter.py -- the DB->CSV round-trip oracle for csv_exporter.

WHAT THIS PROVES
----------------
`export_seeds` (the DB->CSV half of the round-trip, data/maintainer-tool/design.md
S4) reproduces the three committed seed CSVs BYTE-IDENTICALLY from a curated DB:

    export(import(CSVs)) == CSVs

That is the diff-preservation half of the bidirectional byte-identity round-trip
(design S4 / D2): same row order, the same `#`-comment lines in position,
QUOTE_MINIMAL per cell, the trailing-newline + CRLF convention preserved, and
DB<->CSV information-equivalence (every authored column emitted, none invented).

The test exercises the REAL exporter producing REAL CSV bytes:
  Path: rebuild a curated DB from the committed seeds (+ the mini-dump fixture),
        then run export_seeds against the built USER DB into a temp dir, and
        assert each exported file's bytes == the committed seed file's bytes.

ALL THREE CSVS ROUND-TRIP BYTE-IDENTICALLY. The one historical gap was on the
function-kind rows whose seed `signature` is EMPTY but whose kind PROMOTED a
bulk-dump row: the importer used to keep the bulk `abi_walker` signature floor
(`? (i64, ...)`) when the seed authored none, so the DB carried a signature the
seed left blank and a DB-only export could not recover the empty cell. That import
behaviour was corrected -- a curated function-kind row with a blank seed signature
now persists NULL, never the bulk floor (row_builder.build_curated_row;
data/maintainer-tool/design.md S4 information-equivalence; data/maintainer-tool/
changelog.md). DB<->CSV is now information-equivalent on `signature`, and the
byte-identity assertion below holds for all three files.

Two tests:
  1. BYTE-IDENTITY: each of the three exported CSVs equals its committed
     counterpart byte-for-byte (the strongest diff-preservation assertion -- it
     covers row order, comments, quoting, line terminator, and trailing newline
     in one comparison).
  2. COMMENT PRESERVATION: a `#`-comment line injected into a copy of a seed file
     survives the export verbatim and in position (diff-preservation S4) -- the
     committed seeds carry no comments today, so this constructs the case the
     contract requires.

SEED-DIR POINTING
-----------------
Reuses the existing oracle convention (test_apply_reverify.py): the rebuild reads
its seed paths from import_to_sqlite's MODULE_SEED_CSV / ADDRESS_NAMES_SEED_CSV /
ADDRESS_VERSIONS_SEED_CSV module constants; this test monkeypatches those three to
point at a temp seed dir for the build, restoring them after. The baseline build
runs against the small committed mini-dump excerpt (tests/fixtures/mini-dump/) so
it is fast.

RUN
---
    python tests/test_csv_exporter.py
    pytest tests/test_csv_exporter.py
"""
import contextlib
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
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import export_seeds  # noqa: E402


SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# --------------------------------------------------------------------------
# Seed-dir pointing (the test_apply_reverify.py convention): copy the committed
# seeds into a temp dir, run a rebuild with imp's seed-path constants repointed.
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


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}; the oracle needs the "
                         f"local refdata-1.5.1164953 mini-dump present.")
    for f in SEED_FILES:
        if not os.path.isfile(os.path.join(REAL_SEED_DIR, f)):
            raise SystemExit(f"committed seed not found: {f} under {REAL_SEED_DIR}")


# --------------------------------------------------------------------------
# Module-scoped baseline (built once; reused). The DB from the committed seeds.
# --------------------------------------------------------------------------
_BASELINE = {}


def _get_baseline():
    if "root" not in _BASELINE:
        _require_inputs()
        root = tempfile.mkdtemp(prefix="export_oracle_")
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
def _byte_identity(b):
    """Export from the built USER DB into a temp dir; assert each of the three
    CSVs is byte-identical to the committed seed file."""
    out_dir = tempfile.mkdtemp(prefix="export_out_")
    try:
        written = export_seeds(b["user_db"], out_dir)
        assert set(written) == set(SEED_FILES), (
            f"export_seeds wrote {set(written)}, expected {set(SEED_FILES)}")
        for f in SEED_FILES:
            exported = open(os.path.join(out_dir, f), "rb").read()
            committed = open(os.path.join(REAL_SEED_DIR, f), "rb").read()
            first_diff = next(
                (i for i in range(min(len(exported), len(committed)))
                 if exported[i] != committed[i]), "n/a")
            assert exported == committed, (
                f"{f}: exported bytes != committed seed bytes "
                f"(len exported={len(exported)} committed={len(committed)}); "
                f"first diff at byte {first_diff}")
    finally:
        shutil.rmtree(out_dir, ignore_errors=True)


def _survival_cols_source_the_av_fold(b):
    """The exported `survival_*` CSV columns are sourced from the av row's FOLDED
    columns (D22 / design S11.2 -- the av row is the flat source, the former survival
    sibling table having been folded onto it and DELETED). The exporter reads no
    survival table (there is none); it reads the folded av columns.

    Falsifiable: the rebuilt DB has NO survival table (the fold deleted it), so the
    export can ONLY source these cells from the av row. Assert (a) the DB has no
    survival table, (b) the export succeeds, and (c) each exported `survival_*` cell
    equals the av row's folded column read straight from the DB. The derives_from cell
    is checked as the inverted kcdx_id (the CSV carries the dependency entity's
    kcdx_id; the av column the resolved av_id)."""
    import csv
    import sqlite3

    work = tempfile.mkdtemp(prefix="export_avfold_")
    try:
        # A copy of the rebuilt DB -- it has no survival table (the fold deleted it),
        # so only an av-row reader reproduces the survival_* cells.
        db = os.path.join(work, "reference.sqlite")
        shutil.copy2(b["user_db"], db)
        con = sqlite3.connect(db)
        try:
            present = {r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table'")}
            assert "survival" not in present, (
                "the rebuilt DB still has a `survival` table (the fold should have "
                "deleted it)")
            # Ground truth read straight from the av row's folded columns, keyed by
            # kcdx_id, with derives_from inverted av_id -> kcdx_id (the CSV form).
            av_to_kcdx = {r[0]: r[1] for r in con.execute(
                "SELECT id, kcdx_id FROM address_versions")}

            def _txt(v):
                return "" if v is None else str(v)

            expected = {}
            for (kcdx_id, aob, anchor, rule, slot, eu, df_av) in con.execute(
                    "SELECT kcdx_id, aob, anchor_string, rule, slot_count, "
                    "expect_unique, derives_from FROM address_versions "
                    "WHERE kcdx_id IS NOT NULL"):
                df_kid = av_to_kcdx.get(df_av) if df_av is not None else None
                expected[str(kcdx_id)] = {
                    "survival_aob": _txt(aob),
                    "survival_anchor_string": _txt(anchor),
                    "survival_rule": _txt(rule),
                    "survival_slot_count": _txt(slot),
                    "survival_expect_unique": _txt(eu),
                    "survival_derives_from": _txt(df_kid),
                }
        finally:
            con.close()

        out_dir = os.path.join(work, "out")
        # Export must SUCCEED with the survival table gone (proves no read of it).
        written = export_seeds(db, out_dir)
        assert "address_versions_seed.csv" in written, (
            "export_seeds did not write the address_versions seed")

        # Parse the exported CSV; assert each survival_* cell matches the av-row
        # ground truth (a non-empty fold somewhere proves the columns carry data).
        path = os.path.join(out_dir, "address_versions_seed.csv")
        with open(path, "r", encoding="utf-8", newline="") as fh:
            reader = csv.DictReader(
                (ln for ln in fh if not ln.lstrip().startswith("#")))
            rows = list(reader)
        assert rows, "exported address_versions seed had no data rows"
        any_nonempty = False
        for row in rows:
            kid = row["kcdx_id"]
            exp = expected.get(kid)
            assert exp is not None, f"exported kcdx_id={kid} not in the av-row set"
            for col, want in exp.items():
                assert row[col] == want, (
                    f"kcdx_id={kid} CSV {col}={row[col]!r} != av-row fold {want!r} "
                    f"(the exporter must source survival_* from the av fold)")
                if want:
                    any_nonempty = True
        assert any_nonempty, (
            "no survival_* cell carried a value -- the assertion would pass "
            "vacuously; the committed seed must have at least one folded datum")
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _comment_preservation(b):
    """A `#`-comment line injected into the committed module seed survives the
    export verbatim and in position (diff-preservation S4). The committed seeds
    carry no comments today, so this constructs the contract case: write a copy
    of module_seed.csv with a comment line after the header, point the exporter's
    EXISTING-file format-detection at it, and assert the comment is re-emitted in
    the same position with the data unchanged."""
    out_dir = tempfile.mkdtemp(prefix="export_cmt_")
    try:
        committed = open(
            os.path.join(REAL_SEED_DIR, "module_seed.csv"), "rb").read().decode("utf-8")
        # Split into content lines (drop the trailing empty a trailing newline
        # leaves), insert a comment after the header, and write WITHOUT a trailing
        # newline -- so the export must preserve both the comment position AND the
        # absent trailing newline.
        lines = committed.split("\r\n")
        if lines and lines[-1] == "":
            lines = lines[:-1]
        assert lines[0].startswith("id,"), "module seed header changed"
        commented = [lines[0], "# a maintainer comment", *lines[1:]]
        target = os.path.join(out_dir, "module_seed.csv")
        with open(target, "w", encoding="utf-8", newline="") as f:
            f.write("\r\n".join(commented))   # no trailing newline -> tests that too

        # export overwrites module_seed.csv in out_dir; it must preserve the
        # comment-after-header position + the absent trailing newline.
        export_seeds(b["user_db"], out_dir)
        result = open(target, "rb").read().decode("utf-8")
        rlines = result.split("\r\n")
        assert rlines[0].startswith("id,"), "header missing after export"
        assert rlines[1] == "# a maintainer comment", (
            f"comment not preserved in position; got line[1]={rlines[1]!r}")
        assert not result.endswith("\r\n"), (
            "trailing-newline convention not preserved (source had none)")
    finally:
        shutil.rmtree(out_dir, ignore_errors=True)


# pytest entry points (use the module-scoped fixture).
def test_export_byte_identical_to_committed_seeds(baseline):  # noqa: F811
    _byte_identity(baseline)


def test_export_survival_cols_source_the_av_fold(baseline):  # noqa: F811
    _survival_cols_source_the_av_fold(baseline)


def test_export_preserves_comments_and_trailing_newline(baseline):  # noqa: F811
    _comment_preservation(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _byte_identity(b)
        print("PASS test_export_byte_identical_to_committed_seeds")
        _survival_cols_source_the_av_fold(b)
        print("PASS test_export_survival_cols_source_the_av_fold")
        _comment_preservation(b)
        print("PASS test_export_preserves_comments_and_trailing_newline")
        print("\nall csv_exporter round-trip oracle tests passed")
    finally:
        _cleanup_baseline()
