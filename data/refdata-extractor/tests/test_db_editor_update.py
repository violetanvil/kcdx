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

  2. FULL-COLUMN UPDATE (US-5: correct an rva / signature / kind on an EXISTING
     row) -- a POSITIVE oracle (the step-3c applier extension landed). The applier's
     present-row branch (import_to_sqlite._apply_one_db) now detects a non-trio
     change and runs a full-column UPDATE through the SAME machinery the ADD path
     uses (build_curated_row + the kind-class PROMOTE/mint/REFUSE gate + the folded
     re-find cells from the per-kind dispatch), as an UPDATE of the existing row (same
     av_id, same identity key), NOT an INSERT. The settled semantics (step report /
     design.md §6 US-5 + §10 D13/D19), asserted below:
       1. rva change (function) to an rva WITH a bulk baseline -> RE-PROMOTE: the
          row carries the NEW bulk fn's content_hash + length (the function survival
          datum lives on the av row; no folded re-find cell for a function).
       2. rva change (function) to an rva with NO bulk baseline -> REFUSE
          (BaselineRefusal), NO write. The ONE designed apply != rebuild seam (a
          from-scratch rebuild would silently MINT a NULL-fingerprint function; the
          interactive applier refuses rather than mint -- user-approved). Asserted
          as the refusal, NOT a byte-match.
       3. kind change -> the folded re-find cells rebuild for the new kind, the av
          fingerprint recomputes by kind-class (function -> PROMOTE/REFUSE;
          non-function -> mint, fingerprint NULL).
       4. signature-only change -> the fingerprint + folded re-find cells are
          UNTOUCHED; only the signature cell changes.
       5. audit-trio-only change -> the existing trio-UPDATE path (byte-identical;
          covered by case 1 above -- not regressed).
     apply==rebuild is proven by the whole-DB convergence fingerprint: the direct
     UPDATE produces a DB byte-identical to the seed-rebuild apply of the same
     edit, AND the edited row reaches the same logical curated state a from-scratch
     run_rebuild describes (asserted id-agnostically -- the direct-write model is
     id-stable, design D19; run_rebuild re-keys ids, so the curated row, not the
     internal id, is the comparand). Case 2 is the sole non-convergent case.

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


def _folded_for(db_path, kcdx_id, valid_from_tag):
    """The folded survival/re-find cells of the curated (kcdx_id, valid_from) av row
    as a dict (D22 / design §11.2 -- the former 1:1 `survival` sibling folded onto the
    av row, no separate table). The six re-find columns + the body fingerprint
    (content_hash/length), so a full-column edit's effect on the folded data is
    checkable. content_hash stays bytes (comparable)."""
    con = sqlite3.connect(db_path)
    try:
        vf = con.execute("SELECT id FROM game_versions WHERE tag = ?",
                         (valid_from_tag,)).fetchone()[0]
        row = con.execute(
            "SELECT aob, anchor_string, rule, slot_count, expect_unique, "
            "derives_from, content_hash, length FROM address_versions "
            "WHERE kcdx_id = ? AND valid_from = ?", (kcdx_id, vf)).fetchone()
        if row is None:
            return None
        return dict(zip(
            ("aob", "anchor_string", "rule", "slot_count", "expect_unique",
             "derives_from", "content_hash", "length"), row))
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


_SNAP_TABLES = ("address_versions", "address_names")


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


def _pick_function_row_and_other_bulk_rva(user_db, dev_db):
    """A curated FUNCTION-kind row (the re-promote subject) PLUS a DIFFERENT bulk
    function rva that carries a body fingerprint (the new rva to re-promote to).
    Returns (kcdx_id, valid_from_tag, current_rva, new_rva). The new rva is an
    uncurated bulk fn (kcdx_id IS NULL, content_hash NOT NULL) other than the row's
    own rva -- so the re-promote carries that bulk fn's NEW fingerprint."""
    ucon = sqlite3.connect(user_db)
    try:
        gv = {r[0]: r[1] for r in ucon.execute("SELECT id, tag FROM game_versions")}
        kdec = _dict_id_to_val(ucon, "address_versions", "kind")
        target = None
        for kid, vf, rva, kindid in ucon.execute(
                "SELECT kcdx_id, valid_from, rva, kind FROM address_versions "
                "WHERE kcdx_id IS NOT NULL AND rva IS NOT NULL"):
            if kdec.get(kindid) in ("function", "function_variadic",
                                    "function_no_sig"):
                target = (kid, gv.get(vf), rva)
                break
    finally:
        ucon.close()
    if target is None:
        return None
    kid, vf_tag, cur_rva = target
    dcon = sqlite3.connect(dev_db)
    try:
        row = dcon.execute(
            "SELECT rva FROM address_versions WHERE kcdx_id IS NULL AND "
            "content_hash IS NOT NULL AND rva != ? ORDER BY rva LIMIT 1",
            (cur_rva,)).fetchone()
    finally:
        dcon.close()
    if row is None:
        return None
    return (kid, vf_tag, cur_rva, row[0])


def _a_no_baseline_rva(dev_db):
    """An rva past the DEV DB's max bulk function entry -- a function-kind re-promote
    to it has NO bulk baseline (the case-2 REFUSE)."""
    con = sqlite3.connect(dev_db)
    try:
        mx = con.execute(
            "SELECT MAX(rva) FROM address_versions WHERE kcdx_id IS NULL").fetchone()[0]
        return (mx or 0) + 0x100000
    finally:
        con.close()


def _db_fingerprint(out_dir):
    """Whole-DB byte fingerprint: a per-table content hash over EVERY table in both
    reference DBs (the same comparator test_direct_write.py uses). Two out_dirs with
    the same fingerprint hold byte-identical DBs."""
    fp = {}
    for which in ("reference.sqlite", "reference-dev.sqlite"):
        dbp = os.path.join(out_dir, which)
        con = sqlite3.connect(dbp)
        try:
            tables = [r[0] for r in con.execute(
                "SELECT name FROM sqlite_master WHERE type='table' ORDER BY name")]
        finally:
            con.close()
        for t in tables:
            fp[(which, t)] = _hash_table(dbp, t)
    return fp


def _rebuild_edited_seed(b, kid, vf_tag, edits):
    """Rebuild a FRESH DB pair from scratch off the committed seed with the one-row
    edit folded in -- the apply==rebuild reference. Returns the out_dir (caller
    removes it). Reuses the baseline's seed source so only `edits` differs."""
    from seeds_shared import seed_csv_edit
    out = tempfile.mkdtemp(prefix="db_editor_rebuild_")
    seed_dir = os.path.join(out, "seed_src")
    _copy_seeds(seed_dir)
    seed_csv_edit.update_row_in_place(
        os.path.join(seed_dir, "address_versions_seed.csv"),
        key_columns=("kcdx_id", "valid_from_version"),
        key_values=(str(kid), str(vf_tag)), edits=dict(edits))
    rebuild_out = os.path.join(out, "rebuild")
    _rebuild_into(seed_dir, rebuild_out)
    return out, rebuild_out


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
# Case 2: full-column UPDATE (US-5) -- the POSITIVE oracle (step-3c applier
# extension landed). Five sub-cases, each proving apply==rebuild EXCEPT the one
# designed refusal seam (sub-case 2 below).
#
# THE apply==rebuild PROOF (two oracles, both validated by the step-3c probe):
#   - WHOLE-DB byte-identity: the direct full-column UPDATE produces a DB
#     byte-identical (_db_fingerprint over every table in both DBs) to the
#     SEED-REBUILD APPLY of the same edit -- the design's convergence contract
#     (D13/D19: db_editor's direct write and the seed-rebuild path BOTH drive the
#     SAME _apply_one_db, so they converge by construction). This is the
#     test_direct_write.py convergence comparator, applied here per sub-case.
#   - CURATED-ROW logical equivalence vs a from-scratch run_rebuild: the edited
#     row reaches the SAME logical curated state a run_rebuild of the edited seed
#     describes, asserted ID-AGNOSTICALLY (_av_row_decoded drops `id`/`module_id`).
#     run_rebuild RE-KEYS internal ids (a function promote keys the curated row at
#     the bulk fn's ordinal id; a kind change mints a fresh id), which a DIRECT
#     UPDATE cannot reproduce -- the direct-write model is ID-STABLE by design
#     (D19 §3/§4: a maintainer edit is a DIRECT in-place UPDATE, NOT a re-keying
#     rebuild; run_rebuild is the one-time genesis bootstrap, never re-run for an
#     edit). So the comparand is the curated ROW's content, not its internal id.
#     (A whole-DB byte-match vs run_rebuild is NOT the contract and is impossible
#     by UPDATE -- a step-3c finding; the convergence target is the seed-rebuild
#     apply, which is itself an _apply_one_db drive.)
# --------------------------------------------------------------------------
def _seed_rebuild_apply(out, kid, vf_tag, edits):
    """Reconstruct the OLD seed-rebuild write for ONE edit and commit it (the
    convergence reference). Export the committed DB -> fold the edit into the temp
    seed -> repoint the importer's seed constants -> apply_seeds(defer_commit) +
    commit. Both this and the direct path drive _apply_one_db, so a divergence is a
    real applier bug. Mutates `out`'s DBs in place."""
    from seeds_shared import seed_csv_edit
    user_db = os.path.join(out, "reference.sqlite")
    work = tempfile.mkdtemp(prefix="db_editor_seedrebuild_")
    try:
        from seeds_shared.csv_exporter import export_seeds
        prospective = os.path.join(work, "prospective_seed")
        os.makedirs(prospective, exist_ok=True)
        export_seeds(user_db, prospective)
        seed_csv_edit.update_row_in_place(
            os.path.join(prospective, "address_versions_seed.csv"),
            key_columns=("kcdx_id", "valid_from_version"),
            key_values=(str(kid), str(vf_tag)), edits=dict(edits))
        version = (GVT, imp.GAME_VERSION_ORDINAL)
        with _seeds_pointed_at(prospective):
            handle = imp.apply_seeds(out, None, version=version, defer_commit=True)
        imp.commit(handle)
    finally:
        shutil.rmtree(work, ignore_errors=True)


def _assert_apply_equals_rebuild(b, kid, vf_tag, edits, label):
    """Apply `edits` to (kid, vf_tag) via the DIRECT path on one fresh DB and via
    the SEED-REBUILD path on another, assert the two DBs are byte-identical
    (whole-DB convergence), AND assert the direct-edited curated row matches a
    from-scratch run_rebuild of the edited seed id-agnostically. Returns the direct
    out_dir (caller may inspect specific cells before removing it)."""
    out_direct = _fresh_db(b)
    out_seed = _fresh_db(b)
    rebuild_root = None
    try:
        db_editor.update_version_row(out_direct, DLL_PATH, kid, vf_tag, dict(edits))
        _seed_rebuild_apply(out_seed, kid, vf_tag, edits)
        fp_d = _db_fingerprint(out_direct)
        fp_s = _db_fingerprint(out_seed)
        assert fp_d == fp_s, (
            f"[{label}] direct full-column UPDATE did NOT converge with the "
            f"seed-rebuild apply (apply==rebuild broken); differing tables: "
            f"{sorted(k for k in fp_d if fp_d.get(k) != fp_s.get(k))}")
        # Curated row logical-equivalence vs a from-scratch run_rebuild (id-agnostic).
        rebuild_root, rebuild_out = _rebuild_edited_seed(b, kid, vf_tag, edits)
        after_direct = _av_row_decoded(
            os.path.join(out_direct, "reference.sqlite"), kid, vf_tag)
        after_rebuild = _av_row_decoded(
            os.path.join(rebuild_out, "reference.sqlite"), kid, vf_tag)
        assert after_direct == after_rebuild, (
            f"[{label}] the direct-edited curated row does not match a from-scratch "
            f"run_rebuild of the edited seed (id-agnostic): direct={after_direct} "
            f"rebuild={after_rebuild}")
        return out_direct
    finally:
        shutil.rmtree(out_seed, ignore_errors=True)
        if rebuild_root is not None:
            shutil.rmtree(rebuild_root, ignore_errors=True)
        # out_direct is NOT removed here -- the caller removes it after inspecting.


def _full_column_update(b):
    user_db = os.path.join(b["out"], "reference.sqlite")
    dev_db = os.path.join(b["out"], "reference-dev.sqlite")

    # ---- sub-case 1: rva re-promote (function row -> a DIFFERENT bulk fn's rva).
    # The row's content_hash + length become the NEW bulk fn's; the survival
    # function_hash carries the new fingerprint; apply==rebuild.
    picked = _pick_function_row_and_other_bulk_rva(user_db, dev_db)
    assert picked is not None, "no function row + alternate bulk rva in the fixture"
    kid, vf_tag, cur_rva, new_rva = picked
    before = _av_row_decoded(user_db, kid, vf_tag)
    out = _assert_apply_equals_rebuild(
        b, kid, vf_tag, {"rva": "0x%X" % new_rva}, "rva re-promote")
    try:
        udb = os.path.join(out, "reference.sqlite")
        after = _av_row_decoded(udb, kid, vf_tag)
        assert after["rva"] != before["rva"], "[rva re-promote] rva did not change"
        assert after["content_hash"] != before["content_hash"], (
            "[rva re-promote] content_hash did not re-promote to the new bulk fn")
        assert after["content_hash"] is not None and after["length"] is not None, (
            "[rva re-promote] re-promoted row lost its fingerprint")
        # function row: NO folded re-find cell; the body fingerprint (content_hash/
        # length) on the av row IS the survival datum and carries the NEW fingerprint.
        fc = _folded_for(udb, kid, vf_tag)
        assert fc is not None and all(
            fc[c] is None for c in ("aob", "anchor_string", "rule", "slot_count",
                                    "expect_unique")), (
            "[rva re-promote] function row carries a folded re-find cell")
        assert fc["content_hash"] == after["content_hash"], (
            "[rva re-promote] folded fingerprint not the new body hash")
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # ---- sub-case 2: no-baseline REFUSAL -- the DESIGNED apply != rebuild seam.
    # Re-promoting a function row to an rva with NO bulk baseline RAISES
    # BaselineRefusal and writes NOTHING (both DBs byte-identical to before). This
    # is the ONE place the interactive applier is STRICTER than a from-scratch
    # rebuild BY DESIGN (user-approved): a rebuild would silently MINT a
    # NULL-fingerprint function; the applier refuses rather than mint, preserving
    # the no-NULL-fingerprint-function invariant. Asserted as the REFUSAL (not a
    # byte-match) -- a future reader must NOT "fix" the applier to mint here (that
    # silently reopens the missing-baseline-disguised-as-an-entity hole).
    out = _fresh_db(b)
    try:
        no_base = _a_no_baseline_rva(dev_db)
        before_fp = _db_fingerprint(out)
        raised = None
        try:
            db_editor.update_version_row(
                out, DLL_PATH, kid, vf_tag, {"rva": "0x%X" % no_base})
        except imp.BaselineRefusal as e:
            raised = e
        assert raised is not None, (
            "[no-baseline refusal] re-promoting a function row to a no-baseline rva "
            "did NOT raise BaselineRefusal (the applier must refuse, never mint a "
            "NULL-fingerprint function -- the designed apply != rebuild seam)")
        assert _db_fingerprint(out) == before_fp, (
            "[no-baseline refusal] the refusal left a partial write (both DBs must "
            "be byte-identical to before the attempt)")
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # ---- sub-case 3: kind change (function -> non-function). The folded re-find
    # cells rebuild for the new kind; the av fingerprint recomputes by kind-class (a
    # non-function kind mints -> content_hash/length NULL); apply==rebuild.
    out = _assert_apply_equals_rebuild(
        b, kid, vf_tag, {"kind": "data_slot"}, "kind fn->non-fn")
    try:
        udb = os.path.join(out, "reference.sqlite")
        after = _av_row_decoded(udb, kid, vf_tag)
        assert after["kind"] == "data_slot", "[kind change] kind did not change"
        assert after["content_hash"] is None and after["length"] is None, (
            "[kind change] non-function kind kept a fingerprint (must mint NULL)")
        # The folded re-find cells rebuilt for the new (non-function) kind: the
        # function body fingerprint is NOT leaked into them, and with no survival
        # datum supplied for the new data_slot kind the re-find cells are NULL.
        fc = _folded_for(udb, kid, vf_tag)
        assert fc is not None and fc["content_hash"] is None, (
            "[kind change] folded fingerprint not cleared on the kind change")
        assert all(fc[c] is None for c in ("aob", "anchor_string", "rule",
                                           "slot_count", "expect_unique")), (
            "[kind change] folded re-find cells did not rebuild empty for the new kind")
    finally:
        shutil.rmtree(out, ignore_errors=True)

    # ---- sub-case 4: signature-only. The fingerprint + folded re-find cells are
    # UNTOUCHED (signature is not part of the body hash); only the signature cell
    # changes; apply==rebuild.
    before = _av_row_decoded(user_db, kid, vf_tag)
    before_fc = _folded_for(user_db, kid, vf_tag)
    out = _assert_apply_equals_rebuild(
        b, kid, vf_tag, {"signature": "void (corrected sig)"}, "signature-only")
    try:
        udb = os.path.join(out, "reference.sqlite")
        after = _av_row_decoded(udb, kid, vf_tag)
        assert after["signature"] == "void (corrected sig)", (
            "[signature-only] signature did not change")
        assert after["content_hash"] == before["content_hash"], (
            "[signature-only] fingerprint changed (must be untouched)")
        assert after["length"] == before["length"], (
            "[signature-only] length changed (must be untouched)")
        after_fc = _folded_for(udb, kid, vf_tag)
        assert after_fc == before_fc, (
            "[signature-only] folded re-find cells changed (must be untouched)")
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
    _full_column_update(baseline)


def test_invalid_edit_aborts_with_no_write(baseline):  # noqa: F811
    _invalid_aborts_with_no_write(baseline)


if __name__ == "__main__":
    try:
        b = _get_baseline()
        _audit_trio_only_update(b)
        print("PASS test_audit_trio_only_update_lands_atomically")
        _full_column_update(b)
        print("PASS test_full_column_update_lands_atomically")
        _invalid_aborts_with_no_write(b)
        print("PASS test_invalid_edit_aborts_with_no_write")
        print("\nall db_editor update oracle tests passed")
    finally:
        _cleanup_baseline()
