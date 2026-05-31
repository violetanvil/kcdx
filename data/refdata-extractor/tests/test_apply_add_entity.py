"""test_apply_add_entity.py -- the apply-equals-rebuild oracle for the ADD actions
(db-updator Phase 1, step 4): add-entity (function-kind PROMOTE + non-function-
kind MINT), the baseline-present refusal, add-versions-row interval close, and
add idempotence.

WHAT THIS PROVES
----------------
`apply` lands a hand-edited ADD delta (a brand-new curated entity: a names row +
a versions row) into BOTH reference DBs WITHOUT a rebuild, and the result is
identical to what a full --rebuild from the EDITED seeds would have produced --
the Phase-1 oracle (context.md "rebuild is the oracle"). The id-autoincrement
column is EXCLUDED from the comparison (apply mints MAX(id)+1 per-DB; rebuild
assigns from its own base -- both are valid non-colliding handles).

Tests:
  1. ADD-ENTITY, FUNCTION KIND (oracle): add a new curated entity whose rva
     matches a real bulk function in the dump. Path A: rebuild EDITED seeds.
     Path B: rebuild ORIGINAL seeds + apply EDITED seeds. Assert the new
     (kcdx_id, valid_from) address_versions row matches A==B in BOTH DBs (id
     excluded), the FINGERPRINT columns are NON-NULL + equal (the promote
     carried them), and the address_names row was added in both.
  2. ADD-ENTITY, NON-FUNCTION KIND (oracle): add a new entity with a data_slot
     kind (rva-bearing) and a vtable_index kind (rva-less). Assert apply's row
     matches rebuild's (id excluded) and the fingerprint columns are NULL.
  3. BASELINE REFUSAL: a function-kind add at an rva with NO bulk row in the DEV
     DB makes apply refuse (SystemExit / non-zero) and writes NOTHING to either
     DB (both byte-unchanged).
  4. ADD-VERSIONS-ROW interval close: the single-baseline-version fixture makes a
     realistic second-game-version row impossible, so the interval-close LOGIC is
     covered by a narrower direct unit test (build the close UPDATE + the INSERT
     against a hand-made DB and assert the prior row's valid_through is set and
     the open-row uniqueness index is not violated). See the test docstring for
     why a full second-version oracle is deferred.
  5. IDEMPOTENCE: re-running apply after an add classifies the now-present row as
     no-op (or re-verify if the trio matches), NOT a duplicate INSERT.

SEED-DIR POINTING + BASELINE FIXTURE
------------------------------------
Reuses test_apply_reverify.py's mechanism verbatim: monkeypatch imp's three
seed-path module constants for the duration of a build (_seeds_pointed_at), and a
module-scoped baseline fixture builds the ~2-min rebuilds ONCE. Path-B applies
run against a fresh per-test copy of the ORIGINAL-seed baseline so each test
starts clean.

RUN
---
    python tests/test_apply_add_entity.py
    pytest tests/test_apply_add_entity.py
"""
import contextlib
import csv
import glob
import os
import shutil
import sqlite3
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
DUMP_DIR = os.path.normpath(
    os.path.join(HERE, "..", "dump", "refdata-1.5.1164953"))
DLL_PATH = os.path.normpath(
    os.path.join(HERE, "..", "..", "..", "third-party-ghidra", "WHGame.dll"))
REPO_ROOT = os.path.normpath(os.path.join(HERE, "..", "..", ".."))
REAL_SEED_DIR = os.path.join(REPO_ROOT, "data", "seeds")

sys.path.insert(0, PYDIR)
import import_to_sqlite as imp  # noqa: E402

SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")
GVT = imp.GAME_VERSION_TAG   # "1.5.1164953"


# --------------------------------------------------------------------------
# Seed-dir pointing (reused from test_apply_reverify.py).
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


def _apply_into(seed_dir, out_dir):
    with _seeds_pointed_at(seed_dir):
        imp.run_apply(out_dir, DLL_PATH)


# --------------------------------------------------------------------------
# Seed-row helpers: read/write the two seed CSVs in a copy.
# --------------------------------------------------------------------------
def _read_csv(path):
    with open(path, newline="", encoding="utf-8") as f:
        rd = csv.DictReader(f)
        return rd.fieldnames, [dict(r) for r in rd]


def _write_csv(path, fields, rows):
    with open(path, "w", newline="", encoding="utf-8") as f:
        wr = csv.DictWriter(f, fieldnames=fields)
        wr.writeheader()
        wr.writerows(rows)


def _max_kcdx_id(seed_dir):
    _, nrows = _read_csv(os.path.join(seed_dir, "address_names_seed.csv"))
    return max(int(r["id"]) for r in nrows)


def _curated_rvas(seed_dir):
    """Set of rvas already curated (in the versions seed) -- a new function-kind
    entity must NOT reuse one (it would collide on (kcdx_id..)/double-promote)."""
    _, vrows = _read_csv(os.path.join(seed_dir, "address_versions_seed.csv"))
    out = set()
    for r in vrows:
        rv = (r.get("rva") or "").strip()
        if rv:
            out.add(int(rv, 16) if rv.startswith("0x") else int(rv))
    return out


def _dump_rva_values(table, valcol):
    """Build {rva: stripped value of `valcol`} across all shards of a dump table.
    Used to find functions that carry a FULL fingerprint (a signatures/ row with
    observed_arg_slots + a caller_reg_args/ row with caller_reg_arg_count)."""
    out = {}
    for sh in sorted(glob.glob(os.path.join(DUMP_DIR, table, f"{table}_*.csv"))):
        with open(sh, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rr = (r.get("rva") or "").strip()
                if not rr:
                    continue
                v = int(rr, 16) if rr.startswith("0x") else int(rr)
                out[v] = (r.get(valcol) or "").strip()
    return out


def _a_free_bulk_function_rva(seed_dir):
    """Pick an rva that (a) is a real bulk function in the dump, (b) is NOT
    already curated, AND (c) carries a FULL bulk fingerprint -- a signatures/ row
    (observed_arg_slots) AND a caller_reg_args/ row (caller_reg_arg_count). The
    fingerprint requirement is what makes the PROMOTE-carries-fingerprint
    assertion meaningful: the very first uncurated function in the dump is a
    short stub with NO caller_reg_args row (its fingerprint columns are
    legitimately NULL), so picking it would make the non-NULL fingerprint check
    fail on a row apply and rebuild AGREE on. Scan functions/ in shard order and
    return the first free rva present in both fingerprint tables."""
    curated = _curated_rvas(seed_dir)
    sigs = _dump_rva_values("signatures", "observed_arg_slots")
    cras = _dump_rva_values("caller_reg_args", "caller_reg_arg_count")
    for shard in sorted(glob.glob(
            os.path.join(DUMP_DIR, "functions", "functions_*.csv"))):
        with open(shard, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rv = (r.get("rva") or "").strip()
                if not rv:
                    continue
                v = int(rv, 16) if rv.startswith("0x") else int(rv)
                if v in curated:
                    continue
                if sigs.get(v) and cras.get(v):
                    return v
    raise SystemExit("no free bulk function rva with a full fingerprint found")


def _a_non_function_rva(seed_dir):
    """An rva that is NOT a bulk function-ENTRY rva (and not already curated) --
    the realistic shape of an rva-bearing non-function kind (a data_slot lives in
    .data, a vtable_base / string_anchor in .rdata; these never coincide with a
    function ENTRY in the functions/ dump). This matters for the oracle: the
    REBUILD seed pass PROMOTES (keeps the bulk fingerprint) whenever a curated rva
    matches a bulk entry REGARDLESS of kind, while apply nulls the fingerprint by
    kind-class. They only AGREE on a non-function kind when its rva does NOT match
    a bulk entry -- which is the real-world case (a data slot's address is never a
    function entry). Returns an rva past the max bulk function entry (guaranteed
    absent from the function set)."""
    curated = _curated_rvas(seed_dir)
    max_fn = 0
    for shard in sorted(glob.glob(
            os.path.join(DUMP_DIR, "functions", "functions_*.csv"))):
        with open(shard, newline="", encoding="utf-8") as f:
            for r in csv.DictReader(f):
                rv = (r.get("rva") or "").strip()
                if not rv:
                    continue
                v = int(rv, 16) if rv.startswith("0x") else int(rv)
                if v > max_fn:
                    max_fn = v
    cand = max_fn + 0x10
    while cand in curated:
        cand += 0x10
    return cand


def _add_entity(seed_dir, name, rva_int, notes, signature, kind):
    """Append a new curated entity: a names row (fresh id, no supersession/
    deprecation) + a versions row at the baseline version with a full audit trio.
    `kind` is an AUTHORED required column on the versions seed (no longer inferred
    from notes), so the caller states it. Returns the new kcdx_id. `rva_int`
    None -> rva-less (vtable_index) row."""
    kid = _max_kcdx_id(seed_dir) + 1

    nfields, nrows = _read_csv(os.path.join(seed_dir, "address_names_seed.csv"))
    nrows.append({c: "" for c in nfields})
    nrows[-1].update({"id": str(kid), "name": name, "notes": notes})
    _write_csv(os.path.join(seed_dir, "address_names_seed.csv"), nfields, nrows)

    vfields, vrows = _read_csv(
        os.path.join(seed_dir, "address_versions_seed.csv"))
    vrows.append({c: "" for c in vfields})
    rva_str = "" if rva_int is None else ("0x%08X" % rva_int)
    vrows[-1].update({
        "kcdx_id": str(kid),
        "valid_from_version": GVT,
        "module": "WHGame.dll",
        "rva": rva_str,
        "kind": kind,
        "signature": signature,
        "last_verified_at_version": GVT,
        "verified_by": "oracle_add_test",
        "verified_date": "2099-12-31",
        "evidence_kind": "maintainer_ghidra",
    })
    _write_csv(os.path.join(seed_dir, "address_versions_seed.csv"),
               vfields, vrows)
    return kid


# --------------------------------------------------------------------------
# DB readers for the oracle comparison.
# --------------------------------------------------------------------------
# Fingerprint columns the function-kind promote carries from the bulk row.
_FINGERPRINT_COLS = ("length", "content_hash", "observed_arg_slots",
                     "caller_reg_arg_count", "caller_arg_agreement")


def _dict_id_to_val(con, table, col):
    return {r[0]: r[1] for r in con.execute(
        f'SELECT id, val FROM "_dict_{table}_{col}"')}


def _av_row(db_path, kcdx_id, valid_from_tag):
    """Return the curated address_versions row for (kcdx_id, valid_from-tag) as a
    {col: value} dict with `id` EXCLUDED and dict-encoded columns (kind,
    caller_arg_agreement) DECODED to their strings (so dict-id numbering
    differences between an apply DB and a rebuild DB never cause a false
    mismatch). evidence_kind too. content_hash kept as bytes (comparable)."""
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
        # Decode the dict-encoded columns present in this projection.
        for dc in ("kind", "caller_arg_agreement", "evidence_kind"):
            if dc in d and d[dc] is not None:
                d[dc] = _dict_id_to_val(con, "address_versions", dc).get(d[dc])
        # Normalize valid_from / last_verified_at_version / valid_through to the
        # game_versions TAG so a different version-id numbering can't differ.
        gv = {r[0]: r[1] for r in con.execute("SELECT id, tag FROM game_versions")}
        for vc in ("valid_from", "valid_through", "last_verified_at_version"):
            if vc in d and d[vc] is not None:
                d[vc] = gv.get(d[vc], d[vc])
        d.pop("id", None)            # id excluded (autoincrement handle)
        d.pop("module_id", None)     # module fk id can differ; not load-bearing here
        return d
    finally:
        con.close()


def _names_row(db_path, kcdx_id):
    con = sqlite3.connect(db_path)
    try:
        cols = [c[1] for c in con.execute('PRAGMA table_info("address_names")')]
        row = con.execute(
            f'SELECT {",".join(chr(34)+c+chr(34) for c in cols)} '
            f'FROM address_names WHERE id = ?', (kcdx_id,)).fetchone()
        return dict(zip(cols, row)) if row else None
    finally:
        con.close()


def _count_rows(db_path, kcdx_id):
    con = sqlite3.connect(db_path)
    try:
        return con.execute(
            "SELECT COUNT(*) FROM address_versions WHERE kcdx_id = ?",
            (kcdx_id,)).fetchone()[0]
    finally:
        con.close()


def _hash_table(db_path, table):
    import hashlib
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


# --------------------------------------------------------------------------
# Module-scoped baseline: the ORIGINAL-seed rebuild (Path-B's starting DB) built
# ONCE and copied per-test. Edited-seed rebuilds (Path A) are built per-test
# because each test edits a different entity.
# --------------------------------------------------------------------------
_BASELINES = {}


def _require_inputs():
    if not os.path.isdir(DUMP_DIR):
        raise SystemExit(f"dump dir not found: {DUMP_DIR}")
    if not os.path.isfile(DLL_PATH):
        raise SystemExit(f"WHGame.dll not found: {DLL_PATH}")


def _build_baselines(root):
    _require_inputs()
    orig_seed = os.path.join(root, "orig_seed")
    _copy_seeds(orig_seed)
    orig_out = os.path.join(root, "rebuild_orig")
    _rebuild_into(orig_seed, orig_out)
    return {"orig_seed": orig_seed, "orig_out": orig_out}


def _get_baselines():
    if "root" not in _BASELINES:
        root = tempfile.mkdtemp(prefix="add_oracle_")
        _BASELINES["root"] = root
        _BASELINES.update(_build_baselines(root))
    return _BASELINES


def _cleanup_baselines():
    root = _BASELINES.get("root")
    if root:
        shutil.rmtree(root, ignore_errors=True)
        _BASELINES.clear()


def _fresh_apply_baseline(b):
    """A per-test copy of the ORIGINAL-seed rebuild (so apply writes into a clean
    starting DB each test). Copies the prebuilt DBs rather than re-running the
    ~2-min rebuild."""
    out = tempfile.mkdtemp(prefix="add_applyB_")
    for f in ("reference.sqlite", "reference-dev.sqlite"):
        shutil.copy2(os.path.join(b["orig_out"], f), os.path.join(out, f))
    return out


try:
    import pytest

    @pytest.fixture(scope="module")
    def baselines():
        b = _get_baselines()
        yield b
        _cleanup_baselines()
except ImportError:   # pragma: no cover
    pytest = None


# --------------------------------------------------------------------------
# Test 1: add-entity, function kind -> PROMOTE (fingerprint carried).
# --------------------------------------------------------------------------
def _add_entity_function_oracle(b):
    edit_seed = tempfile.mkdtemp(prefix="add_fn_seed_")
    rebuild_edit = tempfile.mkdtemp(prefix="add_fn_rebuild_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        rva = _a_free_bulk_function_rva(edit_seed)
        kid = _add_entity(edit_seed, "oracle_added_fn", rva,
                          notes="A brand-new curated function for the add oracle.",
                          signature="i32 (ptr a, i32 b)", kind="function")

        # Path A: rebuild the EDITED seeds (ground truth).
        _rebuild_into(edit_seed, rebuild_edit)
        # Path B: rebuild ORIGINAL (prebuilt copy) + apply EDITED.
        apply_out = _fresh_apply_baseline(b)
        _apply_into(edit_seed, apply_out)

        for label, a_db, b_db in (
                ("user", os.path.join(rebuild_edit, "reference.sqlite"),
                 os.path.join(apply_out, "reference.sqlite")),
                ("dev", os.path.join(rebuild_edit, "reference-dev.sqlite"),
                 os.path.join(apply_out, "reference-dev.sqlite"))):
            a_row = _av_row(a_db, kid, GVT)
            b_row = _av_row(b_db, kid, GVT)
            assert a_row is not None, f"[{label}] rebuild missing the added fn row"
            assert b_row is not None, f"[{label}] apply missing the added fn row"
            assert a_row == b_row, (
                f"[{label}] added-fn row apply != rebuild (id excluded):\n"
                f"  rebuild={a_row}\n  apply  ={b_row}\n"
                f"  diff keys={[k for k in a_row if a_row.get(k)!=b_row.get(k)]}")
            # The promote carried the fingerprint: NON-NULL + equal.
            for fc in _FINGERPRINT_COLS:
                assert b_row.get(fc) is not None, (
                    f"[{label}] fingerprint col {fc} is NULL on a promoted "
                    f"function (promote did not carry the bulk fingerprint)")
                assert a_row.get(fc) == b_row.get(fc), (
                    f"[{label}] fingerprint {fc}: rebuild {a_row.get(fc)} != "
                    f"apply {b_row.get(fc)}")
            # The address_names row was added in both.
            assert _names_row(a_db, kid) is not None, f"[{label}] rebuild no name row"
            assert _names_row(b_db, kid) is not None, f"[{label}] apply no name row"
            assert _names_row(a_db, kid) == _names_row(b_db, kid), (
                f"[{label}] names row apply != rebuild")
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        shutil.rmtree(rebuild_edit, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 2: add-entity, non-function kinds -> MINT (fingerprint NULL).
# --------------------------------------------------------------------------
def _add_entity_nonfunction_oracle(b):
    edit_seed = tempfile.mkdtemp(prefix="add_nf_seed_")
    rebuild_edit = tempfile.mkdtemp(prefix="add_nf_rebuild_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        # (b) data_slot -- rva-bearing non-function kind. Use a NON-function-entry
        # rva (the realistic shape: a data slot lives in .data, never at a function
        # entry). The kind-class forces the fingerprint NULL in apply; the rebuild
        # only agrees (mints NULL) because the rva does NOT match a bulk entry --
        # the real-world case. (A non-function kind on a function-entry rva is a
        # rebuild-vs-apply divergence by design; it does not occur for a fresh
        # add at a data address. See _a_non_function_rva.)
        rva = _a_non_function_rva(edit_seed)
        kid_ds = _add_entity(
            edit_seed, "oracle_added_data_slot", rva,
            notes="A static .data pointer slot for the add oracle. data slot.",
            signature="", kind="data_slot")
        # (c) vtable_index -- rva-less. notes carry a slot int the deriver parses.
        kid_vt = _add_entity(
            edit_seed, "oracle_added_vtable_idx", None,
            notes="vtable index = 7 (0-indexed) for the add oracle.",
            signature="", kind="vtable_index")

        _rebuild_into(edit_seed, rebuild_edit)
        apply_out = _fresh_apply_baseline(b)
        _apply_into(edit_seed, apply_out)

        for label, a_db, b_db in (
                ("user", os.path.join(rebuild_edit, "reference.sqlite"),
                 os.path.join(apply_out, "reference.sqlite")),
                ("dev", os.path.join(rebuild_edit, "reference-dev.sqlite"),
                 os.path.join(apply_out, "reference-dev.sqlite"))):
            for kid, knd in ((kid_ds, "data_slot"), (kid_vt, "vtable_index")):
                a_row = _av_row(a_db, kid, GVT)
                b_row = _av_row(b_db, kid, GVT)
                assert a_row is not None and b_row is not None, (
                    f"[{label}] {knd} row missing (rebuild={a_row is not None}, "
                    f"apply={b_row is not None})")
                assert a_row == b_row, (
                    f"[{label}] {knd} row apply != rebuild (id excluded):\n"
                    f"  rebuild={a_row}\n  apply  ={b_row}")
                assert b_row.get("kind") == knd, (
                    f"[{label}] {knd} row kind decoded to {b_row.get('kind')!r}")
                for fc in _FINGERPRINT_COLS:
                    assert b_row.get(fc) is None, (
                        f"[{label}] {knd} fingerprint col {fc} is "
                        f"{b_row.get(fc)!r}, expected NULL (non-function mint)")
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        shutil.rmtree(rebuild_edit, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 3: baseline refusal -- function-kind add at an rva with no bulk row.
# --------------------------------------------------------------------------
def _baseline_refusal(b):
    edit_seed = tempfile.mkdtemp(prefix="add_refuse_seed_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        # A fabricated rva that is NOT a bulk function in the dump. The dump's
        # functions are well below 0x7FFFFFF0; this is past the image -> no bulk
        # row -> a function-kind add must REFUSE (never mint a NULL-fp function).
        # `signature` set -> infer_kind() -> "function".
        bogus = 0x7FFFFFF0
        assert bogus not in _curated_rvas(edit_seed)
        _add_entity(edit_seed, "oracle_no_baseline_fn", bogus,
                    notes="A function with no bulk baseline (refusal test).",
                    signature="void (ptr a)")

        apply_out = _fresh_apply_baseline(b)
        user_db = os.path.join(apply_out, "reference.sqlite")
        dev_db = os.path.join(apply_out, "reference-dev.sqlite")
        before_u = _hash_table(user_db, "address_versions")
        before_d = _hash_table(dev_db, "address_versions")
        before_un = _hash_table(user_db, "address_names")
        before_dn = _hash_table(dev_db, "address_names")

        raised = False
        try:
            _apply_into(edit_seed, apply_out)
        except (SystemExit, imp.BaselineRefusal, RuntimeError):
            raised = True
        assert raised, "apply did not refuse a function-kind add with no baseline"

        # Neither DB gained a row (both tables byte-unchanged).
        assert before_u == _hash_table(user_db, "address_versions"), \
            "user address_versions changed despite baseline refusal"
        assert before_d == _hash_table(dev_db, "address_versions"), \
            "dev address_versions changed despite baseline refusal"
        assert before_un == _hash_table(user_db, "address_names"), \
            "user address_names changed despite baseline refusal"
        assert before_dn == _hash_table(dev_db, "address_names"), \
            "dev address_names changed despite baseline refusal"
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 4: add-versions-row interval close (narrower direct unit test).
#
# WHY NOT A FULL ORACLE: the Phase-1 baseline + seeds carry exactly ONE
# game_versions row (GAME_VERSION_TAG). add-versions-row models a moved/renamed
# function at a NEW game version -- it needs a SECOND game_versions row to be
# realistic, and the rebuild path materializes only baseline-version seed rows
# (build_rows skips vfv != GAME_VERSION_TAG), so a 2nd-version seed row would not
# even appear in a rebuild -> no apply-equals-rebuild oracle is constructible
# until a 2nd game version exists. (SURFACED in the step report.) Until then the
# interval-close LOGIC is asserted directly: with a hand-made DB carrying one
# open curated row, run apply's close-UPDATE + INSERT shape and assert (a) the
# prior row's valid_through is set and (b) the partial-unique open-row index
# accepts the new open row (no violation).
# --------------------------------------------------------------------------
def _interval_close_logic(b):
    out = tempfile.mkdtemp(prefix="add_close_")
    try:
        dbp = os.path.join(out, "t.sqlite")
        con = sqlite3.connect(dbp)
        # Minimal address_versions table + the REAL partial-unique open index
        # (copied verbatim from write_db): the constraint this test guards.
        con.execute("CREATE TABLE address_versions ("
                    "id INTEGER PRIMARY KEY AUTOINCREMENT, kcdx_id INTEGER, "
                    "rva INTEGER, valid_from INTEGER, valid_through INTEGER)")
        con.execute(
            "CREATE UNIQUE INDEX ix_av_open_unique ON address_versions(kcdx_id) "
            "WHERE kcdx_id IS NOT NULL AND valid_through IS NULL")
        # An existing OPEN curated row for kcdx_id=42 at version 1.
        con.execute("INSERT INTO address_versions (kcdx_id, rva, valid_from, "
                    "valid_through) VALUES (42, 0x1000, 1, NULL)")
        con.commit()

        # apply's add-versions-row shape: close the prior open interval FIRST,
        # then insert the new open row at version 2. (If the close were skipped,
        # the INSERT would trip ix_av_open_unique -- two open rows for kcdx 42.)
        prev_vf = con.execute(
            "SELECT valid_from FROM address_versions WHERE kcdx_id=42 AND "
            "valid_through IS NULL").fetchone()[0]
        con.execute("BEGIN")
        con.execute("UPDATE address_versions SET valid_through = ? "
                    "WHERE kcdx_id = ? AND valid_through IS NULL", (prev_vf, 42))
        con.execute("INSERT INTO address_versions (kcdx_id, rva, valid_from, "
                    "valid_through) VALUES (42, 0x2000, 2, NULL)")
        con.execute("COMMIT")   # would raise IntegrityError if the index tripped

        # The prior row's valid_through is now set (interval closed)...
        closed = con.execute(
            "SELECT valid_through FROM address_versions WHERE kcdx_id=42 AND "
            "valid_from=1").fetchone()[0]
        assert closed == prev_vf, f"prior row not closed (valid_through={closed})"
        # ...and exactly one OPEN row remains (the new version-2 row).
        open_rows = con.execute(
            "SELECT valid_from FROM address_versions WHERE kcdx_id=42 AND "
            "valid_through IS NULL").fetchall()
        assert open_rows == [(2,)], f"expected one open row at v2, got {open_rows}"
        con.close()

        # Negative control: skipping the close DOES trip the index (proves the
        # close is what protects it, not luck).
        con2 = sqlite3.connect(dbp)
        tripped = False
        try:
            con2.execute("INSERT INTO address_versions (kcdx_id, rva, "
                         "valid_from, valid_through) VALUES (42, 0x3000, 3, NULL)")
            con2.commit()
        except sqlite3.IntegrityError:
            tripped = True
        finally:
            con2.close()
        assert tripped, "a 2nd open row for kcdx 42 did NOT trip ix_av_open_unique"
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --------------------------------------------------------------------------
# Test 5: idempotence -- re-applying after an add does NOT duplicate.
# --------------------------------------------------------------------------
def _add_idempotence(b):
    edit_seed = tempfile.mkdtemp(prefix="add_idem_seed_")
    apply_out = None
    try:
        _copy_seeds(edit_seed)
        rva = _a_free_bulk_function_rva(edit_seed)
        kid = _add_entity(edit_seed, "oracle_idem_fn", rva,
                          notes="Idempotence add-oracle function.",
                          signature="void (ptr a)", kind="function")

        apply_out = _fresh_apply_baseline(b)
        _apply_into(edit_seed, apply_out)          # first apply: does the add
        user_db = os.path.join(apply_out, "reference.sqlite")
        dev_db = os.path.join(apply_out, "reference-dev.sqlite")
        assert _count_rows(user_db, kid) == 1, "user: add did not create one row"
        assert _count_rows(dev_db, kid) == 1, "dev: add did not create one row"
        before_u = _hash_table(user_db, "address_versions")
        before_d = _hash_table(dev_db, "address_versions")

        _apply_into(edit_seed, apply_out)          # second apply: must no-op
        assert _count_rows(user_db, kid) == 1, "user: re-apply DUPLICATED the row"
        assert _count_rows(dev_db, kid) == 1, "dev: re-apply DUPLICATED the row"
        assert before_u == _hash_table(user_db, "address_versions"), \
            "user DB changed on idempotent re-apply of an add"
        assert before_d == _hash_table(dev_db, "address_versions"), \
            "dev DB changed on idempotent re-apply of an add"
    finally:
        shutil.rmtree(edit_seed, ignore_errors=True)
        if apply_out:
            shutil.rmtree(apply_out, ignore_errors=True)


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_add_entity_function_kind_oracle(baselines):  # noqa: F811
    _add_entity_function_oracle(baselines)


def test_add_entity_nonfunction_kind_oracle(baselines):  # noqa: F811
    _add_entity_nonfunction_oracle(baselines)


def test_baseline_refusal_no_bulk_row(baselines):  # noqa: F811
    _baseline_refusal(baselines)


def test_add_versions_row_interval_close(baselines):  # noqa: F811
    _interval_close_logic(baselines)


def test_add_is_idempotent(baselines):  # noqa: F811
    _add_idempotence(baselines)


if __name__ == "__main__":
    try:
        b = _get_baselines()
        _add_entity_function_oracle(b)
        print("PASS test_add_entity_function_kind_oracle")
        _add_entity_nonfunction_oracle(b)
        print("PASS test_add_entity_nonfunction_kind_oracle")
        _baseline_refusal(b)
        print("PASS test_baseline_refusal_no_bulk_row")
        _interval_close_logic(b)
        print("PASS test_add_versions_row_interval_close")
        _add_idempotence(b)
        print("PASS test_add_is_idempotent")
        print("\nall apply add-entity oracle tests passed")
    finally:
        _cleanup_baselines()
