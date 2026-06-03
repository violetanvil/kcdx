"""test_field_delta.py -- the saved-vs-prospective field delta (maintainer-tool
Phase 1, step 6): field_delta + is_new_version_nothing_changed (design D8 / D12).

WHAT THIS PROVES
----------------
seeds_shared.field_delta is the PURE computation behind the GUI save-confirm surface
(design data/maintainer-tool/design.md S10 D8 + S7 + S6 US-4; ui/screens/
s06-save-confirm.md). Given a SAVED record dict and the prospective EDITED record
dict (the data-core's seed-row shape -- {column: cell_string}, '' for a NULL cell),
it returns the changed fields as `field -> (old, new)`, deterministically ordered,
with unchanged fields absent; and it computes the D12 "nothing changed" verdict for
a new version. The test exercises the REAL functions on SYNTHETIC record-dict pairs
(the cheapest oracle in the phase -- no DB, no fixture). Cases:

  1. AUDIT-TRIO edit -> exactly the trio's changed fields as old -> new; an UNCHANGED
     field is ABSENT from the delta.
  2. NEW-ENTITY INSERT (all-from-empty) -> an all-fields delta from empty.
  3. D12 nothing-changed -> a new version identical to its source except
     valid_from_version yields the verdict; a CHANGED field DEFEATS it.
  4. DETERMINISTIC order + formatting -> the exact ordered delta for a fixed input
     (authored column order; None and '' are the same empty cell).

RUN
---
    python tests/test_field_delta.py
    pytest tests/test_field_delta.py
"""
import os
import sys
from collections import OrderedDict

HERE = os.path.dirname(os.path.abspath(__file__))
PYDIR = os.path.normpath(os.path.join(HERE, "..", "python"))
sys.path.insert(0, PYDIR)

from seeds_shared.field_delta import (  # noqa: E402
    field_delta,
    is_new_version_nothing_changed,
)
from seeds_shared.csv_exporter import (  # noqa: E402
    ADDRESS_VERSIONS_CSV_HEADER,
    ADDRESS_NAMES_CSV_HEADER,
)


# A realistic SAVED address_versions record (the seed-row dict shape: cell strings,
# '' for a NULL cell -- exactly what csv_exporter emits and the GUI passes). Reused
# across cases as the unedited baseline.
def _saved_version_record():
    return {
        "kcdx_id": "42",
        "valid_from_version": "1.4",
        "module": "WHGame.dll",
        "rva": "0x00ABCDEF",
        "kind": "function",
        "signature": "void (int)",
        "last_verified_at_version": "1.4",
        "verified_by": "alice",
        "verified_date": "2026-01-01",
        "evidence_kind": "maintainer_ghidra",
        "survival_aob": "",
        "survival_anchor_string": "",
        "survival_derives_from": "",
        "survival_rule": "",
        "survival_slot_count": "",
        "survival_expect_unique": "",
        "value": "",
        "offset": "",
        "vtable_slot": "",
        "struct_offset": "",
    }


# --------------------------------------------------------------------------
# Case 1: an audit-trio re-verify edit -> exactly the trio's changed fields;
# an unchanged field is absent.
# --------------------------------------------------------------------------
def _audit_trio_edit_yields_only_changed():
    saved = _saved_version_record()
    prospective = dict(saved)
    # Re-verify at 1.5: the audit trio + last_verified bump (US-3). Nothing else.
    prospective["last_verified_at_version"] = "1.5"
    prospective["verified_by"] = "bob"
    prospective["verified_date"] = "2026-06-01"
    prospective["evidence_kind"] = "live_test_plugin"

    delta = field_delta(saved, prospective)

    expected = OrderedDict([
        ("last_verified_at_version", ("1.4", "1.5")),
        ("verified_by", ("alice", "bob")),
        ("verified_date", ("2026-01-01", "2026-06-01")),
        ("evidence_kind", ("maintainer_ghidra", "live_test_plugin")),
    ])
    assert delta == expected, (
        f"audit-trio delta wrong:\n  got      {dict(delta)}\n  expected {dict(expected)}")
    # An unchanged field (module, rva, signature, kind, ...) is ABSENT.
    for unchanged in ("module", "rva", "kind", "signature", "kcdx_id",
                      "valid_from_version"):
        assert unchanged not in delta, \
            f"unchanged field {unchanged!r} leaked into the delta"


# --------------------------------------------------------------------------
# Case 2: a new-entity INSERT (all-from-empty) -> an all-fields delta from empty.
# --------------------------------------------------------------------------
def _insert_all_from_empty():
    # The INSERT's "saved" side is the empty record (nothing existed before); the
    # prospective side is the new row's authored cells (the non-empty ones).
    saved = {}
    prospective = {
        "kcdx_id": "99",
        "valid_from_version": "1.5",
        "module": "WHGame.dll",
        "rva": "0x00112233",
        "kind": "data_slot",
    }
    delta = field_delta(saved, prospective)

    # Every non-empty prospective field appears, old == '' (empty), in authored order.
    expected = OrderedDict([
        ("kcdx_id", ("", "99")),
        ("valid_from_version", ("", "1.5")),
        ("module", ("", "WHGame.dll")),
        ("rva", ("", "0x00112233")),
        ("kind", ("", "data_slot")),
    ])
    assert delta == expected, (
        f"INSERT all-from-empty delta wrong:\n  got      {dict(delta)}\n"
        f"  expected {dict(expected)}")
    # A field empty on BOTH sides (not in prospective at all) is absent.
    assert "signature" not in delta and "offset" not in delta, \
        "an all-empty field leaked into the INSERT delta"


# --------------------------------------------------------------------------
# Case 3: D12 nothing-changed -- identical-except-valid_from fires; a change defeats.
# --------------------------------------------------------------------------
def _nothing_changed_fires_and_a_change_defeats():
    source = _saved_version_record()

    # A new version identical to its source on every authored column except
    # valid_from_version -> the verdict FIRES (steer to re-verify, not duplicate).
    identical_at_new_tag = dict(source)
    identical_at_new_tag["valid_from_version"] = "1.5"
    assert is_new_version_nothing_changed(source, identical_at_new_tag) is True, (
        "nothing-changed did NOT fire on a new version identical to its source "
        "except valid_from_version (D12 steering signal missing)")

    # A new version with a CHANGED authored column (signature) -> the verdict is
    # DEFEATED (a real new version, not a duplicate-but-for-the-tag).
    changed = dict(identical_at_new_tag)
    changed["signature"] = "void (int, ptr)"
    assert is_new_version_nothing_changed(source, changed) is False, (
        "nothing-changed FIRED on a new version whose signature differs from the "
        "source (false positive -- D12 would mis-steer)")

    # The field delta of the nothing-changed case (ignoring valid_from for D12) is
    # empty -- the verdict and the delta agree (single source of "what differs").
    delta_ignoring_key = field_delta(
        source, identical_at_new_tag, ignore=("valid_from_version",))
    assert delta_ignoring_key == OrderedDict(), (
        "the nothing-changed case has a non-empty delta when ignoring "
        f"valid_from_version: {dict(delta_ignoring_key)}")

    # A None-vs-'' difference is NOT a change (both are an empty cell) -- the verdict
    # holds when the source carries None where the new row carries ''.
    source_with_none = dict(source)
    source_with_none["offset"] = None        # an empty cell as None
    new_with_blank = dict(identical_at_new_tag)
    new_with_blank["offset"] = ""            # the same empty cell as ''
    assert is_new_version_nothing_changed(source_with_none, new_with_blank) is True, \
        "None vs '' (both empty cells) was mis-read as a change"


# --------------------------------------------------------------------------
# Case 4: deterministic order + formatting -- the exact ordered output for a fixed
# input, including a names-row (lifecycle) field order + a non-header trailing key.
# --------------------------------------------------------------------------
def _deterministic_order_and_formatting():
    # Edit several fields that are NOT in authored-header adjacency, plus pass the
    # two dicts with keys in a SCRAMBLED insertion order, to prove the delta's field
    # order follows the authored column order (csv header), not dict-insertion order.
    saved = OrderedDict([
        ("struct_offset", "8"),
        ("evidence_kind", "maintainer_ghidra"),
        ("kcdx_id", "42"),
        ("valid_from_version", "1.4"),
        ("rva", "0x00ABCDEF"),
        ("last_verified_at_version", "1.4"),
    ])
    prospective = OrderedDict([
        ("rva", "0x00ABCDEF"),                    # unchanged -> absent
        ("kcdx_id", "42"),                        # unchanged -> absent
        ("valid_from_version", "1.5"),            # changed
        ("evidence_kind", "live_test_plugin"),    # changed
        ("last_verified_at_version", "1.5"),      # changed
        ("struct_offset", "16"),                  # changed
    ])
    delta = field_delta(saved, prospective)

    # Authored column order (ADDRESS_VERSIONS_CSV_HEADER):
    #   ... valid_from_version ... last_verified_at_version ... evidence_kind ...
    #   ... struct_offset (last). The delta MUST follow this, regardless of the
    #   scrambled input dict order.
    expected = OrderedDict([
        ("valid_from_version", ("1.4", "1.5")),
        ("last_verified_at_version", ("1.4", "1.5")),
        ("evidence_kind", ("maintainer_ghidra", "live_test_plugin")),
        ("struct_offset", ("8", "16")),
    ])
    assert list(delta.items()) == list(expected.items()), (
        f"deterministic order wrong:\n  got      {list(delta.items())}\n"
        f"  expected {list(expected.items())}")

    # The authored order claim is anchored to the real header (no magic literals):
    hdr = ADDRESS_VERSIONS_CSV_HEADER
    keys = list(delta.keys())
    assert keys == sorted(keys, key=hdr.index), \
        f"delta keys not in authored-header order: {keys}"

    # A lifecycle (names-row) edit uses the names header order; a key NOT in the
    # header is ordered AFTER the named columns, sorted.
    names_saved = {
        "id": "42", "name": "OldName", "is_deprecated": "",
        "zz_extra": "a",   # a non-header key -> sorted after the named columns
    }
    names_prospective = {
        "id": "42", "name": "OldName", "is_deprecated": "1",
        "deprecated_at_version": "1.5", "zz_extra": "b",
    }
    names_delta = field_delta(
        names_saved, names_prospective, field_order=ADDRESS_NAMES_CSV_HEADER)
    # is_deprecated + deprecated_at_version are header columns (in that order); name
    # unchanged -> absent; zz_extra (non-header) is last.
    assert list(names_delta.keys()) == [
        "is_deprecated", "deprecated_at_version", "zz_extra"], (
        f"names-row delta order wrong: {list(names_delta.keys())}")
    assert names_delta["is_deprecated"] == ("", "1")
    assert names_delta["deprecated_at_version"] == ("", "1.5")
    assert names_delta["zz_extra"] == ("a", "b")


# --------------------------------------------------------------------------
# pytest entry points.
# --------------------------------------------------------------------------
def test_audit_trio_edit_yields_only_changed_fields():
    _audit_trio_edit_yields_only_changed()


def test_insert_all_from_empty():
    _insert_all_from_empty()


def test_nothing_changed_fires_and_a_change_defeats():
    _nothing_changed_fires_and_a_change_defeats()


def test_deterministic_order_and_formatting():
    _deterministic_order_and_formatting()


if __name__ == "__main__":
    _audit_trio_edit_yields_only_changed()
    print("PASS test_audit_trio_edit_yields_only_changed_fields")
    _insert_all_from_empty()
    print("PASS test_insert_all_from_empty")
    _nothing_changed_fires_and_a_change_defeats()
    print("PASS test_nothing_changed_fires_and_a_change_defeats")
    _deterministic_order_and_formatting()
    print("PASS test_deterministic_order_and_formatting")
    print("\nall field_delta tests passed")
