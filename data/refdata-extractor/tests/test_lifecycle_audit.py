"""test_lifecycle_audit.py -- the entity-lifecycle COMPLETENESS audit (design D41 /
policy.md S"Status is NOT an authored column" / s09-needs-action.md), maintainer-tool
lifecycle-completeness plan step 1.2.

WHAT THIS PROVES
----------------
`seeds_shared.lifecycle_audit.audit_lifecycle` computes the needs-action set at the
current game version V -- the three version-relative incomplete-lifecycle kinds the
write-time HARD-ERROR checks cannot catch. It READS the DB (READ-ONLY) + COMPUTES; it
WRITES NOTHING (the byte-identical DB is asserted). The cases (each crafted on a HIGH
kcdx_id outside the seeded 1-157 range so the baseline entities are untouched, and every
assertion is SCOPED to the crafted entities -- never a whole-DB total -- so pre-existing
baseline rows cannot pollute the verdict):

  POSITIVES -- each kind fires:
    1. UNCOVERED ORPHAN -- a closed interval ending BELOW V, no successor row covering V,
       is_deprecated=0, superseded_by NULL -> flagged in `uncovered`.
    2. NEVER VERIFIED -- a row with last_verified_at_version NULL -> flagged in
       `never_verified`.
    3. BROKEN REFERENCE -- an entity whose deprecation_replacement points at a NONEXISTENT
       kcdx_id -> flagged in `broken_refs`.

  HEALTHY -- the GOAL state:
    4. An OPEN interval covering V (valid_through NULL), verified, no dangling ref -> in
       NONE of the three sets.

  FALSIFIABLE NEGATIVES -- these MUST NOT be flagged (the conditions are PRECISE, not
  blanket):
    5. An OPEN interval (valid_through NULL, covers V) -> NOT in uncovered (open covers
       forward). FALSIFIABLE: if the orphan check ignored the open-row-covers-forward
       rule, this would flag.
    6. A DEPRECATED uncovered entity (is_deprecated=1, V >= deprecated_at) -> NOT in
       uncovered (deprecation is the completed lifecycle). FALSIFIABLE: dropping the
       is_deprecated guard flags it (the mutation check).
    7. A SUPERSEDED uncovered entity (superseded_by set, V >= superseded_at) -> NOT in
       uncovered (supersession is the completed lifecycle).

THE FIXTURE (no write path needed -- the audit READS)
-----------------------------------------------------
The audit is READ-ONLY, so the test builds the exact DB state by DIRECT sqlite injection
into a copy of the rebuilt baseline -- crafted address_names entities (with the lifecycle
+ reference columns set) and their address_versions intervals. This isolates the
read+compute logic from the write path. The byte-identical-DB assertion proves the audit
never writes.

ACCEPTANCE SIGNAL
-----------------
Emits the canonical ACCEPT-RESULT / ACCEPT-SUITE lines (.claude/rules/acceptance-signal.md)
for the headless data-core run -- the same _emit_signal shape test_reverify_resolver uses.

RUN
---
    python tests/test_lifecycle_audit.py
    pytest tests/test_lifecycle_audit.py
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
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import lifecycle_audit as la  # noqa: E402

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953", ordinal 1164953
# A LATER game version that is the CURRENT version V the audit measures at (ordinal
# strictly > GVT). Injected as the max-ordinal row so audit_lifecycle(None) picks it as V.
LATER_TAG = "1.6.2000000"
LATER_ORDINAL = 2000000
# A MIDDLE version between GVT and LATER (for a closed interval ending BELOW V).
MID_TAG = "1.5.1500000"
MID_ORDINAL = 1500000


# --------------------------------------------------------------------------
# Baseline rebuild (single-version GVT DB), then DIRECT-inject the crafted state.
# --------------------------------------------------------------------------
def _have_inputs():
    return os.path.isdir(DUMP_DIR)


def _rebuild_baseline(out_dir):
    os.makedirs(out_dir, exist_ok=True)
    saved = (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
             imp.ADDRESS_VERSIONS_SEED_CSV)
    imp.MODULE_SEED_CSV = os.path.join(REAL_SEED_DIR, "module_seed.csv")
    imp.ADDRESS_NAMES_SEED_CSV = os.path.join(REAL_SEED_DIR, "address_names_seed.csv")
    imp.ADDRESS_VERSIONS_SEED_CSV = os.path.join(REAL_SEED_DIR,
                                                 "address_versions_seed.csv")
    try:
        imp.run_rebuild(DUMP_DIR, out_dir)
    finally:
        (imp.MODULE_SEED_CSV, imp.ADDRESS_NAMES_SEED_CSV,
         imp.ADDRESS_VERSIONS_SEED_CSV) = saved


def _gv_id(con, tag):
    row = con.execute("SELECT id FROM game_versions WHERE tag = ?", (tag,)).fetchone()
    return row[0] if row else None


def _inject_state(out_dir):
    """Direct-inject the crafted DB state into the USER reference.sqlite: the later +
    middle game_versions rows, and crafted address_names entities (with the lifecycle +
    reference columns set) + their address_versions intervals, all on fresh HIGH kcdx_ids
    (so the seeded 1-157 entities are untouched). Returns a dict of the injected kcdx_ids
    each case keys on. NEVER hardcodes an id -- reads them back."""
    db_path = os.path.join(out_dir, "reference.sqlite")
    con = sqlite3.connect(db_path)
    try:
        gvt_id = _gv_id(con, GVT)
        assert gvt_id is not None
        # Register the MIDDLE + LATER game versions. LATER is the max ordinal -> the V
        # audit_lifecycle(None) measures at.
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (MID_TAG, MID_ORDINAL))
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (LATER_TAG, LATER_ORDINAL))
        mid_id = _gv_id(con, MID_TAG)
        later_id = _gv_id(con, LATER_TAG)
        assert None not in (mid_id, later_id)

        names_max = con.execute("SELECT MAX(id) FROM address_names").fetchone()[0] or 0
        avmax = con.execute("SELECT MAX(id) FROM address_versions").fetchone()[0] or 0
        module_id = con.execute("SELECT id FROM modules LIMIT 1").fetchone()[0]
        kind_id = con.execute(
            'SELECT id FROM "_dict_address_versions_kind" WHERE val = ?',
            ("function",)).fetchone()[0]

        def _add_entity(name, *, is_deprecated=0, deprecated_at=None,
                        superseded_by=None, superseded_at=None,
                        deprecation_replacement=None):
            """A crafted address_names entity with the lifecycle + reference columns set.
            Extends the seed range (HIGH id) so the baseline 1-157 entities are untouched."""
            nonlocal names_max
            names_max += 1
            con.execute(
                "INSERT INTO address_names "
                "(id, name, is_deprecated, deprecated_at_version, superseded_by, "
                " superseded_at_version, deprecation_replacement) "
                "VALUES (?, ?, ?, ?, ?, ?, ?)",
                (names_max, name, is_deprecated, deprecated_at, superseded_by,
                 superseded_at, deprecation_replacement))
            return names_max

        def _add_av(kcdx_id, valid_from_id, valid_through_id, last_verified_id):
            """One address_versions interval for a crafted entity. last_verified_id may be
            None (the never-verified case)."""
            nonlocal avmax
            avmax += 1
            con.execute(
                "INSERT INTO address_versions "
                "(id, kcdx_id, kind, module_id, rva, valid_from, valid_through, "
                " last_verified_at_version) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?)",
                (avmax, kcdx_id, kind_id, module_id, 0x1000 + avmax,
                 valid_from_id, valid_through_id, last_verified_id))
            return avmax

        # (1) UNCOVERED ORPHAN -- a CLOSED interval [GVT..MID] ending BELOW V (=LATER),
        #     no successor row covering V, not deprecated, not superseded. -> uncovered.
        e_orphan = _add_entity("kcdx_test_orphan")
        _add_av(e_orphan, gvt_id, mid_id, mid_id)

        # (2) NEVER VERIFIED -- an OPEN interval (covers V, so NOT an orphan) with
        #     last_verified NULL. -> never_verified (and NOT uncovered: it is covered).
        e_never = _add_entity("kcdx_test_never_verified")
        _add_av(e_never, gvt_id, None, None)

        # (3) BROKEN REFERENCE -- an entity covered at V (so NOT an orphan itself) whose
        #     deprecation_replacement points at a NONEXISTENT kcdx_id. -> broken_refs.
        nonexistent_target = names_max + 9999  # never inserted
        e_broken = _add_entity("kcdx_test_broken_ref",
                               deprecation_replacement=nonexistent_target)
        _add_av(e_broken, gvt_id, None, later_id)

        # (4) HEALTHY -- an OPEN interval covering V, verified at V, no dangling ref. ->
        #     in NONE of the three sets (the GOAL state).
        e_healthy = _add_entity("kcdx_test_healthy")
        _add_av(e_healthy, gvt_id, None, later_id)

        # (5) OPEN-INTERVAL NEGATIVE -- an OPEN interval (valid_through NULL) covering V,
        #     verified. -> NOT in uncovered (open covers forward). (Same shape as healthy
        #     but asserted specifically against the open-covers-forward rule.)
        e_open = _add_entity("kcdx_test_open_covered")
        _add_av(e_open, gvt_id, None, later_id)

        # (6) DEPRECATED uncovered NEGATIVE -- a CLOSED interval ending below V (uncovered)
        #     BUT is_deprecated=1 with deprecated_at <= V. -> NOT in uncovered (the
        #     completed-lifecycle exclusion). The mutation-check target.
        e_dep = _add_entity("kcdx_test_deprecated_uncovered",
                            is_deprecated=1, deprecated_at=mid_id)
        _add_av(e_dep, gvt_id, mid_id, mid_id)

        # (7) SUPERSEDED uncovered NEGATIVE -- a CLOSED interval ending below V (uncovered)
        #     BUT superseded_by set with superseded_at <= V. -> NOT in uncovered. The
        #     successor points at the healthy entity (an existing, covered target, so the
        #     supersession itself is NOT a broken ref).
        e_sup = _add_entity("kcdx_test_superseded_uncovered",
                            superseded_by=e_healthy, superseded_at=mid_id)
        _add_av(e_sup, gvt_id, mid_id, mid_id)

        con.commit()
        return {
            "mid_id": mid_id, "later_id": later_id,
            "e_orphan": e_orphan, "e_never": e_never, "e_broken": e_broken,
            "nonexistent_target": nonexistent_target,
            "e_healthy": e_healthy, "e_open": e_open,
            "e_dep": e_dep, "e_sup": e_sup,
        }
    finally:
        con.close()


_BASELINE = {}


def _get_fixture():
    if "out" not in _BASELINE:
        root = tempfile.mkdtemp(prefix="lifecycle_base_")
        out = os.path.join(root, "rebuild")
        _rebuild_baseline(out)
        ids = _inject_state(out)
        _BASELINE.update({"root": root, "out": out, "ids": ids})
    return _BASELINE


def _cleanup():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _db_hash(out_dir):
    """A content hash over the USER + DEV reference DB files -- the no-write proof."""
    h = hashlib.sha256()
    for name in ("reference.sqlite", "reference-dev.sqlite"):
        p = os.path.join(out_dir, name)
        if os.path.isfile(p):
            with open(p, "rb") as f:
                h.update(f.read())
    return h.hexdigest()


def _kids(entries):
    """The set of kcdx_ids in a result list (the audit returns entity-keyed dicts)."""
    return {e["kcdx_id"] for e in entries}


# ==========================================================================
# The cases -- each returns a (id, ok, detail) result for the ACCEPT signal.
# ==========================================================================
def _run_cases(out_dir, ids):
    results = []
    db_before = _db_hash(out_dir)

    # V defaults to the max-ordinal row (LATER) -- the current version.
    out = la.audit_lifecycle(out_dir)
    uncovered = _kids(out["uncovered"])
    never = _kids(out["never_verified"])
    broken = _kids(out["broken_refs"])

    # The audit measured at the LATER tag (the injected max-ordinal version).
    results.append((
        "version-defaults-to-max-ordinal-current",
        out["version"] == LATER_TAG and out["version_ordinal"] == LATER_ORDINAL,
        f"version={out['version']!r} ordinal={out['version_ordinal']!r}, "
        f"expected {LATER_TAG} / {LATER_ORDINAL}"))

    # (1) the uncovered orphan is flagged.
    results.append((
        "uncovered-orphan-flagged",
        ids["e_orphan"] in uncovered,
        f"orphan kcdx_id {ids['e_orphan']} not in uncovered={sorted(uncovered)}"))

    # (2) the never-verified row is flagged.
    results.append((
        "never-verified-flagged",
        ids["e_never"] in never,
        f"never-verified kcdx_id {ids['e_never']} not in never_verified={sorted(never)}"))

    # (3) the broken reference (nonexistent target) is flagged, naming the field + target.
    broken_match = next(
        (e for e in out["broken_refs"] if e["kcdx_id"] == ids["e_broken"]), None)
    results.append((
        "broken-ref-nonexistent-target-flagged",
        broken_match is not None
        and broken_match["field"] == "deprecation_replacement"
        and broken_match["target_kcdx_id"] == ids["nonexistent_target"],
        f"broken-ref entry for kcdx_id {ids['e_broken']} = {broken_match!r} "
        f"(expected field=deprecation_replacement target={ids['nonexistent_target']})"))

    # (4) the HEALTHY entity is in NONE of the three sets.
    e_h = ids["e_healthy"]
    results.append((
        "healthy-entity-in-no-set",
        e_h not in uncovered and e_h not in never and e_h not in broken,
        f"healthy kcdx_id {e_h} appeared in a needs-action set "
        f"(uncovered={e_h in uncovered}, never={e_h in never}, broken={e_h in broken})"))

    # (5) FALSIFIABLE: the OPEN interval covering V is NOT in uncovered (open covers fwd).
    results.append((
        "open-interval-not-uncovered",
        ids["e_open"] not in uncovered,
        f"open-covered kcdx_id {ids['e_open']} WRONGLY flagged uncovered "
        f"(open covers forward -- the open-row rule was not applied)"))

    # (6) FALSIFIABLE (mutation target): the DEPRECATED uncovered entity is NOT in
    #     uncovered (deprecation is the completed lifecycle). Dropping the is_deprecated
    #     guard makes this go RED.
    results.append((
        "deprecated-uncovered-not-flagged",
        ids["e_dep"] not in uncovered,
        f"deprecated uncovered kcdx_id {ids['e_dep']} WRONGLY flagged uncovered "
        f"(deprecation is the completed lifecycle -- the is_deprecated guard is gone)"))

    # (7) FALSIFIABLE: the SUPERSEDED uncovered entity is NOT in uncovered (supersession
    #     is the completed lifecycle).
    results.append((
        "superseded-uncovered-not-flagged",
        ids["e_sup"] not in uncovered,
        f"superseded uncovered kcdx_id {ids['e_sup']} WRONGLY flagged uncovered "
        f"(supersession is the completed lifecycle -- the superseded_by guard is gone)"))

    # The supersession successor (-> e_healthy, an existing covered entity) is NOT a broken
    # ref -- proves the broken-ref check does not over-flag a VALID redirect.
    results.append((
        "valid-supersession-target-not-broken",
        ids["e_sup"] not in broken,
        f"valid supersession kcdx_id {ids['e_sup']} WRONGLY flagged broken "
        f"(its successor {ids['e_healthy']} exists and is covered -- a usable redirect)"))

    # NO-WRITE: the DB is byte-identical after the audit (READ-ONLY).
    after = la.audit_lifecycle(out_dir)  # one more resolve -- still read-only
    _ = after
    results.append((
        "audit-writes-nothing-db-byte-identical",
        _db_hash(out_dir) == db_before,
        "the audit MUTATED the DB across a resolve (it must be READ-ONLY)"))

    return results


# --------------------------------------------------------------------------
# Acceptance signal (.claude/rules/acceptance-signal.md).
# --------------------------------------------------------------------------
def _emit_signal(results):
    passed = sum(1 for _, ok, _ in results if ok)
    total = len(results)
    for aid, ok, detail in results:
        verdict = "PASS" if ok else "FAIL"
        suffix = f" -- {detail}" if (not ok and detail) else ""
        print(f"ACCEPT-RESULT: {verdict} {aid}{suffix}")
    print(f"ACCEPT-SUITE: {passed}/{total} passing")


# --------------------------------------------------------------------------
# pytest entry point (one test drives all cases over the one fixture; emits the
# canonical signal + asserts no failure).
# --------------------------------------------------------------------------
try:
    import pytest

    @pytest.fixture(scope="module")
    def fixture():
        if not _have_inputs():
            pytest.skip(f"mini-dump fixture not found (dump={DUMP_DIR})")
        b = _get_fixture()
        yield b
        _cleanup()
except ImportError:   # pragma: no cover - __main__ runner without pytest
    pytest = None


def test_lifecycle_audit(fixture):   # noqa: F811
    """The audit's three-kind detection over the crafted fixture DB. Emits the canonical
    ACCEPT signal; asserts every case PASSED."""
    results = _run_cases(fixture["out"], fixture["ids"])
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "lifecycle_audit failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


if __name__ == "__main__":
    if not _have_inputs():
        print(f"SKIP (no fixture): mini-dump not found (dump={DUMP_DIR})")
        sys.exit(0)
    try:
        b = _get_fixture()
        res = _run_cases(b["out"], b["ids"])
        _emit_signal(res)
        sys.exit(0 if all(ok for _, ok, _ in res) else 1)
    finally:
        _cleanup()
