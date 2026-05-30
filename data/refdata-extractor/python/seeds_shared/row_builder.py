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

The kind/offset/vtable_slot DERIVATION (infer_kind + kind_offset_and_slot) stays
in import_to_sqlite.py: it is heuristic interpretation of the seed `notes`/`rva`,
not row assembly. The caller derives kind/offset/vtable_slot and the dict-encoded
kind id, then hands them to build_curated_row. This keeps the row-builder a pure
column-assembly function (no Dicts dependency, no heuristics) that `apply` can
call with its own version id and derived kind without dragging the rebuild's
inference machinery along.
"""


def build_bulk_row(av_id, rv, dump_row, sig_row, cra_row, *,
                   module_id, valid_from_id, kind_id, agreement_id,
                   decompile_quality_id, length, content_hash):
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
    }


def build_curated_row(av_id, kid, *, base_row=None, module_id, rva,
                      valid_from_id, kind_id, signature,
                      lvv_id, verified_by, verified_date, evidence_kind_id,
                      offset=None, vtable_slot=None):
    """THE shared curated address_versions row constructor.

    `base_row` is the matched bulk dict (function-kind promote) or None (mint).
    The curated/audit fields always come from the seed; the fingerprint columns
    come from base_row when promoting, else NULL.

    Mirrors import_to_sqlite.py's original step-6 exactly:
      - mint (base_row is None): fresh dict, fingerprints NULL.
      - promote (base_row given): copy the bulk dict, then overwrite the curated
        fields; signature only overwritten when the seed supplied one (so a
        promoted function with no seed signature keeps the dump/abi_walker
        floor); offset / vtable_slot only set when derived non-None; vtable_slot
        also mirrors into `value`.
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
        }
    else:
        # Promote: keep the bulk row's fingerprint + DEV columns; overwrite the
        # curated identity + audit fields.
        v = dict(base_row)
        v["kcdx_id"] = kid
        v["kind"] = kind_id
        v["last_verified_at_version"] = lvv_id
        v["verified_by"] = verified_by or None
        v["verified_date"] = verified_date or None
        v["evidence_kind"] = evidence_kind_id
        if signature:
            v["signature"] = signature

    # Common promote tail (applies to mint + promote alike, matching the
    # original's post-construction block).
    if signature:
        v["signature"] = signature
    if offset is not None:
        v["offset"] = offset
    if vtable_slot is not None:
        v["vtable_slot"] = vtable_slot
        v["value"] = vtable_slot
    return v


# Local copy of the int parser to keep build_bulk_row self-contained for the two
# columns it parses directly. Imported from dict_codec by name so there is one
# definition; aliased here only for readability at the call sites above.
from .dict_codec import parse_int as _pi  # noqa: E402
