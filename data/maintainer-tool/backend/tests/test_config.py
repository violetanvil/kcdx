"""test_config.py -- app.config path derivation (the out_dir DB-location regression).

WHAT THIS PROVES
----------------
config.out_dir -- the directory the data-core opens reference.sqlite from -- resolves to
<checkout>/data, the CANONICAL home of the two reference DBs, NOT <checkout>/data/seeds
(which holds the frozen bootstrap seed CSVs only; no .sqlite is there). This is the
REGRESSION TEST for the live-acceptance bug where out_dir pointed one level too deep
(data/seeds) and the backend reported "no DB loaded" though the DB exists: against the
old code these asserts FAIL; against the fix they PASS.

WHY THE BUG WENT UNCAUGHT
-------------------------
Every backend fixture (test_backend_skeleton / test_read_endpoints / test_save_endpoints /
test_confirm_endpoint) used to BUILD its reference DBs at <root>/data/seeds/ -- the same
wrong path config.out_dir returned -- so the fixture and the bug AGREED and the tests
passed. The fixtures encoded the bug; they have been corrected to build the DBs at
<root>/data/ (the real layout). This direct unit test pins out_dir itself, independent of
any fixture, so the path can never silently drift again.

The sibling path properties (seed_dir, db_export_dir) point at their OWN correct subdirs
and are asserted here to stay put (only out_dir was wrong).

RUN
---
    python -m pytest data/maintainer-tool/backend/tests/ -q
"""
import os
import sys

# --- locate the backend package (the skeleton test's pattern) -----------------------
HERE = os.path.dirname(os.path.abspath(__file__))
BACKEND_DIR = os.path.normpath(os.path.join(HERE, ".."))          # .../backend
sys.path.insert(0, BACKEND_DIR)

from app.config import (   # noqa: E402
    Config,
    USER_DB_NAME,
    DEV_DB_NAME,
    load_config,
)

# A platform-neutral fixed checkout path. Comparisons normpath both sides so the test is
# correct on Windows (\\) and POSIX (/) -- never a hardcoded separator.
CHECKOUT = os.path.normpath(os.path.abspath(os.path.join("X:", "fixture-checkout")))


def _cfg():
    """A Config pinned at the fixed checkout (no env / dev-default ambiguity)."""
    return Config(checkout_path=CHECKOUT, checkout_source="override")


def _norm(p):
    return os.path.normpath(p)


# ----------------------------------------------------------------------------
# out_dir resolves to <checkout>/data -- the DB home -- NOT <checkout>/data/seeds.
# (The regression: the old code returned data/seeds, one level too deep.)
# ----------------------------------------------------------------------------
def test_out_dir_is_checkout_data_not_data_seeds():
    cfg = _cfg()
    expected = os.path.join(CHECKOUT, "data")
    assert _norm(cfg.out_dir) == _norm(expected), \
        f"out_dir must be <checkout>/data, got {cfg.out_dir!r}"
    # The exact regression guard: out_dir is NOT one level too deep at data/seeds.
    wrong = os.path.join(CHECKOUT, "data", "seeds")
    assert _norm(cfg.out_dir) != _norm(wrong), \
        "out_dir regressed to data/seeds -- the DB does not live under data/seeds/"


# ----------------------------------------------------------------------------
# user_db / dev_db (derived from out_dir) resolve to the REAL DB paths.
# ----------------------------------------------------------------------------
def test_user_db_and_dev_db_resolve_under_data():
    cfg = _cfg()
    assert _norm(cfg.user_db) == _norm(os.path.join(CHECKOUT, "data", USER_DB_NAME))
    assert _norm(cfg.dev_db) == _norm(os.path.join(CHECKOUT, "data", DEV_DB_NAME))
    # Concretely the canonical filenames at data/ (reference.sqlite / reference-dev.sqlite).
    assert os.path.basename(cfg.user_db) == "reference.sqlite", cfg.user_db
    assert os.path.basename(cfg.dev_db) == "reference-dev.sqlite", cfg.dev_db
    assert _norm(os.path.dirname(cfg.user_db)) == _norm(os.path.join(CHECKOUT, "data"))


# ----------------------------------------------------------------------------
# The sibling path properties were CORRECT and stay put -- only out_dir was wrong.
# seed_dir -> data/seeds (the frozen bootstrap CSVs); db_export_dir -> data/db-export.
# ----------------------------------------------------------------------------
def test_seed_dir_and_db_export_dir_unchanged():
    cfg = _cfg()
    assert _norm(cfg.seed_dir) == _norm(os.path.join(CHECKOUT, "data", "seeds")), \
        "seed_dir must stay at data/seeds (the bootstrap CSVs live there)"
    assert _norm(cfg.db_export_dir) == _norm(os.path.join(CHECKOUT, "data", "db-export")), \
        "db_export_dir must stay at data/db-export (the derived export record, D20)"
    # out_dir is the PARENT of seed_dir (data/ vs data/seeds/) -- the relationship the
    # bug violated (it returned seed_dir itself).
    assert _norm(cfg.out_dir) == _norm(os.path.dirname(cfg.seed_dir)), \
        "out_dir must be the parent of seed_dir (data/ is data/seeds/'s parent)"


# ----------------------------------------------------------------------------
# The override path flows through load_config to the same out_dir derivation.
# ----------------------------------------------------------------------------
def test_load_config_override_resolves_out_dir_under_data():
    cfg = load_config(checkout_override=CHECKOUT)
    assert cfg.checkout_source == "override"
    assert _norm(cfg.out_dir) == _norm(os.path.join(os.path.abspath(CHECKOUT), "data"))
    assert os.path.basename(cfg.user_db) == "reference.sqlite"
