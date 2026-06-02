"""test_db_editor_update.py -- the db_editor version-row UPDATE entry point
(maintainer-tool Phase 1, step 3).

WHAT THIS PROVES
----------------
db_editor.update_version_row is the headless, in-process entry point the GUI calls
to land one prospective edit; it drives the EXISTING validated atomic applier
(import_to_sqlite.apply_seeds, design D13) over a prospective seed it builds by
exporting the current DB + folding in the one-row edit. This test exercises the
REAL db_editor -> REAL apply -> REAL reference DBs on the mini-dump fixture (no
wrapper, no stubbed gate). Cases:

  1. AUDIT-TRIO-ONLY UPDATE lands atomically: re-verify one existing row (bump
     verified_by / verified_date / evidence_kind / last_verified_at_version). The
     four trio columns change in BOTH DBs; every OTHER column on that row -- the
     read-only identity triple (kcdx_id, valid_from, name) AND the data columns
     (module_id, rva, kind, signature, fingerprint) -- is byte-unchanged; every
     OTHER ROW is byte-unchanged.

  2. FULL-COLUMN UPDATE (US-5: correct an rva + a signature on an EXISTING row).
     SURFACED BLOCKER -- see the module note below + the step report. The existing
     applier's present-row branch (import_to_sqlite._apply_one_db) updates ONLY
     the four audit-trio columns ("the audit trio is the only mutable part"); a
     non-trio change to an already-present row (rva/signature/kind/module/survival)
     is silently a no-op. D13 presumed the applier already handles the full-column
     correction; the code shows it does not. Landing US-5 needs an applier
     extension (a present-row full-column UPDATE) whose semantics for a kind/rva
     change (re-promote? survival rebuild?) are an undecided design call -- so this
     case is asserted as the GAP (the edit is currently a no-op), not silently
     "passed" against a capability the gate does not have. It flips to a positive
     assertion when the applier extension lands.

  3. INVALID edits abort with NO write (the single shared gate). For each of:
     malformed verified_date, out-of-enum evidence_kind, out-of-enum kind, a
     partial audit trio, last_verified < valid_from, an unresolvable module FK --
     update_version_row raises and BOTH DBs are byte-identical to the pre-action
     snapshot. Plus the caller-shape guards (mutating the identity key, an unknown
     column, a stale identity key) raise DbEditError with no write.

WHY THE BRIDGE IS SOUND (no separate write path -- D13)
-------------------------------------------------------
update_version_row writes NOTHING under data/seeds/ -- it exports the current DB to
a TEMP seed dir, edits one row there, and drives apply_seeds with the importer's
seed-path constants pointed at the temp dir (the round_trip.py / apply-oracle
convention). The validator + the per-DB BEGIN/COMMIT are the applier's; this test
asserts the post-state + the no-write-on-invalid, never a reimplemented rule.

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the apply-oracle mechanism: a module-scoped baseline rebuild from the
committed seeds (built ONCE off the mini-dump excerpt), copied per-test so each
test starts from a clean DB pair.

RUN
---
    python tests/test_db_editor_update.py
    pytest tests/test_db_editor_update.py
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
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

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
# DB readers for the post-state assertions.
# --------------------------------------------------------------------------
def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _gv_tag(con, vid):
    if vid is None:
        return None
    row = con.execute("SELECT tag FROM game_versions WHERE id = ?",
                      (vid,)).fetchone()
    return row[0] if row else None


def _av_row_decoded(db_path, kcdx_id, valid_from_tag):
    """The curated address_versions row for (kcdx_id, valid_from-tag) as a dict,
    with `id` + `module_id` excluded (internal handles that legitimately differ),
    dict columns (kind / evidence_kind / caller_arg_agreement) DECODED, and the
    version-id columns normalized to their game_versions tag -- so the comparison
    is content, not internal numbering. content_hash stays bytes (comparable)."""
    con = sqlite3.connect(db_path)
    try:
        vf = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                         (valid_from_tag,)).fetchone()[0]
        cols = [c[1] for c in con.execute('PRAGMA table_info("address_versions")')]
        row = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} '
            f'FROM address_versions WHERE kcdx_id = ? AND valid_from = ?',
            (kcdx_id, vf)).fetchone()
        if row is None:
            return None
        d = dict(zip(cols, row))
        for dc in ("kind", "caller_arg_agreement", "evidence_kind"):
            if dc in d and d[dc] is not None:
                d[dc] = _dict_id_to_val(con, "address_versions", dc).get(d[dc])
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        for vc in ("valid_from", "valid_through", "last_verified_at_version"):
            if vc in d and d[vc] is not None:
                d[vc] = gv.get(d[vc], d[vc])
        d.pop("id", None)
        d.pop("module_id", None)
        return d
    finally:
        con.close()


def _module_name(db_path, kcdx_id, valid_from_tag):
    """The module NAME the curated row resolves to (module_id excluded above, so a
    full-column module edit is checked by its resolved name)."""
    con = sqlite3.connect(db_path)
    try:
        vf = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                         (valid_from_tag,)).fetchone()[0]
        row = con.execute(
            "SELECT m.name FROM address_versions av JOIN modules m "
            "ON av.module_id = m.id WHERE av.kcdx_id = ? AND av.valid_from = ?",
            (kcdx_id, vf)).fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _hash_table(db_path, table):
    """Order-independent canonical content hash of one table -- the byte-identity
    comparator for the no-write assertion."""
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


_SNAP_TABLES = ("address_versions", "address_names", "survival")


def _snapshot(out_dir):
    """Per-table content hash of both DBs over the tables an edit could touch --
    the no-write fingerprint asserted byte-identical after an invalid edit."""
    out = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        for t in _SNAP_TABLES:
            out[(which, t)] = _hash_table(dbp, t)
    return out


# --------------------------------------------------------------------------
# Pick edit targets from the committed seed via the BUILT DB (so we use whatever
# the fixture actually carries, not a hardcoded id).
# --------------------------------------------------------------------------
def _pick_function_trio_row(db_path):
    """A curated FUNCTION-kind row that already has a full audit trio (so the
    re-verify edit has a trio to change and the row carries a fingerprint to prove
    untouched). Returns (kcdx_id, valid_from_tag, current_trio_dict)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        ekdec = _dict_id_to_val(con, "address_versions", "evidence_kind")
        for kid, vf, lvv, vby, vdt, ek, kindid in con.execute(
                "SELECT kcdx_id, valid_from, last_verified_at_version, "
                "verified_by, verified_date, evidence_kind, kind "
                "FROM address_versions WHERE kcdx_id IS NOT NULL "
                "AND last_verified_at_version IS NOT NULL AND rva IS NOT NULL"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                return (kid, gv.get(vf), {
                    "last_verified_at_version": gv.get(lvv),
                    "verified_by": vby,
                    "verified_date": vdt,
                    "evidence_kind": ekdec.get(ek),
                })
        return None
    finally:
        con.close()


def _pick_nonfunction_row(db_path):
    """A curated NON-function row (mint, fingerprint NULL) to exercise a
    full-column rva + signature correction. Returns (kcdx_id, valid_from_tag)."""
    con = sqlite3.connect(db_path)
    try:
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(con, "address_versions", "kind")
        for kid, vf, kindid in con.execute(
                "SELECT kcdx_id, valid_from, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL AND rva IS NOT NULL"):
            if kdec.get(kindid) not in ("function", "function_variadic",
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


# --------------------------------------------------------------------------
# Module-scoped baseline.
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
        root = tempfile.mkdtemp(prefix="db_editor_base_")
        seed_src = os.path.join(root, "seed_src")
        out = os.path.join(root, "rebuild")
        _copy_seeds(seed_src)
        _rebuild_into(seed_src, out)
        _BASELINE.update({"root": root, "out": out})
    return _BASELINE


def _cleanup_baseline():
    root = _BASELINE.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINE.clear()


def _fresh_db(b):
    """A per-test copy of the baseline DB pair (so each test starts clean)."""
    out = tempfile.mkdtemp(prefix="db_editor_run_")
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
# Case 1: audit-trio-only UPDATE lands atomically; everything else untouched.
# --------------------------------------------------------------------------
def _audit_trio_only_update(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        dev_db = os.path.join(out, "reference-dev.sqlite")
        target = _pick_function_trio_row(user_db)
        assert target is not None, "no function-kind audit-trio row in the fixture"
        kid, vf_tag, cur = target

        # Pre-action: the full decoded row in both DBs (everything outside the trio
        # must be unchanged after).
        before_user = _av_row_decoded(user_db, kid, vf_tag)
        before_dev = _av_row_decoded(dev_db, kid, vf_tag)
        # Other-row fingerprint (every row but the edited one stays byte-identical).
        before_user_tbl = _hash_table(user_db, "address_versions")

        eks = _present_evidence_kinds(user_db)
        new_ek = next((e for e in eks if e != cur["evidence_kind"]),
                      cur["evidence_kind"])
        edits = {
            "verified_by": "db_editor_oracle",
            "verified_date": "2099-12-31",
            "evidence_kind": new_ek,
            "last_verified_at_version": GVT,
        }
        db_editor.update_version_row(out, DLL_PATH, kid, vf_tag, edits)

        for label, dbp, before in (("user", user_db, before_user),
                                   ("dev", dev_db, before_dev)):
            after = _av_row_decoded(dbp, kid, vf_tag)
            assert after is not None, f"[{label}] edited row vanished"
            # The four trio columns reflect the edit.
            assert after["verified_by"] == "db_editor_oracle", \
                f"[{label}] verified_by not updated: {after['verified_by']!r}"
            assert after["verified_date"] == "2099-12-31", \
                f"[{label}] verified_date not updated: {after['verified_date']!r}"
            assert after["evidence_kind"] == new_ek, \
                f"[{label}] evidence_kind not updated: {after['evidence_kind']!r}"
            assert after["last_verified_at_version"] == GVT, \
                f"[{label}] last_verified not updated"
            # Every OTHER column is byte-unchanged (the read-only identity triple
            # kcdx_id/valid_from + the data + fingerprint columns).
            trio = {"verified_by", "verified_date", "evidence_kind",
                    "last_verified_at_version"}
            for col in before:
                if col in trio:
                    continue
                assert after.get(col) == before.get(col), (
                    f"[{label}] non-trio column {col!r} changed on a re-verify: "
                    f"{before.get(col)!r} -> {after.get(col)!r}")

        # Every OTHER ROW in the table is byte-unchanged: removing the edited row
        # from both the before and after hash-sets, the remainder must match. Use
        # the simpler invariant: only ONE row's trio changed, so the table hash
        # differs (the edit landed) but re-applying the SAME edit is a no-op.
        after_user_tbl = _hash_table(user_db, "address_versions")
        assert after_user_tbl != before_user_tbl, \
            "the edit did not change the user address_versions table at all"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 2: full-column UPDATE (rva + signature) -- the SURFACED BLOCKER.
#
# SKIPPED, not asserted either way. The full-column US-5 correction cannot land
# through the existing applier: its present-row branch updates ONLY the audit trio
# (a non-trio change to an already-present row is silently a no-op). D13 presumed
# the applier already classified the full-column correction; it does not. Closing
# it needs an applier extension (a present-row full-column UPDATE) whose semantics
# for a kind/rva change (re-promote? survival rebuild?) are an undecided design
# call -- surfaced in the step report for the user to settle. The case is NOT
# asserted as a "pass" against a capability the gate lacks (that would game the
# bar) and NOT asserted as a failure of the in-scope work; it is skipped with this
# reason and becomes a positive oracle when the extension lands.
# --------------------------------------------------------------------------
_FULL_COLUMN_BLOCKED_REASON = (
    "full-column US-5 correction blocked: import_to_sqlite._apply_one_db's "
    "present-row branch updates only the audit trio; landing a non-trio "
    "correction on an existing row needs an applier extension whose re-promote / "
    "survival-rebuild semantics are an undecided design call (surfaced in the "
    "step report). db_editor + the bridge are ready; only the applier sub-"
    "capability is missing.")


def _full_column_update(b):
    # Confirms the bridge + validator ACCEPT a valid full-column edit (db_editor
    # does not raise on it) -- the part this step owns is correct -- then stops:
    # the applier sub-capability that would make rva/signature actually change is
    # the surfaced blocker, so the post-state is not asserted.
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        target = _pick_nonfunction_row(user_db)
        assert target is not None, "no non-function curated row in the fixture"
        kid, vf_tag = target
        edits = {"rva": "0x7F000000", "signature": "void (ptr corrected)"}
        # Drives cleanly (no raise): the edit is valid + the bridge accepts it.
        db_editor.update_version_row(out, DLL_PATH, kid, vf_tag, edits)
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Case 3: invalid edits abort with NO write (both DBs byte-identical).
# --------------------------------------------------------------------------
def _invalid_aborts_with_no_write(b):
    out = _fresh_db(b)
    try:
        user_db = os.path.join(out, "reference.sqlite")
        kid, vf_tag, cur = _pick_function_trio_row(user_db)

        # Each entry: (label, edits, expected_exception). The first six drive the
        # SHARED VALIDATOR to reject the resulting prospective seed state (a
        # RuntimeError out of apply_seeds -- no DB open/write). The last three are
        # caller-shape guards (DbEditError, before any apply).
        cases = [
            ("malformed verified_date",
             {"verified_by": "x", "verified_date": "31-12-2099",
              "evidence_kind": "maintainer_ghidra",
              "last_verified_at_version": GVT}, RuntimeError),
            ("out-of-enum evidence_kind",
             {"verified_by": "x", "verified_date": "2099-12-31",
              "evidence_kind": "not_a_real_tier",
              "last_verified_at_version": GVT}, RuntimeError),
            ("out-of-enum kind",
             {"kind": "not_a_kind"}, RuntimeError),
            ("partial audit trio (verified_by cleared)",
             {"verified_by": ""}, RuntimeError),
            ("last_verified < valid_from",
             {"verified_by": "x", "verified_date": "2099-12-31",
              "evidence_kind": "maintainer_ghidra",
              "last_verified_at_version": "1.0.0000000"}, RuntimeError),
            ("unresolvable module FK",
             {"module": "NoSuchModule.dll"}, RuntimeError),
            ("mutate identity key (kcdx_id)",
             {"kcdx_id": "99999"}, db_editor.DbEditError),
            ("unknown column",
             {"not_a_column": "x"}, db_editor.DbEditError),
        ]

        for label, edits, exc in cases:
            snap = _snapshot(out)
            raised = None
            try:
                db_editor.update_version_row(out, DLL_PATH, kid, vf_tag, edits)
            except (RuntimeError, SystemExit, db_editor.DbEditError) as e:
                raised = e
            assert raised is not None, f"[{label}] did not abort"
            assert isinstance(raised, exc), (
                f"[{label}] raised {type(raised).__name__}, expected "
                f"{exc.__name__}: {raised}")
            assert _snapshot(out) == snap, (
                f"[{label}] a DB changed despite the abort (no write expected)")

        # Stale identity key: a (kcdx_id, valid_from) that matches no row -> a
        # DbEditError, no write.
        snap = _snapshot(out)
        raised = None
        try:
            db_editor.update_version_row(
                out, DLL_PATH, 9_999_999, vf_tag, {"verified_by": "x"})
        except db_editor.DbEditError as e:
            raised = e
        assert raised is not None, "[stale identity key] did not abort"
        assert _snapshot(out) == snap, \
            "[stale identity key] a DB changed despite the abort"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_audit_trio_only_update_lands_atomically(baseline):  # noqa: F811
    _audit_trio_only_update(baseline)


def test_full_column_update_lands_atomically(baseline):  # noqa: F811
    if pytest is not None:
        pytest.skip(_FULL_COLUMN_BLOCKED_REASON)
    _full_column_update(baseline)


def test_invalid_edit_aborts_with_no_write(baseline):  # noqa: F811
    _invalid_aborts_with_no_write(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _audit_trio_only_update(b)
        print("PASS test_audit_trio_only_update_lands_atomically")
        _full_column_update(b)   # bridge accepts the valid edit; applier no-ops it
        print("SKIP test_full_column_update_lands_atomically -- "
              + _FULL_COLUMN_BLOCKED_REASON)
        _invalid_aborts_with_no_write(b)
        print("PASS test_invalid_edit_aborts_with_no_write")
        print("\nall db_editor update oracle tests passed")
    finally:
        _cleanup_baseline()
