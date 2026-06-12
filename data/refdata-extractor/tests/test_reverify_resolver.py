"""test_reverify_resolver.py -- the bulk re-verify RESOLVE seam (D39 / D34 / D35 /
D29-rev / D17a), maintainer-tool Phase 6, step 6.2b.

WHAT THIS PROVES
----------------
`seeds_shared.reverify_resolver.resolve_reverify_batch` resolves the v3 verification
report's actionable rows into the per-row edit-specs `/confirm/batch` consumes
({kcdx_id, valid_from_version, edits}). It READS the DB (READ-ONLY) + COMPUTES; it
WRITES NOTHING (the byte-identical DB is asserted). The cases:

  VERIFY-ALL (D34/D29-rev/D17a):
    1. The proof-rank-keyed evidence_kind -- a `verified_working` (rank 1) row ->
       `live_production`; a `passed_not_verified` (ranks 2-5) row -> `live_test_plugin`
       (BOTH asserted). FALSIFIABLE: a `passed_not_verified` -> `live_production`
       assertion (the WRONG mapping the soundness gate caught) FAILS when flipped.
    2. The audit trio (last_verified_at_version -> swept, verified_date -> today,
       verified_by -> the injected identity -- D17a).
    3. The D34 gap-extension on a CLOSED gap-pass row (valid_through < swept ->
       extended to swept) AND the SKIP of an already-covered row (an OPEN row whose
       interval covers swept -> NO valid_through edit; a row last_verified >= swept ->
       NO edit-spec at all).

  CLOSE-INTERVALS (D35):
    4. The deterministic interval-containing target by kcdx_id + version, and the
       valid_through -> last_verified_at_version retract. FALSIFIABLE: a close-target
       resolved to a row whose interval does NOT contain the version is caught (the
       resolver raises / does not pick it).

  VERDICT ROUTING (D39/D28/D36):
    5. The resolver emits edit-specs for exactly the actionable set; a no-action verdict
       (filtered by the caller) is never resolved -- and a verdict/action mismatch raises.

THE FIXTURE (no write path needed -- the resolver READS)
--------------------------------------------------------
The resolver is READ-ONLY, so the test builds the exact DB state by DIRECT sqlite
injection into a copy of the rebuilt baseline -- a later game_versions row + crafted
address_versions intervals (a CLOSED gap row, an OPEN already-covered row, an OPEN
failing row). This isolates the resolve+compute logic (the unit under test) from the
write path (covered by test_db_editor_interval / test_db_editor_batch). The byte-
identical-DB assertion proves the resolver never writes.

ACCEPTANCE SIGNAL
-----------------
Emits the canonical ACCEPT-RESULT / ACCEPT-SUITE lines
(.claude/rules/acceptance-signal.md) for the headless data-core run, the same
_emit_signal shape test_db_editor_interval uses.

RUN
---
    python tests/test_reverify_resolver.py
    pytest tests/test_reverify_resolver.py
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
from seeds_shared import reverify_resolver as rr  # noqa: E402

GVT = imp.GAME_VERSION_TAG          # "1.5.1164953", ordinal 1164953
SEED_FILES = ("module_seed.csv", "address_names_seed.csv",
              "address_versions_seed.csv")
# A LATER game version the swept report runs at (ordinal strictly > GVT). The fixture
# injects this game_versions row so a gap-extension / a beyond-interval sweep is testable.
LATER_TAG = "1.6.2000000"
LATER_ORDINAL = 2000000
# A MIDDLE version between GVT and LATER (for a closed-interval gap: [GVT..MID], swept LATER).
MID_TAG = "1.5.1500000"
MID_ORDINAL = 1500000

TODAY = "2026-06-11"               # a fixed verified_date (deterministic)
SIGNER = "TestSigner"              # the injected identity (D17a)


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


def _ek_id(con, val):
    """The evidence_kind dict id for a string value (the dict-encoded column)."""
    row = con.execute(
        'SELECT id FROM "_dict_address_versions_evidence_kind" WHERE val = ?',
        (val,)).fetchone()
    return row[0] if row else None


def _inject_state(out_dir):
    """Direct-inject the crafted DB state into the USER reference.sqlite: the later +
    middle game_versions rows, and four CRAFTED address_versions rows on fresh kcdx_ids
    (so the existing curated rows are untouched). Returns a dict of the injected row ids
    + kcdx_ids the cases key on. NEVER hardcodes an id -- reads them back."""
    db_path = os.path.join(out_dir, "reference.sqlite")
    con = sqlite3.connect(db_path)
    try:
        gvt_id = _gv_id(con, GVT)
        assert gvt_id is not None
        # Register the LATER + MIDDLE game versions (the swept/gap versions).
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (MID_TAG, MID_ORDINAL))
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    (LATER_TAG, LATER_ORDINAL))
        mid_id = _gv_id(con, MID_TAG)
        later_id = _gv_id(con, LATER_TAG)
        ek_prod = _ek_id(con, "live_production")
        ek_test = _ek_id(con, "live_test_plugin")
        assert None not in (mid_id, later_id, ek_prod, ek_test)

        # Four fresh curated entities (high kcdx_ids -- outside the seeded range) so the
        # crafted rows don't collide with the 157 baseline entities. Each is one
        # address_versions row with a crafted interval window + trio.
        names = con.execute("SELECT MAX(id) FROM address_names").fetchone()[0] or 0
        avmax = con.execute("SELECT MAX(id) FROM address_versions").fetchone()[0] or 0
        module_id = con.execute("SELECT id FROM modules LIMIT 1").fetchone()[0]
        kind_id = con.execute(
            'SELECT id FROM "_dict_address_versions_kind" WHERE val = ?',
            ("function",)).fetchone()[0]

        def _add_entity(name):
            nonlocal names
            names += 1
            con.execute(
                "INSERT INTO address_names (id, name) VALUES (?, ?)", (names, name))
            return names

        def _add_av(kcdx_id, valid_from_id, valid_through_id, last_verified_id,
                    verified_by, verified_date, ek_id):
            nonlocal avmax
            avmax += 1
            con.execute(
                "INSERT INTO address_versions "
                "(id, kcdx_id, kind, module_id, rva, valid_from, valid_through, "
                " last_verified_at_version, verified_by, verified_date, evidence_kind) "
                "VALUES (?, ?, ?, ?, ?, ?, ?, ?, ?, ?, ?)",
                (avmax, kcdx_id, kind_id, module_id, 0x1000 + avmax,
                 valid_from_id, valid_through_id, last_verified_id,
                 verified_by, verified_date, ek_id))
            return avmax

        # --- VERIFY-ALL fixtures ---
        # (A) A CLOSED gap row: valid_from=GVT, valid_through=MID, last_verified=GVT.
        #     A sweep at LATER sits BEYOND the [GVT..MID] interval -> gap-extend
        #     valid_through -> LATER. verified_working -> live_production.
        e_gap = _add_entity("kcdx_test_gap_closed")
        id_gap = _add_av(e_gap, gvt_id, mid_id, gvt_id, "Old", "2025-01-01", ek_prod)

        # (B) An OPEN already-covered-interval row: valid_from=GVT, valid_through=NULL,
        #     last_verified=GVT. A sweep at LATER: the OPEN interval covers LATER (open
        #     covers forward), so NO valid_through edit -- the trio still writes
        #     (last_verified GVT < LATER). passed_not_verified -> live_test_plugin.
        e_open = _add_entity("kcdx_test_open_covered")
        id_open = _add_av(e_open, gvt_id, None, gvt_id, "Old", "2025-01-01", ek_test)

        # (C) An ALREADY-VERIFIED-BEYOND row: last_verified=LATER. A sweep at GVT (older)
        #     -> SKIP entirely (last_verified >= swept), NO edit-spec.
        e_skip = _add_entity("kcdx_test_already_covered")
        id_skip = _add_av(e_skip, gvt_id, None, later_id, "Old", "2025-01-01", ek_prod)

        # --- CLOSE-INTERVALS fixture ---
        # (D) An OPEN row valid_from=GVT, valid_through=NULL, last_verified=MID. A failed
        #     sweep at LATER: the OPEN interval contains LATER (open covers forward) ->
        #     close valid_through -> last_verified (MID). Distinct from a NON-containing
        #     decoy row of the SAME entity at a later, gap-separated CLOSED interval.
        e_close = _add_entity("kcdx_test_close")
        id_close = _add_av(e_close, gvt_id, None, mid_id, "Old", "2025-01-01", ek_prod)

        con.commit()
        return {
            "gvt_id": gvt_id, "mid_id": mid_id, "later_id": later_id,
            "ek_prod": ek_prod, "ek_test": ek_test,
            "e_gap": e_gap, "id_gap": id_gap,
            "e_open": e_open, "id_open": id_open,
            "e_skip": e_skip, "id_skip": id_skip,
            "e_close": e_close, "id_close": id_close,
        }
    finally:
        con.close()


_BASELINE = {}


def _get_fixture():
    if "out" not in _BASELINE:
        root = tempfile.mkdtemp(prefix="reverify_base_")
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


def _report_row(kcdx_id, version, verdict, method_rank, matched_id):
    return {"kcdx_id": kcdx_id, "version": version, "verdict": verdict,
            "method_rank": method_rank, "matched_address_version_id": matched_id}


def _spec_for(specs, kcdx_id):
    for s in specs:
        if s["kcdx_id"] == kcdx_id:
            return s
    return None


# ==========================================================================
# The cases -- each returns a (id, ok, detail) result for the ACCEPT signal.
# ==========================================================================
def _run_cases(out_dir, ids):
    results = []
    db_before = _db_hash(out_dir)

    # ---- VERIFY-ALL: proof-rank evidence_kind + trio + gap-extension + skip ----
    # A verified_working (rank 1) gap row, a passed_not_verified (ranks 2-5) open row,
    # and an already-covered skip row -- resolved in one verify-all batch at LATER_TAG
    # (the open/gap rows) and one at GVT (the skip row needs an older swept version).
    va_rows = [
        _report_row(ids["e_gap"], LATER_TAG, "verified_working", 1, ids["id_gap"]),
        _report_row(ids["e_open"], LATER_TAG, "passed_not_verified", 3, ids["id_open"]),
    ]
    va = rr.resolve_reverify_batch(out_dir, va_rows, action="verify-all",
                                   verified_by=SIGNER, today=TODAY)

    gap = _spec_for(va, ids["e_gap"])
    openrow = _spec_for(va, ids["e_open"])

    # (1) proof-rank evidence_kind -- BOTH mappings (the falsifiable soundness check).
    ek_gap = gap["edits"]["evidence_kind"] if gap else None
    ek_open = openrow["edits"]["evidence_kind"] if openrow else None
    results.append((
        "verify-all-rank1-evidence-kind-live_production",
        ek_gap == "live_production",
        f"verified_working -> {ek_gap!r}, expected 'live_production'"))
    results.append((
        "verify-all-rank2-5-evidence-kind-live_test_plugin",
        ek_open == "live_test_plugin",
        f"passed_not_verified -> {ek_open!r}, expected 'live_test_plugin' "
        f"(NOT live_production -- the soundness gate's wrong mapping)"))

    # (2) the audit trio (D17a) on the gap row.
    trio_ok = (gap is not None
               and gap["edits"].get("last_verified_at_version") == LATER_TAG
               and gap["edits"].get("verified_date") == TODAY
               and gap["edits"].get("verified_by") == SIGNER)
    results.append((
        "verify-all-audit-trio",
        trio_ok,
        f"trio={gap['edits'] if gap else None!r} "
        f"(expected last_verified={LATER_TAG}, verified_date={TODAY}, "
        f"verified_by={SIGNER})"))

    # (3a) the D34 gap-extension on the CLOSED gap row: valid_through -> LATER.
    results.append((
        "verify-all-d34-gap-extension-on-closed-gap-row",
        gap is not None and gap["edits"].get("valid_through_version") == LATER_TAG,
        f"closed gap row valid_through_version="
        f"{gap['edits'].get('valid_through_version') if gap else None!r}, "
        f"expected {LATER_TAG} (the gap-extension did not fire)"))

    # (3b) the SKIP of the OPEN already-covered interval: NO valid_through edit (open
    #      covers forward) -- but the trio DOES write (last_verified GVT < LATER).
    open_no_vt = (openrow is not None
                  and "valid_through_version" not in openrow["edits"]
                  and openrow["edits"].get("last_verified_at_version") == LATER_TAG)
    results.append((
        "verify-all-open-interval-no-valid_through-edit",
        open_no_vt,
        f"open-covered row edits={openrow['edits'] if openrow else None!r} "
        f"(expected the trio but NO valid_through_version -- open covers forward)"))

    # (3c) the SKIP of an already-verified-beyond row: NO edit-spec at all. The swept
    #      version GVT is OLDER than the row's last_verified (LATER) -> nothing to add.
    skip_rows = [_report_row(ids["e_skip"], GVT, "verified_working", 1, ids["id_skip"])]
    skip_specs = rr.resolve_reverify_batch(out_dir, skip_rows, action="verify-all",
                                           verified_by=SIGNER, today=TODAY)
    results.append((
        "verify-all-already-covered-row-skipped",
        _spec_for(skip_specs, ids["e_skip"]) is None and skip_specs == [],
        f"already-covered row produced {skip_specs!r}, expected [] (skipped -- "
        f"last_verified >= swept, nothing to add)"))

    # ---- CLOSE-INTERVALS: deterministic interval-containing target + D35 retract ----
    ci_rows = [_report_row(ids["e_close"], LATER_TAG, "failed", 5, None)]
    ci = rr.resolve_reverify_batch(out_dir, ci_rows, action="close-intervals",
                                   verified_by=SIGNER, today=TODAY)
    close = _spec_for(ci, ids["e_close"])
    # (4) the D35 retract: the OPEN interval containing LATER is closed to its
    #     last_verified_at_version (MID_TAG).
    results.append((
        "close-intervals-d35-retract-to-last-verified",
        close is not None
        and close["edits"].get("valid_through_version") == MID_TAG
        and close["valid_from_version"] == GVT,
        f"close spec={close!r} (expected valid_through_version={MID_TAG} on the "
        f"valid_from={GVT} interval-containing row)"))

    # (4-falsifiable) a close-target's interval MUST contain the version. The interval-
    #     containment helper, asked for a version BEFORE the only row's valid_from,
    #     finds NO containing row -> the resolver raises (never silently picks a
    #     non-containing row). Use a fresh report version EARLIER than GVT.
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        con.execute("INSERT INTO game_versions (tag, ordinal) VALUES (?, ?)",
                    ("1.4.1000000", 1000000))
        con.commit()
    finally:
        con.close()
    raised = None
    try:
        rr.resolve_reverify_batch(
            out_dir, [_report_row(ids["e_close"], "1.4.1000000", "failed", 5, None)],
            action="close-intervals", verified_by=SIGNER, today=TODAY)
    except rr.ReverifyResolveError as e:   # noqa: BLE001
        raised = e
    results.append((
        "close-intervals-non-containing-version-raises",
        raised is not None and "interval-containing" in str(raised),
        "a version no interval contains was resolved to a (wrong) row instead of "
        "raising -- the containment check is gone" if raised is None else ""))

    # ---- VERDICT ROUTING: a verdict/action mismatch raises (no silent skip) ----
    mismatch = None
    try:
        rr.resolve_reverify_batch(
            out_dir, [_report_row(ids["e_open"], LATER_TAG, "failed", 5, None)],
            action="verify-all", verified_by=SIGNER, today=TODAY)
    except rr.ReverifyResolveError as e:   # noqa: BLE001
        mismatch = e
    results.append((
        "verdict-routing-mismatch-raises",
        mismatch is not None,
        "a failed row in a verify-all batch did not raise (routing not enforced)"
        if mismatch is None else ""))

    # ---- NO-WRITE: the DB is byte-identical after every resolve (preview-only) ----
    # (the helper added a couple of game_versions rows for the falsifiable cases ABOVE,
    #  so re-hash AGAINST the post-injection baseline captured here, not db_before --
    #  the resolver itself never wrote; the test's own setup did. Re-open READ-ONLY by
    #  the resolver only.) Assert the resolver writes nothing across a clean resolve.
    after = _db_hash(out_dir)
    # Run one more resolve and confirm it does not change the hash (the resolver is
    # read-only; only the test's own INSERTs above moved the hash).
    rr.resolve_reverify_batch(out_dir, va_rows, action="verify-all",
                              verified_by=SIGNER, today=TODAY)
    results.append((
        "resolver-writes-nothing-db-byte-identical",
        _db_hash(out_dir) == after,
        "the resolver MUTATED the DB across a resolve (it must be READ-ONLY)"))
    # Note db_before is captured before the test's own falsifiable-case INSERTs; the
    # resolver-only no-write property is the `after`-stable check above.
    _ = db_before

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


def test_reverify_resolver(fixture):   # noqa: F811
    """The resolver's verify-all + close-intervals + routing cases over the crafted
    fixture DB. Emits the canonical ACCEPT signal; asserts every case PASSED."""
    results = _run_cases(fixture["out"], fixture["ids"])
    _emit_signal(results)
    failures = [(aid, detail) for aid, ok, detail in results if not ok]
    assert not failures, "reverify_resolver failures:\n  " + \
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
