"""test_db_editor_interval.py -- the D40 valid_through interval-edit write path +
the AUTHORED-CLOSED interval validator (maintainer-tool Phase 6, step 6.2a-fix).

WHAT THIS PROVES
----------------
D40 makes `valid_through` an AUTHORED + tool-auto-filled column (the interval-CLOSE
column, moved off the bulk derived overlay onto the curated seed as
`valid_through_version`). This test exercises the REAL write path end-to-end through
the batch confirm seam + the REAL interval validator -- no wrapper, no stubbed gate:

  1. EXTEND lands (batch path): an OPEN v1.5 row's valid_through is set to a LATER
     version tag via update_version_rows_batch({valid_through_version: <later tag>})
     -> the row's valid_through holds that LATER ordinal after commit (the interval
     was EXTENDED to close at the later version). FALSIFIABLE: the pre-fix write path
     could not emit valid_through (the trio-only UPDATE / US-5 exclusion), so the row
     stayed OPEN -> this assertion fails.

  2. CLOSE lands (batch path): an OPEN v1.5 row's valid_through is set to its own
     baseline version (= last_verified_at_version) via the batch path -> the row's
     valid_through holds that ordinal after commit (the interval is CLOSED at its own
     version, valid_through == valid_from). FALSIFIABLE: same as (1) -- a write path
     that cannot emit valid_through leaves the row OPEN.

  3. US-5 full-column UPDATE PRESERVES a closed valid_through (the
     _UPDATE_PRESERVE_COLUMNS reconciliation): a CLOSED row gets a US-5 full-column
     edit (rva) through the batch path; its valid_through is UNCHANGED after commit --
     the full-column UPDATE never re-opens (NULLs) a closed interval. FALSIFIABLE BY
     DESIGN: if the US-5 path emitted valid_through=None (the build_curated_row mint),
     the closed interval would be NULLED -> a 2nd open row -> this assertion (the
     valid_through-unchanged check) fails. This is the exact KI-0007-class collision
     _UPDATE_PRESERVE_COLUMNS guards, asserted for the D40 interval column.

  4. The interval validator ACCEPTS legal + REJECTS illegal (AUTHORED-CLOSED scope):
     check_address_version_intervals (the shared seed gate) over crafted seed-row
     dicts --
       (a) ACCEPTS a legal CLOSED interval (valid_through >= valid_from, a known tag);
       (b) REJECTS valid_through < valid_from (the close before the open) -- a loud
           RuntimeError naming the entity (AP14);
       (c) REJECTS an unknown valid_through_version tag (FK closure) -- loud, named;
       (d) ACCEPTS a 2-OPEN create-version transient (two open rows for one entity),
           because open-row uniqueness is the DB index + write-time close, NOT this
           seed-only gate (the 6.2a-fix scoping). FALSIFIABLE: if the validator still
           fronted open-row uniqueness, the transient would raise -> this fails;
       (e) REJECTS two OVERLAPPING CLOSED intervals -- loud, named.
     The scoping is FALSIFIABLE both ways: (b)/(c)/(e) prove the kept checks still
     fire (not weakened); (d) proves the dropped open-row check no longer over-reaches.

REAL EVERYTHING; reuses the apply-oracle baseline (a module-scoped rebuild from the
committed seeds, copied per-case). Skips gracefully if the mini-dump / DLL is absent.
The validator cases (4) need no fixture -- they run on in-memory seed-row dicts.

ACCEPTANCE SIGNAL
-----------------
Emits the canonical ACCEPT-RESULT / ACCEPT-SUITE lines
(.claude/rules/acceptance-signal.md) for the headless data-core run, via the shared
_emit_signal helper shape (the same one test_round_trip / test_bulk_exporter use).

RUN
---
    python tests/test_db_editor_interval.py
    pytest tests/test_db_editor_interval.py
"""
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
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "db-export")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402
from seeds_shared import db_editor  # noqa: E402
from seeds_shared import check_address_version_intervals  # noqa: E402
from seeds_shared import check_live_entity_has_open_interval  # noqa: E402

GVT = imp.GAME_VERSION_TAG   # "1.5.1164953"
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")
# A LATER game-version tag for the EXTEND case + the create_version setup. Lexically
# sorts AFTER GVT (the validator's ordinal compare is lexicographic over release tags).
LATER_TAG = "1.6.2000000"


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


def _gv_id(con, tag):
    row = con.execute("SELECT id FROM game_versions WHERE tag = ?", (tag,)).fetchone()
    return row[0] if row else None


def _valid_through_id(db_path, kcdx_id, valid_from_tag):
    """The raw valid_through id of the (kcdx_id, valid_from) row -- None = OPEN. A raw
    id (NOT tag-normalized) so a clobber-to-NULL is directly observable."""
    con = sqlite3.connect(db_path)
    try:
        vf = _gv_id(con, valid_from_tag)
        row = con.execute(
            "SELECT valid_through FROM address_versions WHERE kcdx_id = ? "
            "AND valid_from = ?", (kcdx_id, vf)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _rva_of(db_path, kcdx_id, valid_from_tag):
    con = sqlite3.connect(db_path)
    try:
        vf = _gv_id(con, valid_from_tag)
        row = con.execute(
            "SELECT rva FROM address_versions WHERE kcdx_id = ? AND valid_from = ?",
            (kcdx_id, vf)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _pick_function_trio_rows(db_path, n):
    """The first `n` curated FUNCTION-kind rows that each carry a full audit trio +
    an rva. Returns [(kcdx_id, valid_from_tag)]. Never a hardcoded id."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        out = []
        for kid, vf, kindid in con.execute(
                "SELECT kcdx_id, valid_from, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL AND last_verified_at_version IS NOT NULL "
                "AND rva IS NOT NULL ORDER BY kcdx_id"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                out.append((kid, gv.get(vf)))
                if len(out) == n:
                    break
        return out
    finally:
        con.close()


def _pick_nonfunction_row(db_path, *, exclude=()):
    """A curated NON-function entity (create_version on a function row hits the
    function-kind baseline gate -- an unrelated path). Returns (kcdx_id, vf_tag)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, kindid in con.execute(
                "SELECT kcdx_id, valid_from, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL ORDER BY kcdx_id"):
            if kid in exclude:
                continue
            if kdec.get(kindid) not in ("function", "function_variadic",
                                        "function_no_sig"):
                return (kid, gv.get(vf))
        return None
    finally:
        con.close()


def _seed_source_row_for_create(user_db, kid, vf_tag):
    """The v1.5 row's authored cells (the create_version prefill) read from the exported
    seed, with the audit trio NULLed (a brand-new unverified version) -- the same shape
    the update oracle uses, so a create_version at LATER_TAG is apply-valid."""
    from seeds_shared.csv_exporter import ADDRESS_VERSIONS_SEED_NAME
    from seeds_shared.csv_exporter import export_seeds as _export_seeds
    exp = tempfile.mkdtemp(prefix="interval_src_")
    try:
        _export_seeds(user_db, exp)
        import csv as _csv
        with open(os.path.join(exp, ADDRESS_VERSIONS_SEED_NAME),
                  newline="", encoding="utf-8") as f:
            lines = [ln for ln in f if not ln.lstrip().startswith("#")]
            src = None
            for r in _csv.DictReader(lines):
                if ((r.get("kcdx_id") or "").strip() == str(kid)
                        and (r.get("valid_from_version") or "").strip() == vf_tag):
                    src = {k: v for k, v in r.items()
                           if k not in ("kcdx_id", "valid_from_version")}
                    break
        assert src is not None, "could not read the source seed row"
    finally:
        shutil.rmtree(exp, ignore_errors=True)
    for _c in ("last_verified_at_version", "verified_by", "verified_date",
               "evidence_kind", "valid_through_version"):
        if _c in src:
            src[_c] = ""
    return src


# --------------------------------------------------------------------------
# Module-scoped baseline (built ONCE, copied per-case).
# --------------------------------------------------------------------------
_BASELINE = {}


def _have_inputs():
    return os.path.isdir(DUMP_DIR) and os.path.isfile(DLL_PATH)


def _get_baseline():
    if "root" not in _BASELINE:
        root = tempfile.mkdtemp(prefix="interval_base_")
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
    out = tempfile.mkdtemp(prefix="interval_run_")
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
# Case 1 + 2: EXTEND and CLOSE land through the batch path.
# --------------------------------------------------------------------------
def _extend_and_close_land(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")

        # Need the LATER_TAG game_versions row to exist for the EXTEND. create_version
        # a 2nd version on entity A at LATER_TAG -> registers LATER_TAG + adds the new
        # OPEN v1.6 row (and closes A's v1.5 row). After this, both tags exist.
        anchor, anchor_vf = _pick_nonfunction_row(user_db)
        db_editor.create_version(
            out, DLL_PATH, anchor, LATER_TAG,
            _seed_source_row_for_create(user_db, anchor, anchor_vf))

        # Pick TWO MORE distinct curated entities (still OPEN at v1.5) for EXTEND + CLOSE.
        ext = _pick_nonfunction_row(user_db, exclude=(anchor,))
        assert ext is not None, "fixture lacks a 2nd curated non-function entity"
        ext_kid, ext_vf = ext
        clo = _pick_nonfunction_row(user_db, exclude=(anchor, ext_kid))
        # If the fixture has only 2 non-function entities, reuse a function-trio row for
        # CLOSE (CLOSE at its OWN version does not hit the create_version baseline gate).
        if clo is None:
            fr = _pick_function_trio_rows(user_db, 1)
            assert fr, "fixture lacks a 3rd editable curated entity for CLOSE"
            clo_kid, clo_vf = fr[0]
        else:
            clo_kid, clo_vf = clo

        # Pre-state: both targets OPEN (valid_through NULL).
        assert _valid_through_id(user_db, ext_kid, ext_vf) is None, "EXTEND target not open"
        assert _valid_through_id(user_db, clo_kid, clo_vf) is None, "CLOSE target not open"

        # KI-0025: fully closing an entity's SOLE interval leaves it with no open row.
        # The standing invariant `check_live_entity_has_open_interval` (correctly) rejects
        # that for a LIVE entity -- the only LEGAL way to close a last interval is to
        # retire the entity. So deprecate ext + clo first (a deprecated entity is EXEMPT
        # from the open-interval requirement). This models the real legal path AND keeps
        # the assertion narrow: does the batch write path emit `valid_through`? Deprecate
        # at GVT (always-registered) -- the deprecation tag is independent of WHERE the
        # interval closes (EXTEND->LATER, CLOSE->GVT); only the exemption matters here.
        db_editor.deprecate_entity(
            out, DLL_PATH, ext_kid, is_deprecated=True, deprecated_at_version=GVT)
        db_editor.deprecate_entity(
            out, DLL_PATH, clo_kid, is_deprecated=True, deprecated_at_version=GVT)

        from seeds_shared import commit
        # EXTEND ext -> close at LATER_TAG (valid_through = LATER > valid_from = v1.5).
        # CLOSE clo -> close at its OWN version GVT (valid_through == valid_from).
        specs = [
            {"kcdx_id": ext_kid, "valid_from_version": ext_vf,
             "edits": {"valid_through_version": LATER_TAG}},
            {"kcdx_id": clo_kid, "valid_from_version": clo_vf,
             "edits": {"valid_through_version": GVT}},
        ]
        handle = db_editor.update_version_rows_batch(
            out, DLL_PATH, specs, defer_commit=True)
        commit(handle)

        for label, dbp in (("user", user_db), ("dev", dev_db)):
            con = sqlite3.connect(dbp)
            try:
                later_id = _gv_id(con, LATER_TAG)
                gvt_id = _gv_id(con, GVT)
            finally:
                con.close()
            # (1) EXTEND persisted: ext's valid_through is the LATER ordinal (not NULL).
            ext_vt = _valid_through_id(dbp, ext_kid, ext_vf)
            assert ext_vt == later_id, (
                f"[{label}] EXTEND did not persist: valid_through={ext_vt!r}, expected "
                f"the {LATER_TAG} ordinal {later_id!r} (the write path did not emit "
                f"valid_through -- D40 interval edit dropped)")
            # (2) CLOSE persisted: clo's valid_through is the GVT ordinal (closed at own).
            clo_vt = _valid_through_id(dbp, clo_kid, clo_vf)
            assert clo_vt == gvt_id, (
                f"[{label}] CLOSE did not persist: valid_through={clo_vt!r}, expected "
                f"the {GVT} ordinal {gvt_id!r} (the interval was not closed)")
        return True
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: a US-5 full-column UPDATE PRESERVES a closed valid_through.
# --------------------------------------------------------------------------
def _us5_preserves_closed_valid_through(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")

        # Build a CLOSED row: create_version a 2nd version at LATER_TAG on a curated
        # entity -> the v1.5 row is CLOSED (valid_through = its own ordinal).
        kid, vf = _pick_nonfunction_row(user_db)
        db_editor.create_version(
            out, DLL_PATH, kid, LATER_TAG,
            _seed_source_row_for_create(user_db, kid, vf))
        before_vt = _valid_through_id(user_db, kid, vf)
        assert before_vt is not None, (
            "setup precondition failed: the v1.5 row is not CLOSED after create_version")
        old_rva = _rva_of(user_db, kid, vf)

        # US-5 full-column edit (rva) on the CLOSED v1.5 row through the batch path.
        from seeds_shared import commit
        new_rva = (old_rva or 0) + 0x40
        handle = db_editor.update_version_rows_batch(
            out, DLL_PATH,
            [{"kcdx_id": kid, "valid_from_version": vf,
              "edits": {"rva": "0x%X" % new_rva}}],
            defer_commit=True)
        commit(handle)

        for label, dbp in (("user", user_db), ("dev", dev_db)):
            # THE LOAD-BEARING ASSERTION: valid_through is UNCHANGED -- the US-5
            # full-column UPDATE did NOT re-open (NULL) the closed interval
            # (_UPDATE_PRESERVE_COLUMNS). FALSIFIABLE: a US-5 path emitting
            # valid_through=None would NULL it -> this fails.
            after_vt = _valid_through_id(dbp, kid, vf)
            assert after_vt == before_vt, (
                f"[{label}] a US-5 full-column edit CLOBBERED a closed valid_through: "
                f"{before_vt!r} -> {after_vt!r} (the _UPDATE_PRESERVE_COLUMNS "
                f"reconciliation regressed -- the closed interval was re-opened)")
            assert after_vt is not None, (
                f"[{label}] the closed interval is OPEN after the US-5 edit (NULLed)")
            # The edit itself applied (the rva changed) -- not a no-op masking the check.
            assert _rva_of(dbp, kid, vf) == new_rva, (
                f"[{label}] the US-5 rva edit did not apply (expected {new_rva!r}, got "
                f"{_rva_of(dbp, kid, vf)!r})")
        return True
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 4: the interval validator -- ACCEPTS legal, REJECTS illegal (AUTHORED-CLOSED
# scope). Pure in-memory seed-row dicts; no fixture needed.
# --------------------------------------------------------------------------
def _vrow(kid, vf, vt=""):
    """A minimal versions-seed row dict the interval validator reads (kcdx_id,
    valid_from_version, valid_through_version)."""
    return {"kcdx_id": str(kid),
            "valid_from_version": vf,
            "valid_through_version": vt}


def _validator_accepts_and_rejects():
    results = []

    # (a) ACCEPTS a legal CLOSED interval (valid_through >= valid_from, a known tag).
    # The seed describes both tags (each is some row's valid_from), so the FK closes.
    legal = [_vrow(1, "1.5.1164953", "1.6.2000000"),
             _vrow(2, "1.6.2000000", "")]   # entity 2 supplies the close tag + is open
    try:
        check_address_version_intervals(legal)
        results.append(("interval-validator-accepts-legal-closed", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append(("interval-validator-accepts-legal-closed", False,
                        f"a legal closed interval was rejected: {e}"))

    # (b) REJECTS valid_through < valid_from (the close before the open).
    backwards = [_vrow(1, "1.6.2000000", "1.5.1164953"),
                 _vrow(2, "1.5.1164953", "")]
    raised = _expect_raise(check_address_version_intervals, backwards)
    results.append((
        "interval-validator-rejects-backwards",
        raised is not None and "1" in str(raised),  # AP14: names the entity
        "" if raised is not None else
        "valid_through < valid_from was ACCEPTED (the ordering check is gone)"))

    # (c) REJECTS an unknown valid_through_version tag (FK closure over the seed).
    unknown = [_vrow(1, "1.5.1164953", "9.9.9999999")]   # 9.9.* is no row's valid_from
    raised = _expect_raise(check_address_version_intervals, unknown)
    results.append((
        "interval-validator-rejects-unknown-tag",
        raised is not None and "9.9.9999999" in str(raised),  # AP14: names the bad tag
        "" if raised is not None else
        "an unknown valid_through tag was ACCEPTED (the FK closure is gone)"))

    # (d) ACCEPTS a 2-OPEN create-version transient (two open rows for one entity).
    # This is the 6.2a-fix SCOPING: open-row uniqueness is the DB index + write-time
    # close, NOT this seed-only gate. FALSIFIABLE: if the validator still fronted
    # open-row uniqueness, this raises.
    two_open = [_vrow(1, "1.5.1164953", ""),
                _vrow(1, "1.6.2000000", "")]   # SAME entity, BOTH open
    try:
        check_address_version_intervals(two_open)
        results.append(("interval-validator-accepts-2open-transient", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append((
            "interval-validator-accepts-2open-transient", False,
            f"a 2-open create-version transient was REJECTED (the dropped open-row "
            f"check still over-reaches): {e}"))

    # (e) REJECTS two OVERLAPPING CLOSED intervals of one entity.
    # [1.5..1.7] and [1.6..1.8] overlap (1.6 falls inside the first). All four tags are
    # some row's valid_from, so each FK closes -- the overlap is the sole defect.
    overlap = [_vrow(1, "1.5.1164953", "1.7.3000000"),
               _vrow(1, "1.6.2000000", "1.8.4000000"),
               _vrow(2, "1.7.3000000", ""),
               _vrow(3, "1.8.4000000", "")]
    raised = _expect_raise(check_address_version_intervals, overlap)
    results.append((
        "interval-validator-rejects-overlapping-closed",
        raised is not None and "overlap" in str(raised).lower(),
        "" if raised is not None else
        "two overlapping CLOSED intervals were ACCEPTED (the overlap check is gone)"))

    return results


# --------------------------------------------------------------------------
# Case 4b: the LIVE-entity-has-open-interval validator (KI-0025) -- a live entity
# must keep >=1 open interval; a deprecated/superseded entity is EXEMPT. Pure
# in-memory seed-row dicts; no fixture needed.
# --------------------------------------------------------------------------
def _nrow(kid, *, is_deprecated=False, superseded_by=""):
    """A minimal names-seed row dict the live-entity check reads (id, the
    deprecation + supersession status). name is required by the reader shape."""
    return {"id": str(kid),
            "name": f"entity_{kid}",
            "is_deprecated": "1" if is_deprecated else "",
            "superseded_by": superseded_by}


def _live_open_interval_validator_accepts_and_rejects():
    results = []

    # (a) ACCEPTS a live entity WITH an open interval (the normal current form).
    v = [_vrow(1, "1.5.1164953", "")]
    n = [_nrow(1)]
    try:
        check_live_entity_has_open_interval(v, n)
        results.append(("live-open-accepts-live-with-open", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append(("live-open-accepts-live-with-open", False,
                        f"a live entity with an open interval was rejected: {e}"))

    # (b) REJECTS a LIVE entity whose ONLY row is CLOSED (the exact KI-0025 state:
    # kcdx_id=12's sole row closed at its own version -> no current form). AP14: names
    # the entity.
    v = [_vrow(12, "1.5.1164953", "1.5.1164953")]   # closed at its own version
    n = [_nrow(12)]                                  # live: not deprecated/superseded
    raised = _expect_raise(check_live_entity_has_open_interval, v, n)
    results.append((
        "live-open-rejects-live-closed-only",
        raised is not None and "12" in str(raised),
        "" if raised is not None else
        "a live entity with ONLY a closed interval was ACCEPTED (KI-0025 recurs)"))

    # (c) ACCEPTS a DEPRECATED entity with only a closed interval (EXEMPT -- a retired
    # entity legitimately has no current form). FALSIFIABLE: if the check ignored
    # deprecation, this would raise.
    v = [_vrow(5, "1.5.1164953", "1.5.1164953")]
    n = [_nrow(5, is_deprecated=True)]
    try:
        check_live_entity_has_open_interval(v, n)
        results.append(("live-open-accepts-deprecated-closed-only", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append(("live-open-accepts-deprecated-closed-only", False,
                        f"a deprecated entity with no open interval was rejected "
                        f"(the deprecation exemption is gone): {e}"))

    # (d) ACCEPTS a SUPERSEDED entity with only a closed interval (EXEMPT -- same as
    # deprecated: a superseded entity's current form moved to its successor).
    v = [_vrow(6, "1.5.1164953", "1.5.1164953")]
    n = [_nrow(6, superseded_by="entity_7")]
    try:
        check_live_entity_has_open_interval(v, n)
        results.append(("live-open-accepts-superseded-closed-only", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append(("live-open-accepts-superseded-closed-only", False,
                        f"a superseded entity with no open interval was rejected "
                        f"(the supersession exemption is gone): {e}"))

    # (e) ACCEPTS a live entity with a CLOSED past interval AND an OPEN current one
    # (the normal version-history shape: closed [1.5..1.6) + open [1.7..)). >=1 open.
    v = [_vrow(8, "1.5.1164953", "1.6.2000000"),
         _vrow(8, "1.7.3000000", "")]
    n = [_nrow(8)]
    try:
        check_live_entity_has_open_interval(v, n)
        results.append(("live-open-accepts-live-closed-past-plus-open", True, ""))
    except Exception as e:   # noqa: BLE001
        results.append(("live-open-accepts-live-closed-past-plus-open", False,
                        f"a live entity with a closed past + open current interval "
                        f"was rejected: {e}"))

    return results


def _expect_raise(fn, *args):
    try:
        fn(*args)
        return None
    except Exception as e:   # noqa: BLE001
        return e


# --------------------------------------------------------------------------
# Case 4c: KI-0025 -- _present_row_non_trio_differs compares the derives-from edge in
# kcdx_id space, so a NO-OP comparison of an unchanged dependent row NEVER requires the
# dependency's OPEN interval (the chicken-and-egg that blocked the kcdx_id=12 reopen).
# A minimal 2-row in-memory address_versions table -- no fixture rebuild needed.
# --------------------------------------------------------------------------
def _minimal_av_db():
    """A 2-row in-memory address_versions table reproducing the KI-0025 shape: a
    dependency (kcdx_id=12, string_anchor, its sole interval CLOSED) + a dependent
    (kcdx_id=9, instruction_anchor) whose folded `derives_from` points at the
    dependency's av_id. Carries ONLY the columns _present_row_non_trio_differs reads."""
    con = sqlite3.connect(":memory:")
    con.execute(
        "CREATE TABLE address_versions ("
        " id INTEGER PRIMARY KEY, kcdx_id INTEGER, kind INTEGER, module_id INTEGER,"
        " rva INTEGER, signature TEXT, offset INTEGER, vtable_slot INTEGER,"
        " struct_offset INTEGER, valid_through INTEGER,"
        " aob TEXT, anchor_string TEXT, rule TEXT, slot_count INTEGER,"
        " expect_unique INTEGER, derives_from INTEGER)")
    # The DEPENDENCY (kcdx_id=12): av id=100, sole interval CLOSED (valid_through set).
    con.execute(
        "INSERT INTO address_versions (id, kcdx_id, kind, module_id, valid_through, "
        "anchor_string) VALUES (100, 12, 5, 1, 1, 'exec autoexec.cfg')")
    # The DEPENDENT (kcdx_id=9): av id=200, folded derives_from -> the dependency's av id.
    con.execute(
        "INSERT INTO address_versions (id, kcdx_id, kind, module_id, rva, aob, "
        "expect_unique, derives_from) VALUES "
        "(200, 9, 7, 1, 0x86AD99, '48 8B 0D ?? ?? ?? ??', 1, 100)")
    con.commit()
    return con


def _present_row_no_op_does_not_require_open_dependency():
    """KI-0025 Fix A, the two cases the Gate-A review specified:
      (a) the dependent's NO-OP comparison returns False WITHOUT raising even though
          its dependency (kcdx_id=12) has NO open interval (the chicken-and-egg fix);
      (b) a GENUINE derives-from change is STILL detected as a full-column edit (the
          fix did not trade the raise for a silent false no-op)."""
    results = []
    con = _minimal_av_db()
    try:
        # The action mirrors kcdx_id=9's UNCHANGED present row: same module/kind/rva/
        # survival cells as the stored row, derives_from edge -> kcdx_id 12 (unchanged).
        a_noop = {
            "kind": "instruction_anchor", "module": "1", "rva": 0x86AD99,
            "signature": "", "offset": None, "vtable_slot": None,
            "struct_offset": None,
            "survival_aob": "48 8B 0D ?? ?? ?? ??", "survival_anchor_string": None,
            "survival_rule": None, "survival_slot_count": None,
            "survival_expect_unique": 1, "survival_derives_from_kid": 12,
        }
        # kind_id_fn / module_id_fn return the stored ids so the non-survival compare is a
        # no-op; the test isolates the derives-from edge comparison.
        kind_id_fn = lambda: 7      # noqa: E731 -- matches the stored kind id
        module_id_fn = lambda: 1    # noqa: E731 -- matches the stored module id

        # (a) NO-OP comparison must return False and MUST NOT raise (kcdx_id=12 closed).
        try:
            differs = imp._present_row_non_trio_differs(
                con, 200, a_noop, module_id_fn=module_id_fn, kind_id_fn=kind_id_fn)
            if differs:
                results.append((
                    "present-noop-unchanged-returns-false", False,
                    "an UNCHANGED dependent row compared as DIFFERING (false positive -- "
                    "the edge comparison is wrong)"))
            else:
                results.append(("present-noop-unchanged-returns-false", True, ""))
        except Exception as e:   # noqa: BLE001
            results.append((
                "present-noop-unchanged-returns-false", False,
                f"the no-op comparison RAISED on a closed-interval dependency (KI-0025 "
                f"chicken-and-egg NOT fixed): {type(e).__name__}: {e}"))

        # (b) A GENUINE derives-from change (edge -> a DIFFERENT kcdx_id) is detected.
        a_changed = dict(a_noop, survival_derives_from_kid=999)
        try:
            differs = imp._present_row_non_trio_differs(
                con, 200, a_changed, module_id_fn=module_id_fn, kind_id_fn=kind_id_fn)
            results.append((
                "present-genuine-derives-change-detected", bool(differs),
                "" if differs else
                "a GENUINE derives-from change was NOT detected (the fix masks a real "
                "edit as a no-op -- the dangerous failure mode)"))
        except Exception as e:   # noqa: BLE001
            results.append((
                "present-genuine-derives-change-detected", False,
                f"comparing a changed edge RAISED unexpectedly: {type(e).__name__}: {e}"))
        return results
    finally:
        con.close()


# --------------------------------------------------------------------------
# Case 5: validate_db_shape's RELAXED interval check (D40 reconciliation) ACCEPTS a
# legitimately-CLOSED interval injected into the built DB + STILL REJECTS a malformed
# one. Exercises the real validate_db_shape.interval_check_results seam.
# --------------------------------------------------------------------------
def _results_to_map(results):
    return {name: (ok, detail) for name, ok, detail in results}


def _shape_validator_accepts_closed_rejects_malformed(b):
    import validate_db_shape as vds
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")

        # A LEGAL close: create_version a 2nd version at LATER_TAG -> the v1.5 row is
        # CLOSED (valid_through = its own ordinal, a well-ordered non-overlapping close).
        kid, vf = _pick_nonfunction_row(user_db)
        db_editor.create_version(
            out, DLL_PATH, kid, LATER_TAG,
            _seed_source_row_for_create(user_db, kid, vf))

        # (a) The relaxed check ACCEPTS the DB with a legitimately-closed interval --
        #     all three results PASS (the old "ALL rows open" check would have FAILED
        #     the well-ordered/closed row as a baseline violation).
        con = sqlite3.connect(user_db)
        try:
            ok_map = _results_to_map(vds.interval_check_results(con))
        finally:
            con.close()
        ordered_ok = ok_map["address_versions closed intervals well-ordered "
                            "(valid_through >= valid_from)"][0]
        overlap_ok = ok_map["no curated entity has overlapping CLOSED "
                            "address_versions intervals"][0]
        open_ok = ok_map["no curated entity has 2 open address_versions rows"][0]
        assert ordered_ok and overlap_ok and open_ok, (
            f"the relaxed shape check REJECTED a legitimately-closed interval "
            f"(ordered={ordered_ok}, overlap={overlap_ok}, open_unique={open_ok}) -- "
            f"a legal D35 close must be VALID, not a baseline violation")

        # (b) MALFORMED: corrupt the closed row to valid_through < valid_from (an
        #     ordinal earlier than its own valid_from) -> the well-ordered check FAILS,
        #     proving the relaxed check still REJECTS a malformed interval (not weakened).
        con = sqlite3.connect(user_db)
        try:
            # game_versions ids are version-ordered; id 1 is the earliest. Set the
            # closed v1.5 row's valid_through to an ordinal STRICTLY below its valid_from.
            vf_id = _gv_id(con, vf)
            assert vf_id is not None
            bad_vt = vf_id - 1 if vf_id > 1 else None
            # Need a vt strictly < vf. If vf is the lowest id, inject a lower sentinel
            # game_versions row so the malformed compare is exercisable.
            if bad_vt is None or bad_vt < 1:
                con.execute("INSERT INTO game_versions (id, tag, ordinal) "
                            "VALUES (?, ?, ?)", (vf_id + 1000, "0.0.0000001", 0))
                # re-point: make vt a real-but-EARLIER ordinal by using a tag whose id
                # we control to be below vf via a negative-ordinal sentinel row.
                con.execute("UPDATE game_versions SET id = -1 WHERE tag = '0.0.0000001'")
                bad_vt = -1
            con.execute(
                "UPDATE address_versions SET valid_through = ? "
                "WHERE kcdx_id = ? AND valid_from = ?", (bad_vt, kid, vf_id))
            con.commit()
            bad_map = _results_to_map(vds.interval_check_results(con))
        finally:
            con.close()
        ordered_fail = bad_map["address_versions closed intervals well-ordered "
                               "(valid_through >= valid_from)"][0]
        assert ordered_fail is False, (
            "the relaxed shape check ACCEPTED a malformed interval "
            "(valid_through < valid_from) -- the well-ordered check is weakened/gone")
        return True
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Acceptance signal (the canonical grammar -- .claude/rules/acceptance-signal.md).
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
# pytest entry points.
# --------------------------------------------------------------------------
def test_extend_and_close_land_through_batch(baseline):  # noqa: F811
    assert _extend_and_close_land(baseline)


def test_us5_full_column_update_preserves_closed_interval(baseline):  # noqa: F811
    assert _us5_preserves_closed_valid_through(baseline)


def test_interval_validator_accepts_legal_rejects_illegal():
    """The AUTHORED-CLOSED interval validator: ACCEPTS a legal closed interval + the
    2-open transient, REJECTS backwards / unknown-tag / overlapping-closed. Emits the
    canonical ACCEPT signal. No fixture needed (pure seed-row dicts)."""
    results = _validator_accepts_and_rejects()
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "interval validator failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


def test_live_entity_has_open_interval_accepts_rejects():
    """The LIVE-entity-has-open-interval validator (KI-0025): REJECTS a live entity
    whose only interval is closed (no current form), ACCEPTS a live entity with an
    open row + a deprecated/superseded entity with none (exempt). Reproduces the
    kcdx_id=12 closed-only state. Emits the canonical ACCEPT signal. No fixture."""
    results = _live_open_interval_validator_accepts_and_rejects()
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "live-open-interval validator failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


def test_present_row_no_op_does_not_require_open_dependency():
    """KI-0025 Fix A: a NO-OP comparison of an unchanged dependent row does NOT require
    its dependency's OPEN interval (the chicken-and-egg fix), AND a genuine derives-from
    change is still detected (no false no-op). Emits the canonical ACCEPT signal. No
    fixture (a minimal in-memory address_versions table)."""
    results = _present_row_no_op_does_not_require_open_dependency()
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "present-row no-op comparison failures:\n  " + \
        "\n  ".join(f"{aid}: {detail}" for aid, detail in failures)


def test_shape_validator_accepts_closed_rejects_malformed(baseline):  # noqa: F811
    assert _shape_validator_accepts_closed_rejects_malformed(baseline)


if __name__ == "__main__":
    val_results = _validator_accepts_and_rejects()
    val_results += _live_open_interval_validator_accepts_and_rejects()
    val_results += _present_row_no_op_does_not_require_open_dependency()
    if not _have_inputs():
        print(f"SKIP (no fixture): mini-dump or WHGame.dll not found "
              f"(dump={DUMP_DIR}, dll={DLL_PATH}); running validator cases only")
        _emit_signal(val_results)
        sys.exit(0 if all(ok for _, ok, _ in val_results) else 1)
    try:
        b = _get_baseline()
        results = []
        try:
            _extend_and_close_land(b)
            results.append(("interval-extend-and-close-land", True, ""))
            print("PASS test_extend_and_close_land_through_batch")
        except AssertionError as e:
            results.append(("interval-extend-and-close-land", False, str(e).splitlines()[0]))
            print("FAIL test_extend_and_close_land_through_batch")
        try:
            _us5_preserves_closed_valid_through(b)
            results.append(("interval-us5-preserves-closed", True, ""))
            print("PASS test_us5_full_column_update_preserves_closed_interval")
        except AssertionError as e:
            results.append(("interval-us5-preserves-closed", False, str(e).splitlines()[0]))
            print("FAIL test_us5_full_column_update_preserves_closed_interval")
        try:
            _shape_validator_accepts_closed_rejects_malformed(b)
            results.append(("shape-validator-accepts-closed-rejects-malformed", True, ""))
            print("PASS test_shape_validator_accepts_closed_rejects_malformed")
        except AssertionError as e:
            results.append(("shape-validator-accepts-closed-rejects-malformed", False,
                            str(e).splitlines()[0]))
            print("FAIL test_shape_validator_accepts_closed_rejects_malformed")
        results.extend(val_results)
        _emit_signal(results)
        sys.exit(0 if all(ok for _, ok, _ in results) else 1)
    finally:
        _cleanup_baseline()
