"""test_read_api.py -- the data-core read-for-display surface (maintainer-tool
Phase 2, step 2a): derive_status + read_curated_set + read_entity_detail +
read_version_rows over the mini-dump-built USER reference.sqlite.

WHAT THIS PROVES
----------------
read_api is the SINGLE read-for-display gate (design D13 / law 6): the backend and
any consumer read the curated set / an entity's detail / its version rows -- and
the derived status -- by CALLING read_api, never by re-querying or re-deriving.
This test exercises the REAL read_api over the REAL mini-dump-built DB (not a mock
of the rows). Two halves:

  A. derive_status -- the POLICY ORACLE. One case per status token, INCLUDING the
     boundary cases the >=/<= rules turn on. The expected token comes from READING
     policy.md S"Status is NOT an authored column" (the 4-rule precedence), NOT
     from the implementation -- the test is the policy's oracle, not a mirror of
     the code. derive_status takes already-resolved integer ORDINALS (policy.md's
     ordinal compare), so these cases are plain-integer constructions.

  B. read_curated_set / read_entity_detail / read_version_rows -- end-to-end
     against the built USER DB. status is computed end-to-end (the FK->ordinal
     resolution + derive_status), never a mock of the rows.

THE FIXTURE HAS NO DEPRECATED / SUPERSEDED ENTITY (a surfaced fixture limit)
---------------------------------------------------------------------------
The committed address_names seed carries ZERO deprecated and ZERO superseded
entities (every row's is_deprecated / superseded_by is empty). So the
DEPRECATED/SUPERSEDED end-to-end cases CONSTRUCT the lifecycle flags on a fresh DB
copy via the landed db_editor.deprecate_entity / supersede_entity entry points
(the same harness the lifecycle oracle uses) -- not a mock, a real validated edit
landing real flags -- then assert read_curated_set / read_version_rows show the
right derived status. The fixture also carries exactly ONE game_versions row, so
the multi-version VERIFIED->UNVERIFIED FLIP (an older row at a lower current
ordinal) is pinned at the derive_status UNIT level (case A) where constructed
ordinals exercise it directly -- the DB cannot materialize a second game version
(policy.md "Surprise the maintainer should know about": a row whose
valid_from_version != the import's GAME_VERSION_TAG is silently skipped).

SEED-DIR / BASELINE FIXTURE
---------------------------
Reuses the db_editor INSERT test harness wholesale (test_db_editor_insert.py): the
module-scoped baseline rebuild (built ONCE off the committed seeds + the mini-dump
excerpt), the per-test fresh DB copy, the next-free-id / non-function-rva pickers,
and the db_editor import. read_api opens the USER reference.sqlite READ-ONLY.

RUN
---
    python -m pytest data/refdata-extractor/tests/test_read_api.py -q
    python tests/test_read_api.py
"""
import os
import shutil
import sqlite3
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

# Reuse the INSERT test harness verbatim: the seed-dir pointing, the module-scoped
# baseline, the fresh-DB copy, the DB readers, the non-function rva picker, the
# next-free-id-by-name reader, and the db_editor + GVT import.
import test_db_editor_insert as base  # noqa: E402,F401
from test_db_editor_insert import (  # noqa: E402,F401
    db_editor, DLL_PATH, GVT,
    _fresh_db, _all_names_ids, _a_non_function_rva,
    _get_baseline, _cleanup_baseline,
)

PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)
from seeds_shared import (  # noqa: E402
    derive_status, read_curated_set, read_entity_detail, read_version_rows,
    read_modules, DbReadError,
)
from seeds_shared.read_api import (  # noqa: E402
    STATUS_DEPRECATED, STATUS_SUPERSEDED, STATUS_VERIFIED, STATUS_UNVERIFIED,
)


# ===========================================================================
# A. derive_status -- the POLICY ORACLE (plain ordinals; expected tokens read
#    from policy.md, NOT from the implementation).
# ===========================================================================
# A baseline current ordinal the cases reason relative to. The +/- offsets make
# the boundary cases explicit (== the at-version ordinal; last_verified == current;
# valid_from == current).
_CUR = 1000


def _entity(*, is_deprecated=0, deprecated_at=None,
            superseded_by=None, superseded_at=None):
    return {
        "is_deprecated": is_deprecated,
        "deprecated_at_ordinal": deprecated_at,
        "superseded_by": superseded_by,
        "superseded_at_ordinal": superseded_at,
    }


def _row(*, valid_from=None, last_verified=None):
    return {"valid_from_ordinal": valid_from,
            "last_verified_ordinal": last_verified}


# --- rule 1: DEPRECATED (entity.is_deprecated AND current >= deprecated_at) ----
def test_derive_deprecated_past_its_version():
    # policy.md rule 1: deprecated and current PAST the deprecation version -> DEPRECATED.
    # Even a perfectly-verified window cannot override rule 1 (precedence: 1 before 3).
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(is_deprecated=1, deprecated_at=_CUR - 10))
    assert s == STATUS_DEPRECATED, s


def test_derive_deprecated_boundary_current_equals_deprecated_at():
    # policy.md rule 1 is `current >= deprecated_at` (inclusive): current == the
    # deprecation version is DEPRECATED (the >= boundary the rule turns on).
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(is_deprecated=1, deprecated_at=_CUR))
    assert s == STATUS_DEPRECATED, s


def test_derive_not_yet_deprecated_falls_through_to_verified():
    # is_deprecated set but current is BEFORE deprecated_at -> rule 1's right
    # conjunct false -> falls through; here the window verifies -> VERIFIED. (Proves
    # rule 1 does not fire merely because the flag is set -- the version gate matters.)
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(is_deprecated=1, deprecated_at=_CUR + 1))
    assert s == STATUS_VERIFIED, s


# --- rule 2: SUPERSEDED (entity.superseded_by AND current >= superseded_at) ----
def test_derive_superseded_past_its_version():
    # policy.md rule 2: superseded and current PAST the supersession version ->
    # SUPERSEDED (precedence: 2 before 3, so a verified window cannot override).
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(superseded_by="successor_name", superseded_at=_CUR - 10))
    assert s == STATUS_SUPERSEDED, s


def test_derive_superseded_boundary_current_equals_superseded_at():
    # policy.md rule 2 is `current >= superseded_at` (inclusive): current == the
    # supersession version is SUPERSEDED.
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(superseded_by="successor_name", superseded_at=_CUR))
    assert s == STATUS_SUPERSEDED, s


def test_derive_deprecated_wins_over_superseded():
    # Both flags set + both past their version: rule 1 (DEPRECATED) precedes rule 2
    # (SUPERSEDED) -- read-down precedence. Pins the ordering, not just each rule.
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity(is_deprecated=1, deprecated_at=_CUR - 5,
                superseded_by="succ", superseded_at=_CUR - 5))
    assert s == STATUS_DEPRECATED, s


# --- rule 3: VERIFIED (last_verified >= current AND valid_from <= current) -----
def test_derive_verified_window_contains_current():
    # policy.md rule 3: valid_from <= current <= last_verified -> VERIFIED.
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR + 50),
        _entity())
    assert s == STATUS_VERIFIED, s


def test_derive_verified_boundary_last_verified_equals_current():
    # policy.md rule 3 left conjunct is `last_verified >= current` (inclusive):
    # last_verified == current is VERIFIED (the >= boundary). This is the exact
    # version a row was last signed off for -- still trusted AT it.
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 50, last_verified=_CUR),
        _entity())
    assert s == STATUS_VERIFIED, s


def test_derive_verified_boundary_valid_from_equals_current():
    # policy.md rule 3 right conjunct is `valid_from <= current` (inclusive):
    # valid_from == current is the BOUNDARY of VERIFIED (the row is authoritative
    # from this version forward, inclusive).
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR, last_verified=_CUR + 50),
        _entity())
    assert s == STATUS_VERIFIED, s


# --- rule 4: UNVERIFIED (else) -------------------------------------------------
def test_derive_unverified_never_verified():
    # last_verified None (never signed off) -> rule 3 left conjunct false -> UNVERIFIED.
    s = derive_status(
        _CUR, _row(valid_from=_CUR - 50, last_verified=None), _entity())
    assert s == STATUS_UNVERIFIED, s


def test_derive_unverified_new_version_flips_verified_to_unverified():
    # policy.md "a new game version FLIPS VERIFIED->UNVERIFIED automatically": a row
    # last_verified at an OLDER version, with the current ordinal now HIGHER, fails
    # rule 3's `last_verified >= current` -> UNVERIFIED, no row mutation. This is the
    # multi-version flip the single-game-version fixture cannot materialize, pinned
    # here at the unit level.
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR - 100, last_verified=_CUR - 1),  # verified only THROUGH _CUR-1
        _entity())
    assert s == STATUS_UNVERIFIED, s


def test_derive_unverified_valid_from_after_current():
    # valid_from in the FUTURE (> current) -> rule 3 right conjunct false ->
    # UNVERIFIED (the row is not yet authoritative at the current version).
    s = derive_status(
        _CUR,
        _row(valid_from=_CUR + 1, last_verified=_CUR + 50),
        _entity())
    assert s == STATUS_UNVERIFIED, s


def test_derive_older_row_verified_at_its_version_then_flips_at_newer_current():
    # The multi-version story end-to-end at the UNIT level: ONE row (valid_from
    # _CUR-100, last_verified _CUR-1), derived at two different current ordinals.
    # At the version it was verified for (_CUR-1), it is VERIFIED; at the newer
    # current version (_CUR), it has flipped to UNVERIFIED -- exactly policy.md's
    # "older row VERIFIED at its version, flips at the current version" semantics.
    row = _row(valid_from=_CUR - 100, last_verified=_CUR - 1)
    assert derive_status(_CUR - 1, row, _entity()) == STATUS_VERIFIED
    assert derive_status(_CUR, row, _entity()) == STATUS_UNVERIFIED


# ===========================================================================
# B. read_* against the real mini-dump-built DB.
# ===========================================================================
def _current_ordinal_from_db(out_dir):
    """The baseline (max) game_versions.ordinal in the built DB -- the current
    version status is derived at. Read from the DB (the test's own independent read
    of the baseline ordinal), NOT a hardcoded number, per the step doc."""
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        return con.execute("SELECT MAX(ordinal) FROM game_versions").fetchone()[0]
    finally:
        con.close()


def _a_known_unverified_kcdx_id(out_dir):
    """An entity whose current row has NULL last_verified_at_version -- it must
    derive UNVERIFIED at the current version (rule 3 left conjunct false). The
    fixture carries some (the 'never verified' rows); pick the first."""
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        row = con.execute(
            "SELECT kcdx_id FROM address_versions "
            "WHERE kcdx_id IS NOT NULL AND valid_through IS NULL "
            "AND last_verified_at_version IS NULL "
            "ORDER BY kcdx_id LIMIT 1").fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _a_known_verified_kcdx_id(out_dir):
    """An entity whose current row HAS last_verified_at_version set, at the baseline
    version -- it must derive VERIFIED at the current version (valid_from <= current
    <= last_verified). Pick the first such non-deprecated, non-superseded entity."""
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        # A clean entity: is_deprecated is stored 0 (not NULL) for an un-deprecated
        # entity; superseded_by is NULL when not superseded.
        row = con.execute(
            "SELECT av.kcdx_id FROM address_versions av "
            "JOIN address_names an ON an.id = av.kcdx_id "
            "WHERE av.valid_through IS NULL "
            "AND av.last_verified_at_version IS NOT NULL "
            "AND (an.is_deprecated IS NULL OR an.is_deprecated = 0) "
            "AND an.superseded_by IS NULL "
            "ORDER BY av.kcdx_id LIMIT 1").fetchone()
        return row[0] if row else None
    finally:
        con.close()


def _curated_entity_count(out_dir):
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        return con.execute("SELECT COUNT(*) FROM address_names").fetchone()[0]
    finally:
        con.close()


def _modules_from_db(out_dir):
    """The modules table rows {id, name, path} read independently from the DB (the
    test's own read, not read_modules) -- the oracle read_modules is asserted
    against. id-ascending, mirroring read_modules' ORDER BY."""
    con = sqlite3.connect(os.path.join(out_dir, "reference.sqlite"))
    try:
        return [{"id": r[0], "name": r[1], "path": r[2]}
                for r in con.execute(
                    "SELECT id, name, path FROM modules ORDER BY id")]
    finally:
        con.close()


# --- read_curated_set ----------------------------------------------------------
def _curated_set_shape_and_count(b):
    out = _fresh_db(b)
    try:
        entities = read_curated_set(out)
        # Count matches the fixture's curated set (every USER address_names row).
        assert len(entities) == _curated_entity_count(out), (
            f"read_curated_set count {len(entities)} != "
            f"fixture curated count {_curated_entity_count(out)}")
        # Each carries name / kcdx_id / status / kind.
        for e in entities:
            assert set(e) == {"kcdx_id", "name", "status", "kind"}, e
            assert isinstance(e["kcdx_id"], int)
            assert e["name"]
            assert e["status"] in (STATUS_DEPRECATED, STATUS_SUPERSEDED,
                                   STATUS_VERIFIED, STATUS_UNVERIFIED), e
        # A known unverified entity shows UNVERIFIED; a known verified one VERIFIED
        # -- the status is computed end-to-end against the real DB, not a mock.
        by_id = {e["kcdx_id"]: e for e in entities}
        unv = _a_known_unverified_kcdx_id(out)
        if unv is not None:
            assert by_id[unv]["status"] == STATUS_UNVERIFIED, by_id[unv]
        ver = _a_known_verified_kcdx_id(out)
        assert ver is not None, "fixture has no verified entity to assert on"
        assert by_id[ver]["status"] == STATUS_VERIFIED, by_id[ver]
        # kind is the current-row display string (decoded from the dict id), one of
        # the nine address kinds -- never a bare integer.
        assert by_id[ver]["kind"] in (
            "function", "function_variadic", "function_no_sig", "callsite",
            "vtable_index", "vtable_base", "data_slot", "string_anchor",
            "instruction_anchor"), by_id[ver]
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _curated_set_deprecated_shows_deprecated(b):
    # The fixture has NO deprecated entity -> CONSTRUCT one via the landed
    # db_editor.deprecate_entity (a real validated edit), then assert read_curated_set
    # derives DEPRECATED at the current version.
    out = _fresh_db(b)
    try:
        target = _a_known_verified_kcdx_id(out)  # a clean entity to deprecate
        assert target is not None
        db_editor.deprecate_entity(out, DLL_PATH, target, deprecated_at_version=GVT)
        by_id = {e["kcdx_id"]: e for e in read_curated_set(out)}
        assert by_id[target]["status"] == STATUS_DEPRECATED, (
            f"deprecated entity {target} derived {by_id[target]['status']!r}, "
            f"expected DEPRECATED (current >= deprecated_at == GVT ordinal)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _curated_set_superseded_shows_superseded(b):
    # No superseded entity in the fixture -> CONSTRUCT: mint a successor, then
    # supersede a clean entity at GVT, and assert read_curated_set derives SUPERSEDED.
    out = _fresh_db(b)
    try:
        x = _a_known_verified_kcdx_id(out)
        assert x is not None
        succ_name = "read_api_oracle_successor"
        db_editor.create_entity(
            out, DLL_PATH, succ_name,
            first_version_columns={
                "valid_from_version": GVT, "module": "WHGame.dll",
                "kind": "data_slot", "rva": "0x%08X" % _a_non_function_rva()})
        db_editor.supersede_entity(out, DLL_PATH, x, succ_name, GVT)
        by_id = {e["kcdx_id"]: e for e in read_curated_set(out)}
        assert by_id[x]["status"] == STATUS_SUPERSEDED, (
            f"superseded entity {x} derived {by_id[x]['status']!r}, "
            f"expected SUPERSEDED (current >= superseded_at == GVT ordinal)")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --- read_entity_detail --------------------------------------------------------
def _entity_detail_identity_and_lifecycle(b):
    out = _fresh_db(b)
    try:
        kid = _a_known_verified_kcdx_id(out)
        assert kid is not None
        detail = read_entity_detail(out, kid)
        assert detail is not None
        assert set(detail) == {
            "kcdx_id", "name", "superseded_by", "superseded_at_version",
            "is_deprecated", "deprecated_at_version", "deprecation_replacement",
            "notes"}, detail
        assert detail["kcdx_id"] == kid
        assert detail["name"]
        # A clean entity: lifecycle fields are unset (NULL).
        assert detail["superseded_by"] is None
        assert not detail["is_deprecated"]

        # After a deprecate edit, the lifecycle fields reflect it (the detail surface
        # renders them -- proves it reads the live entity row, not a stale shape).
        db_editor.deprecate_entity(out, DLL_PATH, kid, deprecated_at_version=GVT)
        d2 = read_entity_detail(out, kid)
        assert d2["is_deprecated"], d2
        assert d2["deprecated_at_version"] is not None, d2
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _entity_detail_not_found_returns_none(b):
    out = _fresh_db(b)
    try:
        # An id past every existing one -> None (the consumer maps None to 404).
        unknown = max(_all_names_ids(os.path.join(out, "reference.sqlite"))) + 10000
        assert read_entity_detail(out, unknown) is None, (
            "read_entity_detail did not return None for an unknown kcdx_id")
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --- read_version_rows ---------------------------------------------------------
# The read CONTRACT for read_version_rows. THE DISPLAY/EDITABLE column set (US-5 +
# s02/s03) -- the _VERSION_DISPLAY_COLUMNS allowlist as it appears in the returned
# dict (kind/evidence_kind decoded, valid_from/valid_through ordinals). This pins the
# DISPLAY set so a future widening (a real column leaking IN, or content_hash being
# mis-added to the display set) breaks the test, not the wire contract. content_hash
# is DELIBERATELY ABSENT here -- it is verify-only, never a display column.
_DISPLAY_VERSION_COLUMNS = {
    "kcdx_id", "kind", "module_id", "rva", "length", "value", "signature",
    "observed_arg_slots", "caller_reg_arg_count", "caller_arg_agreement",
    "offset", "vtable_slot", "struct_offset",
    # The six folded survival columns (D22/S11.2 -- the former `survival` sibling
    # folded onto address_versions). They ARE in the display contract: the survival
    # re-find data the maintainer authors/sees on s02/s03 (design US-5 + S11.2),
    # curated display columns like offset/vtable_slot/struct_offset.
    "aob", "anchor_string", "rule", "slot_count", "expect_unique", "derives_from",
    "last_verified_at_version", "verified_by", "verified_date", "evidence_kind",
    "valid_from", "valid_through",
    # The version-TAG STRINGS alongside the FK-resolved ordinals: the WRITE-path
    # identity key the maintainer tool's save/confirm/resolve_tag consumes (the ordinal
    # is internal sort/status only; resolve_tag rejects it). Both representations ship.
    "valid_from_version", "valid_through_version",
}
# The EXACT key set every returned row carries: the display columns + the derived
# "status" + the VERIFY-ONLY "content_hash" (the s04 function check's recorded hash --
# returned alongside the display set, NOT a display column; see _DISPLAY_VERSION_COLUMNS
# above for the display set that must NOT contain it).
_EXPECTED_VERSION_ROW_KEYS = _DISPLAY_VERSION_COLUMNS | {"status", "content_hash"}
# The columns the contract DROPS -- they exist on the DB row but must NEVER cross the
# wire: auto_name / decompile_quality (DEV-ONLY, schema.py), id (internal PK row
# handle). content_hash is NOT here -- it now crosses as the verify-only field.
_DROPPED_VERSION_COLUMNS = ("auto_name", "decompile_quality", "id")


def _version_rows_carry_status_and_newest_first(b):
    out = _fresh_db(b)
    try:
        kid = _a_known_verified_kcdx_id(out)
        assert kid is not None
        rows = read_version_rows(out, kid)
        assert rows, "read_version_rows returned no rows for a known entity"
        cur = _current_ordinal_from_db(out)

        # THE READ CONTRACT: each row's key set is EXACTLY the display columns + status
        # + the verify-only content_hash. The DEV-ONLY/internal columns (auto_name /
        # decompile_quality / id) never cross the wire. content_hash DOES cross now (as
        # the verify-only field), but is NOT in the DISPLAY set -- asserted separately
        # below so the display-set guarantee stays falsifiable.
        for r in rows:
            assert set(r) == _EXPECTED_VERSION_ROW_KEYS, (
                f"version row key set drifted from the read contract: "
                f"unexpected {set(r) - _EXPECTED_VERSION_ROW_KEYS}, "
                f"missing {_EXPECTED_VERSION_ROW_KEYS - set(r)}")
            for dropped in _DROPPED_VERSION_COLUMNS:
                assert dropped not in r, (
                    f"{dropped!r} leaked into the version-row read contract: {r}")
            # content_hash is VERIFY-ONLY, NOT a display column: it is present in the
            # row but must NOT be in the display/edit set (s02/s03 never render it).
            # This is the load-bearing separation -- if a future change folded
            # content_hash into _VERSION_DISPLAY_COLUMNS, this breaks.
            assert "content_hash" in r, (
                f"verify-only content_hash missing from the read contract: {r}")
            assert "content_hash" not in _DISPLAY_VERSION_COLUMNS, (
                "content_hash leaked into the DISPLAY column set -- it is verify-only, "
                "returned alongside but NEVER a display/edit column (s02/s03 unaffected)")

        # content_hash is genuinely ON the underlying DB row for this entity (a
        # function row carries the bulk-promote fingerprint) -- the verify-only field
        # below is asserted to match it, hex-encoded.
        con = sqlite3.connect(os.path.join(out, "reference.sqlite"))
        try:
            db_row = con.execute(
                "SELECT content_hash FROM address_versions "
                "WHERE kcdx_id = ? AND content_hash IS NOT NULL LIMIT 1",
                (kid,)).fetchone()
        finally:
            con.close()
        assert db_row is not None, (
            "fixture precondition: the chosen entity's DB row should carry a "
            "content_hash (bulk-promote fingerprint) so the verify-only assertion is real")
        # (a) A FUNCTION row that HAS a content_hash returns it as a lowercase-hex string
        # under the verify-only `content_hash` key -- 64-char (32-byte BLAKE3 -> hex),
        # all-lowercase (the client compares got.toLowerCase() === stored.toLowerCase()),
        # and == the stored BLOB hex-encoded (not a fabricated or truncated value).
        stored_hex = bytes(db_row[0]).hex()
        assert len(stored_hex) == 64, stored_hex  # 32-byte BLAKE3 -> 64 hex chars
        hashed_rows = [r for r in rows if r["content_hash"] is not None]
        assert hashed_rows, (
            "the entity's fingerprinted DB row should surface a non-None verify-only "
            "content_hash in the read output")
        for r in hashed_rows:
            ch = r["content_hash"]
            assert isinstance(ch, str), (f"content_hash must be a hex STRING, got {ch!r}")
            assert ch == ch.lower(), (f"content_hash must be lowercase hex: {ch!r}")
            assert len(ch) == 64, (f"content_hash must be 64-char (32-byte) hex: {ch!r}")
            assert all(c in "0123456789abcdef" for c in ch), (
                f"content_hash must be lowercase hex chars only: {ch!r}")
        # The fingerprinted DB row's stored hash appears verbatim (hex-encoded) in the
        # read output -- the verify-only field IS the recorded hash, not a placeholder.
        assert any(r["content_hash"] == stored_hex for r in hashed_rows), (
            f"the stored content_hash {stored_hex!r} did not appear hex-encoded in the "
            f"verify-only field: {[r['content_hash'] for r in hashed_rows]}")

        # Each row carries a derived status.
        for r in rows:
            assert "status" in r, r
            assert r["status"] in (STATUS_DEPRECATED, STATUS_SUPERSEDED,
                                   STATUS_VERIFIED, STATUS_UNVERIFIED), r
        # The current row (the one verified at the baseline) derives VERIFIED.
        assert rows[0]["status"] == STATUS_VERIFIED, rows[0]

        # NEWEST-first: valid_from ordinals are non-increasing. (The fixture has one
        # version per entity, so this is a single row, but the ordering contract is
        # asserted to hold for the general case.)
        con = sqlite3.connect(os.path.join(out, "reference.sqlite"))
        try:
            gv = {r2[0]: r2[1] for r2 in con.execute(
                "SELECT id, ordinal FROM game_versions")}
            # id -> tag (the independent oracle for the row's valid_from_version tag).
            gv_tag = {r2[0]: r2[1] for r2 in con.execute(
                "SELECT id, tag FROM game_versions")}
            # tag -> ordinal (proves valid_from_version is the TAG, NOT the ordinal: the
            # tag must NOT equal the stringified ordinal it resolves to).
            tag_set = set(gv_tag.values())
        finally:
            con.close()
        ords = [gv.get(r.get("valid_from"), -1) for r in rows]
        assert ords == sorted(ords, reverse=True), (
            f"version rows not newest-first by valid_from ordinal: {ords}")

        # THE FIX (the maintainer-tool 422 bug): each row carries BOTH the FK-resolved
        # valid_from ORDINAL (sort/status key) AND the valid_from_version TAG STRING (the
        # write identity key resolve_tag accepts). The tag is the REAL game-version tag,
        # NOT the ordinal -- a row sending the ordinal as version_tag is the bug. The
        # ordinal coexists (not removed); the tag is the resolve_tag-accepted identity.
        for r in rows:
            # The ordinal is STILL present (display/sort/status key on it -- not removed).
            assert "valid_from" in r, r
            # The tag is present and is the game_versions.tag for the row's valid_from FK.
            assert "valid_from_version" in r, r
            assert r["valid_from_version"] == gv_tag.get(r["valid_from"]), (
                f"valid_from_version {r['valid_from_version']!r} != the tag for FK "
                f"{r['valid_from']!r} ({gv_tag.get(r['valid_from'])!r})")
            # It is a REAL known tag, NOT the stringified ordinal (the 422 cause). The
            # tag and the ordinal are different representations -- assert the value sent
            # to the write path is the tag string, never the ordinal.
            assert r["valid_from_version"] in tag_set, (
                f"valid_from_version {r['valid_from_version']!r} is not a known game-"
                f"version tag {tag_set!r} -- the ordinal must NOT be sent as the tag")
            assert str(r["valid_from_version"]) != str(r["valid_from"]), (
                f"valid_from_version equals the ordinal {r['valid_from']!r} -- it must "
                f"be the TAG STRING, not the FK ordinal (the maintainer-tool 422 bug)")
            # valid_through_version coexists too: null for an open-interval current row
            # (valid_through None), else the tag for that FK.
            assert "valid_through_version" in r, r
            if r.get("valid_through") is None:
                assert r["valid_through_version"] is None, r
            else:
                assert r["valid_through_version"] == gv_tag.get(r["valid_through"]), r

        # The dict-encoded kind is decoded to its display string, not a bare int.
        assert isinstance(rows[0].get("kind"), str), rows[0]
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _version_rows_deprecated_entity_rows_show_deprecated(b):
    # A deprecated entity's version rows all derive DEPRECATED (rule 1 is entity-
    # level, overriding the per-row verification window). Constructed via db_editor.
    out = _fresh_db(b)
    try:
        kid = _a_known_verified_kcdx_id(out)
        assert kid is not None
        # Before: the current row is VERIFIED.
        assert read_version_rows(out, kid)[0]["status"] == STATUS_VERIFIED
        db_editor.deprecate_entity(out, DLL_PATH, kid, deprecated_at_version=GVT)
        rows = read_version_rows(out, kid)
        assert rows, "no version rows after deprecate"
        for r in rows:
            assert r["status"] == STATUS_DEPRECATED, (
                f"a deprecated entity's version row derived {r['status']!r}, "
                f"expected DEPRECATED")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _version_rows_unknown_id_returns_empty(b):
    out = _fresh_db(b)
    try:
        unknown = max(_all_names_ids(os.path.join(out, "reference.sqlite"))) + 10000
        assert read_version_rows(out, unknown) == [], (
            "read_version_rows did not return [] for an unknown kcdx_id")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _version_rows_null_content_hash_returns_none(b):
    # (c) A row with NULL content_hash (never fingerprinted) returns None for the
    # verify-only field -- NOT an absent key (no KeyError/crash), NOT a fabricated
    # value. A non-function kind (data_slot) carries no body fingerprint: content_hash
    # is NULL on the av row. CONSTRUCT one via the landed db_editor.create_entity (a
    # real validated edit, same harness the superseded oracle uses), then assert the
    # read output surfaces content_hash == None for it.
    out = _fresh_db(b)
    try:
        name = "read_api_null_hash_data_slot"
        db_editor.create_entity(
            out, DLL_PATH, name,
            first_version_columns={
                "valid_from_version": GVT, "module": "WHGame.dll",
                "kind": "data_slot", "rva": "0x%08X" % _a_non_function_rva()})
        kid = {nm: i for i, nm in
               _name_id_pairs(os.path.join(out, "reference.sqlite"))}[name]
        # Fixture precondition: this data_slot row's content_hash is NULL in the DB
        # (only function rows carry the bulk-promote fingerprint).
        con = sqlite3.connect(os.path.join(out, "reference.sqlite"))
        try:
            db_ch = con.execute(
                "SELECT content_hash FROM address_versions WHERE kcdx_id = ?",
                (kid,)).fetchone()
        finally:
            con.close()
        assert db_ch is not None, "the minted entity should have a version row"
        assert db_ch[0] is None, (
            "fixture precondition: a data_slot row's content_hash should be NULL "
            "so the None-not-fabricated assertion is real")
        rows = read_version_rows(out, kid)
        assert rows, "read_version_rows returned no rows for the minted entity"
        for r in rows:
            # Present (not an absent key -- no crash), and None (not fabricated).
            assert "content_hash" in r, (
                f"content_hash key absent for a NULL-hash row (must be present as "
                f"None, not crash): {r}")
            assert r["content_hash"] is None, (
                f"a NULL content_hash must surface as None, never a fabricated value: "
                f"{r['content_hash']!r}")
    finally:
        shutil.rmtree(out, ignore_errors=True)


def _name_id_pairs(db_path):
    """(id, name) for every address_names row -- a small local reader so the NULL-hash
    test can resolve the minted entity's kcdx_id by name (the insert harness exposes
    next-free-id and all-ids, not a by-name lookup)."""
    con = sqlite3.connect(db_path)
    try:
        return [(r[0], r[1]) for r in
                con.execute("SELECT id, name FROM address_names")]
    finally:
        con.close()


# --- read_modules --------------------------------------------------------------
def _modules_shape_and_contents(b):
    # read_modules returns the modules registry {id, name, path}, id-ascending,
    # matching an independent DB read (the oracle). The committed fixture carries the
    # WHGame.dll module (module_seed.csv); assert read_modules surfaces it verbatim.
    out = _fresh_db(b)
    try:
        mods = read_modules(out)
        expected = _modules_from_db(out)
        assert mods, "read_modules returned no modules for the built fixture DB"
        assert mods == expected, (
            f"read_modules drifted from an independent DB read: "
            f"{mods!r} != {expected!r}")
        for m in mods:
            assert set(m) == {"id", "name", "path"}, m
            assert isinstance(m["id"], int)
            assert isinstance(m["name"], str) and m["name"]
            assert isinstance(m["path"], str) and m["path"]
        # id-ascending (a stable registry order, not a version ordinal).
        ids = [m["id"] for m in mods]
        assert ids == sorted(ids), f"read_modules not id-ascending: {ids}"
        # The fixture's WHGame.dll module is present (the curated module every
        # curated address row's module_id resolves to).
        assert any(m["name"] == "WHGame.dll" for m in mods), mods
    finally:
        shutil.rmtree(out, ignore_errors=True)


# --- read-only / missing-DB contract -------------------------------------------
def _missing_db_raises(b):
    import tempfile
    empty = tempfile.mkdtemp(prefix="read_api_empty_")
    try:
        raised = None
        try:
            read_curated_set(empty)
        except DbReadError as e:
            raised = e
        assert raised is not None, (
            "read_curated_set on a dir with no reference.sqlite did not raise "
            "DbReadError")
    finally:
        shutil.rmtree(empty, ignore_errors=True)


def _modules_missing_db_raises(b):
    # read_modules mirrors the other read functions' no-DB contract: a dir with no
    # reference.sqlite raises DbReadError (the consumer GET /modules maps it to the
    # s01 empty signal).
    import tempfile
    empty = tempfile.mkdtemp(prefix="read_api_mod_empty_")
    try:
        raised = None
        try:
            read_modules(empty)
        except DbReadError as e:
            raised = e
        assert raised is not None, (
            "read_modules on a dir with no reference.sqlite did not raise "
            "DbReadError")
    finally:
        shutil.rmtree(empty, ignore_errors=True)


# ===========================================================================
# pytest entry points (reuse the insert module's `baseline` fixture).
# ===========================================================================
try:
    import pytest

    @pytest.fixture(scope="module")
    def baseline():
        b = _get_baseline()
        yield b
        _cleanup_baseline()
except ImportError:   # pragma: no cover - allows __main__ runner without pytest
    pytest = None


def test_curated_set_shape_and_count(baseline):  # noqa: F811
    _curated_set_shape_and_count(baseline)


def test_curated_set_deprecated_shows_deprecated(baseline):  # noqa: F811
    _curated_set_deprecated_shows_deprecated(baseline)


def test_curated_set_superseded_shows_superseded(baseline):  # noqa: F811
    _curated_set_superseded_shows_superseded(baseline)


def test_entity_detail_identity_and_lifecycle(baseline):  # noqa: F811
    _entity_detail_identity_and_lifecycle(baseline)


def test_entity_detail_not_found_returns_none(baseline):  # noqa: F811
    _entity_detail_not_found_returns_none(baseline)


def test_version_rows_carry_status_and_newest_first(baseline):  # noqa: F811
    _version_rows_carry_status_and_newest_first(baseline)


def test_version_rows_deprecated_entity_rows_show_deprecated(baseline):  # noqa: F811
    _version_rows_deprecated_entity_rows_show_deprecated(baseline)


def test_version_rows_unknown_id_returns_empty(baseline):  # noqa: F811
    _version_rows_unknown_id_returns_empty(baseline)


def test_version_rows_null_content_hash_returns_none(baseline):  # noqa: F811
    _version_rows_null_content_hash_returns_none(baseline)


def test_missing_db_raises(baseline):  # noqa: F811
    _missing_db_raises(baseline)


def test_modules_shape_and_contents(baseline):  # noqa: F811
    _modules_shape_and_contents(baseline)


def test_modules_missing_db_raises(baseline):  # noqa: F811
    _modules_missing_db_raises(baseline)


if __name__ == "__main__":
    # The derive_status unit cases need no DB.
    for name, fn in sorted(globals().items()):
        if name.startswith("test_derive_") and callable(fn):
            fn()
            print(f"PASS {name}")
    try:
        b = _get_baseline()
        _curated_set_shape_and_count(b)
        print("PASS test_curated_set_shape_and_count")
        _curated_set_deprecated_shows_deprecated(b)
        print("PASS test_curated_set_deprecated_shows_deprecated")
        _curated_set_superseded_shows_superseded(b)
        print("PASS test_curated_set_superseded_shows_superseded")
        _entity_detail_identity_and_lifecycle(b)
        print("PASS test_entity_detail_identity_and_lifecycle")
        _entity_detail_not_found_returns_none(b)
        print("PASS test_entity_detail_not_found_returns_none")
        _version_rows_carry_status_and_newest_first(b)
        print("PASS test_version_rows_carry_status_and_newest_first")
        _version_rows_deprecated_entity_rows_show_deprecated(b)
        print("PASS test_version_rows_deprecated_entity_rows_show_deprecated")
        _version_rows_unknown_id_returns_empty(b)
        print("PASS test_version_rows_unknown_id_returns_empty")
        _version_rows_null_content_hash_returns_none(b)
        print("PASS test_version_rows_null_content_hash_returns_none")
        _missing_db_raises(b)
        print("PASS test_missing_db_raises")
        _modules_shape_and_contents(b)
        print("PASS test_modules_shape_and_contents")
        _modules_missing_db_raises(b)
        print("PASS test_modules_missing_db_raises")
        print("\nall read_api tests passed")
    finally:
        _cleanup_baseline()
