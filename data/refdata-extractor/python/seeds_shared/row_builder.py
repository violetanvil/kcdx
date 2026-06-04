"""seeds_shared.row_builder -- THE single address_versions row constructor.

This is the highest-consequence shared piece of seeds_shared/. It maps a
validated seed row (plus the resolved ids the caller computed) to the
`address_versions` column dict that gets INSERTed. The rebuild path's step-6
(curated promote/mint) and the future incremental `apply` path BOTH call
`build_curated_row`, so "the incremental path wrote a different row shape than
rebuild would have" is structurally impossible -- the column list lives in one
place, not two hand-maintained copies.

Two builders, mirroring the two rebuild row sources:

  - build_bulk_row(...)    -- one uncurated dump function -> its bulk
                             address_versions row (kcdx_id NULL, fingerprint
                             columns from the dump, kind=function). The rebuild's
                             step-2 bulk pass calls this; `apply` never does
                             (it does not read the dump).

  - build_curated_row(...) -- THE shared curated constructor. Given the curated
                             seed facts + resolved ids, returns the final
                             address_versions column dict for a curated entity.
                             Handles all three row-kind classes via `base_row`:
                               * base_row is a matched bulk dict (function kind
                                 promoted from the dump) -> KEEP its fingerprint
                                 columns (length/content_hash/observed_arg_slots/
                                 caller_reg_arg_count/caller_arg_agreement plus
                                 the DEV auto_name/decompile_quality), overwrite
                                 the curated/audit fields.
                               * base_row is None (minted, with or without rva)
                                 -> fingerprint columns NULL.
                             The promote-vs-mint distinction is the SAME code
                             path; only `base_row` differs, which is exactly what
                             keeps a promoted row and a minted row byte-identical
                             to the rebuild's inline construction.

There is NO derivation. kind / offset / vtable_slot / struct_offset are AUTHORED
seed columns: the caller reads each straight from the address_versions_seed.csv
cell (kind via ss.authored_kind; the per-kind datum cells via parse_int), encodes
the kind id, and hands them to build_curated_row. No prose parsing, no `notes`
interpretation, no heuristic inference for any value the importer writes. This
keeps the row-builder a pure column-assembly function (no Dicts dependency, no
heuristics) that both the rebuild and `apply` call with the SAME authored column
values, so they emit byte-identical rows.
"""


def build_bulk_row(av_id, rv, dump_row, sig_row, cra_row, *,
                   module_id, valid_from_id, kind_id, agreement_id,
                   decompile_quality_id, length, content_hash,
                   struct_offset=None):
    """One uncurated dump function -> its bulk address_versions row.

    kcdx_id is NULL (uncurated; the seed pass promotes some of these to curated
    later by passing the resulting dict as build_curated_row's base_row). The
    fingerprint columns come from the dump (dump_row/sig_row/cra_row); the four
    audit columns are NULL (no maintainer signed off on a bulk row).

    The caller pre-encodes kind_id / agreement_id / decompile_quality_id and
    pre-parses length / content_hash (those need the shared Dicts encoder + the
    value codecs, which the caller owns) and passes them in, keeping this a pure
    assembly function.
    """
    return {
        "id": av_id,                       # explicit; matches rva_to_av_id
        "kcdx_id": None,                   # NULL = uncurated bulk; seed pass sets when curated
        "kind": kind_id,
        "module_id": module_id,
        "rva": rv,
        "length": length,
        "content_hash": content_hash,
        "value": None,
        "signature": (sig_row.get("signature") if sig_row else None) or None,
        "observed_arg_slots": _pi(sig_row.get("observed_arg_slots", "")) if sig_row else None,
        "caller_reg_arg_count": _pi(cra_row.get("caller_reg_arg_count", "")) if cra_row else None,
        "caller_arg_agreement": agreement_id,
        "offset": None,
        "vtable_slot": None,
        # Bulk rows are uncurated -- no maintainer ever signed off on them.
        # All four audit columns NULL; status is derived (always "unverified"
        # at any current_version for a NULL last_verified_at_version).
        "last_verified_at_version": None,
        "verified_by": None,
        "verified_date": None,
        "evidence_kind": None,
        "auto_name": (dump_row.get("auto_name") or None),
        "decompile_quality": decompile_quality_id,
        "valid_from": valid_from_id,
        "valid_through": None,
        # struct_offset: NULL for bulk (uncurated dump rows carry no authored
        # vtable/struct byte offset).
        "struct_offset": struct_offset,
    }


def build_curated_row(av_id, kid, *, base_row=None, module_id, rva,
                      valid_from_id, kind_id, signature,
                      lvv_id, verified_by, verified_date, evidence_kind_id,
                      offset=None, vtable_slot=None, struct_offset=None,
                      aob=None, anchor_string=None, rule=None, slot_count=None,
                      expect_unique=None, derives_from=None):
    """THE shared curated address_versions row constructor.

    `base_row` is the matched bulk dict (function-kind promote) or None (mint).
    The curated/audit fields always come from the seed; the fingerprint columns
    come from base_row when promoting, else NULL.

    Mirrors import_to_sqlite.py's original step-6 exactly:
      - mint (base_row is None): fresh dict, fingerprints NULL.
      - promote (base_row given): copy the bulk dict, then overwrite the curated
        fields; signature is the AUTHORED seed cell, NULL when blank (a promoted
        function with no seed signature does NOT inherit the dump/abi_walker
        floor `? (...)` -- the DB carries only what the seed authored, so
        DB<->CSV stay information-equivalent for the round-trip); offset /
        vtable_slot / struct_offset only set when non-None; vtable_slot also
        mirrors into `value`.

    value / offset / vtable_slot / struct_offset are now AUTHORED per-kind seed
    columns (importer-no-prose-derivation Phase 2). The caller passes the
    authored cell when present, else the current derived value (the
    authored-wins-else-fallback seam lives in the importer's build_rows /
    _seed_action_rows -- Phase 3 removes the fallback). The `value = vtable_slot`
    mirror is RETAINED in this step: for the current all-empty seed cells it
    changes no emitted row (vtable_slot is None for every existing row, so the
    mirror branch never runs). struct_offset is pure NEW plumbing -- NULL for
    every current row.

    The six folded survival columns (aob / anchor_string / rule / slot_count /
    expect_unique / derives_from) carry the per-kind re-find facts ON the av row
    (D22 / design §11.2 -- the former `survival` sibling table folded into
    address_versions, the sole home). A caller passes the per-kind cells (the
    rebuild + every add/update path compute them from survival_builder's per-kind
    dispatch via folded_av_cells); an unpassed cell stays NULL on both the mint
    and promote paths. content_hash/length are NOT folded columns -- they are the
    av row's body fingerprint, set by the promote/mint gate.
    """
    if base_row is None:
        v = {
            "id": av_id,
            "kcdx_id": kid,
            "kind": kind_id,
            "module_id": module_id,
            "rva": rva,
            "length": None,
            "content_hash": None,
            "value": None,
            "signature": signature or None,
            "observed_arg_slots": None,
            "caller_reg_arg_count": None,
            "caller_arg_agreement": None,
            "offset": None,
            "vtable_slot": None,
            "last_verified_at_version": lvv_id,
            "verified_by": verified_by or None,
            "verified_date": verified_date or None,
            "evidence_kind": evidence_kind_id,
            "auto_name": None,
            "decompile_quality": None,
            "valid_from": valid_from_id,
            "valid_through": None,
            "struct_offset": None,
            # Folded survival columns (D22 / design §11.2). NULL on the minted
            # dict; populated only when the caller passes the authored cell
            # (step 2 wires that). The promote branch (a copied bulk base_row)
            # does not carry these keys, so the common tail below sets each
            # when non-None -- keeping a promoted row's dict consistent with a
            # minted row's. ADDITIVE plumbing this step: every caller passes
            # None, so every cell is NULL.
            "aob": None,
            "anchor_string": None,
            "rule": None,
            "slot_count": None,
            "expect_unique": None,
            "derives_from": None,
        }
    else:
        # Promote: keep the bulk row's fingerprint + DEV columns; overwrite the
        # curated identity + audit fields. (signature is set in the common tail
        # below -- authored-wins-else-NULL, applied to promote + mint alike.)
        v = dict(base_row)
        v["kcdx_id"] = kid
        v["kind"] = kind_id
        v["last_verified_at_version"] = lvv_id
        v["verified_by"] = verified_by or None
        v["verified_date"] = verified_date or None
        v["evidence_kind"] = evidence_kind_id

    # Common promote tail (applies to mint + promote alike, matching the
    # original's post-construction block). signature persists the AUTHORED seed
    # cell, NULL when blank -- a blank seed signature on a PROMOTED function-kind
    # row must NOT inherit the bulk-dump abi_walker floor (`? (...)`): the DB
    # carries only what the seed authored, keeping DB<->CSV information-equivalent
    # (design.md round-trip contract). The survival/fingerprint path keys
    # function kinds on the body-hash (survival_builder.py function_hash), not
    # the signature, so NULLing the floor changes no survival behaviour.
    v["signature"] = signature or None
    if offset is not None:
        v["offset"] = offset
    if vtable_slot is not None:
        v["vtable_slot"] = vtable_slot
        v["value"] = vtable_slot
    if struct_offset is not None:
        v["struct_offset"] = struct_offset
    # Folded survival columns (D22 / design §11.2) -- set only when the caller
    # passes the authored cell, matching offset/vtable_slot/struct_offset above.
    # The INSERT reads row.get(col), so an unset key writes NULL; a None arg
    # leaves the column NULL (the mint dict seeds them None; the promote dict has
    # no such key, also -> NULL). This step's callers all pass None (additive,
    # pre-populate); step 2 wires the authored values.
    if aob is not None:
        v["aob"] = aob
    if anchor_string is not None:
        v["anchor_string"] = anchor_string
    if rule is not None:
        v["rule"] = rule
    if slot_count is not None:
        v["slot_count"] = slot_count
    if expect_unique is not None:
        v["expect_unique"] = expect_unique
    if derives_from is not None:
        v["derives_from"] = derives_from
    return v


# Local copy of the int parser to keep build_bulk_row self-contained for the two
# columns it parses directly. Imported from dict_codec by name so there is one
# definition; aliased here only for readability at the call sites above.
from .dict_codec import parse_int as _pi  # noqa: E402
