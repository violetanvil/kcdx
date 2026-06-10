"""test_seed_dir_repoint.py -- the seed-path-constant repoint gate
(seeds-to-tracked-csv-migration P2.1; D38's genesis-location repoint).

WHAT THIS PROVES
----------------
`import_to_sqlite`'s curated seed-path constants -- SEED_DIR + the three derived
MODULE_SEED_CSV / ADDRESS_NAMES_SEED_CSV / ADDRESS_VERSIONS_SEED_CSV -- resolve the
D38 CSV-genesis location `data/db-export/`, NOT the retired `data/seeds/`. The
toolchain's curated reads (build_rows, the validators, the rebuild oracle) read
these constants BY NAME, so the default IS the only thing that decides where a
default-context curated read lands. Step 1.3 moved the genesis read LOGIC; this
step (2.1) moves the path-constant DEFAULT those reads feed.

FALSIFIABLE (the load-bearing proof a stray data/seeds/ literal is CAUGHT)
-------------------------------------------------------------------------
- SEED_DIR's basename must be `db-export`; a constant still pointing at
  `data/seeds/` has basename `seeds` and fails the row.
- The three derived constants must each resolve UNDER db-export with the curated
  filename unchanged -- a derived constant that drifted off SEED_DIR fails.
- A run_rebuild driven through the DEFAULT constants (no repoint) against the
  committed mini-dump must SUCCEED and produce both DBs -- if SEED_DIR still
  pointed at the (now-deleted) data/seeds/, build_rows' read_module_seed raises
  FileNotFoundError (the exact pre-existing red this step resolves), so a green
  rebuild is the direct falsification of the un-repointed state.
- A source-scan over the constant-DEFINITION region asserts no `data/seeds`
  path-resolving literal remains in the active rebuild/read path (the assignment
  that decides the default), distinct from a prose comment that merely names the
  retired dir.

MECHANISM (resolved from source, NOT assumed)
---------------------------------------------
- SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export"); the three *_SEED_CSV
  constants are os.path.join(SEED_DIR, "<name>.csv"), so they FOLLOW SEED_DIR --
  this test asserts that derivation holds (they resolve under db-export) rather
  than re-hardcoding each.
- run_rebuild -> build_rows -> read_module_seed(MODULE_SEED_CSV) reads the
  constant by name; with the constants at their default, the curated reads
  resolve data/db-export/ (verified live: the mini-dump's curated RVAs cover the
  data/db-export/ curated set, the same fixture test_rebuild_from_csv.py uses).

FIXTURE -- the committed mini-dump excerpt (fast), NOT the live 1.3 GB dump
--------------------------------------------------------------------------
The rebuild row builds both DBs ONCE from tests/fixtures/mini-dump/ + the
committed curated CSVs at data/db-export/ via the from-dump path run_rebuild,
with the seed-path constants AT THEIR DEFAULT (the point of the test: the default
must resolve db-export). Milliseconds, not the full dump.

ACCEPTANCE SIGNAL
-----------------
A headless path-constant + rebuild-resolution assertion (no engine, no game
launch). Emits the canonical acceptance signal (.claude/rules/acceptance-signal.md)
-- ACCEPT-RESULT per item + one ACCEPT-SUITE aggregate -- to stdout (the
data-core's DB-pipeline test sink), the same emit shape test_rebuild_from_csv.py
uses.

RUN
---
    python -m pytest data/refdata-extractor/tests/test_seed_dir_repoint.py -v
    python data/refdata-extractor/tests/test_seed_dir_repoint.py
"""
import os
import re
import shutil
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
MINI_DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "fixtures", "mini-dump", "refdata-1.5.1164953"))
IMPORTER_SRC = os.path.join(PYDIR, "import_to_sqlite.py")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402

SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")


# ---------------------------------------------------------------------------
# The assertions (each falsifiable; each maps to an ACCEPT-RESULT id).
# ---------------------------------------------------------------------------
def _run_assertions():
    """Returns a list of (acceptance_id, ok, detail)."""
    results = []

    def record(aid, ok, detail=""):
        results.append((aid, bool(ok), detail))

    # (1) SEED_DIR resolves db-export, NOT the retired seeds. The single
    # load-bearing repoint. FALSIFIABLE: a constant still '.../data/seeds' has
    # basename 'seeds' and fails here.
    seed_base = os.path.basename(imp.SEED_DIR.rstrip(os.sep))
    record("seed-dir-is-db-export", seed_base == "db-export",
           f"SEED_DIR basename is {seed_base!r} (want 'db-export'); SEED_DIR="
           f"{imp.SEED_DIR!r} -- a 'seeds' basename means the constant still "
           f"points at the retired data/seeds/")

    # (1b) The retired token must NOT be the resolved seed dir. A direct
    # falsification of the un-repointed state -- the resolved dir's parent/child
    # path must not be '.../data/seeds'.
    norm = os.path.normpath(imp.SEED_DIR).replace("\\", "/")
    record("seed-dir-not-retired-seeds", not norm.endswith("/data/seeds"),
           f"SEED_DIR={norm!r} still resolves the retired data/seeds/ dir")

    # (2) The three DERIVED constants follow SEED_DIR: each resolves UNDER
    # db-export with its curated filename unchanged. FALSIFIABLE: a derived
    # constant edited off SEED_DIR (a stray data/seeds literal on one of the
    # three) fails its row.
    derived = {
        "MODULE_SEED_CSV": imp.MODULE_SEED_CSV,
        "ADDRESS_NAMES_SEED_CSV": imp.ADDRESS_NAMES_SEED_CSV,
        "ADDRESS_VERSIONS_SEED_CSV": imp.ADDRESS_VERSIONS_SEED_CSV,
    }
    for name, p in derived.items():
        parent_base = os.path.basename(os.path.dirname(p.rstrip(os.sep)))
        in_export = parent_base == "db-export"
        not_seeds = not os.path.normpath(p).replace("\\", "/").endswith(
            "/data/seeds/" + os.path.basename(p))
        record(f"derived-under-db-export-{name}", in_export and not_seeds,
               f"{name}={p!r} parent={parent_base!r} (want 'db-export'); a "
               f"derived constant that drifted off SEED_DIR or kept a data/seeds "
               f"literal fails here")

    # (3) The curated CSVs the constants now point at actually EXIST at
    # db-export with the identical filenames -- the repoint resolves real files.
    for f in SEED_FILES:
        p = os.path.join(imp.SEED_DIR, f)
        record(f"curated-csv-present-{f}", os.path.isfile(p),
               f"{p!r} missing -- db-export must hold the three curated CSVs the "
               f"repointed constants resolve")

    # (4) THE RESOLUTION PROOF: a run_rebuild driven through the DEFAULT
    # constants (no repoint) resolves db-export and SUCCEEDS. If SEED_DIR still
    # pointed at the deleted data/seeds/, build_rows' read_module_seed raises
    # FileNotFoundError here (the exact pre-existing red). A green rebuild that
    # produces both DBs is the direct falsification of the un-repointed state.
    rebuilt_ok = False
    rebuild_detail = ""
    if not os.path.isdir(MINI_DUMP_DIR):
        rebuild_detail = (f"mini-dump not found: {MINI_DUMP_DIR}; this gate needs "
                          f"the committed mini-dump excerpt present")
    else:
        out_dir = tempfile.mkdtemp(prefix="seed_dir_repoint_")
        try:
            # Constants AT THEIR DEFAULT -- the whole point. No repoint wrapper.
            imp.run_rebuild(MINI_DUMP_DIR, out_dir)
            user_db = os.path.join(out_dir, "reference.sqlite")
            dev_db = os.path.join(out_dir, "reference-dev.sqlite")
            rebuilt_ok = (os.path.isfile(user_db) and os.path.getsize(user_db) > 0
                          and os.path.isfile(dev_db)
                          and os.path.getsize(dev_db) > 0)
            rebuild_detail = ("" if rebuilt_ok else
                              f"run_rebuild produced no/empty DBs at {out_dir}")
        except FileNotFoundError as e:
            rebuild_detail = (f"run_rebuild raised FileNotFoundError ({e}) -- a "
                              f"curated read still resolves the retired "
                              f"data/seeds/ (the constants were not repointed)")
        except Exception as e:  # surface any other failure as a FAIL, not a crash
            rebuild_detail = f"run_rebuild raised {type(e).__name__}: {e}"
        finally:
            shutil.rmtree(out_dir, ignore_errors=True)
    record("rebuild-through-default-constants-resolves-db-export",
           rebuilt_ok, rebuild_detail)

    # (5) No active data/seeds path-resolving literal remains in the constant
    # DEFINITION region of import_to_sqlite -- the assignment that decides the
    # default. Scans the SEED_DIR assignment line specifically (a comment naming
    # the retired dir is prose, NOT a path the toolchain resolves -- this asserts
    # the ASSIGNMENT, not the file-wide prose).
    with open(IMPORTER_SRC, encoding="utf-8") as f:
        src = f.read()
    # Match the active SEED_DIR assignment (ignore comments / docstrings).
    m = re.search(r'^\s*SEED_DIR\s*=\s*os\.path\.join\(([^)]*)\)',
                  src, re.MULTILINE)
    assign_ok = False
    assign_detail = "no active SEED_DIR = os.path.join(...) assignment found"
    if m:
        args = m.group(1)
        has_seeds = re.search(r'["\']seeds["\']', args) is not None
        has_db_export = re.search(r'["\']db-export["\']', args) is not None
        assign_ok = has_db_export and not has_seeds
        assign_detail = ("" if assign_ok else
                         f"SEED_DIR assignment args={args!r} -- must name "
                         f"'db-export' and NOT 'seeds'")
    record("no-active-seeds-literal-in-seed-dir-assignment",
           assign_ok, assign_detail)

    return results


def _emit_signal(results):
    """Emit the canonical acceptance signal (.claude/rules/acceptance-signal.md)."""
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


# ---------------------------------------------------------------------------
# pytest entry point
# ---------------------------------------------------------------------------
def test_seed_dir_repointed_to_db_export():
    """SEED_DIR + the three derived seed-path constants resolve data/db-export/
    (the D38 genesis), not the retired data/seeds/; a default-constant rebuild of
    the mini-dump resolves db-export and succeeds; no active data/seeds literal
    remains in the SEED_DIR assignment. Emits the ACCEPT signal."""
    results = _run_assertions()
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "seed-dir-repoint failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


if __name__ == "__main__":
    results = _run_assertions()
    _emit_signal(results)
    sys.exit(1 if any(not ok for _, ok, _ in results) else 0)
