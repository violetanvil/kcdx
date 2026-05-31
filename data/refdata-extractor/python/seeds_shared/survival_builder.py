"""seeds_shared.survival_builder -- THE single survival-row constructor.

Maps a curated entity's (kind, the row's survival seed columns, the function
fingerprint when applicable, the resolved derives_from address_version_id) to the
`survival` column dict that gets INSERTed -- ONE per curated address_versions row.
The rebuild path (build_rows) and the incremental apply path BOTH call
build_survival_row, so the two writers cannot emit a different survival shape --
the same no-drift discipline as build_curated_row (row_builder.py).

PER-KIND DISPATCH (the kind-class decides kind_form + which payload column(s)):

  function / function_no_sig / function_variadic
      -> kind_form='function_hash'; payload = content_hash + length, REUSED from
         the address_versions row's already-computed body fingerprint (no seed
         authoring). When the av row has no fingerprint (a function with no bulk
         baseline) these are simply NULL.
  callsite, instruction_anchor
      -> kind_form='aob'; payload = the seed survival_aob column +
         survival_expect_unique (the AOB-unique assertion).
  string_anchor
      -> kind_form='literal'; payload = the seed survival_anchor_string column +
         survival_expect_unique (the unique-xref assertion).
  data_slot
      -> kind_form='derivation'; payload = the seed survival_rule column;
         derives_from set (resolved from the seed survival_derives_from kcdx_id).
  vtable_base
      -> kind_form='table_shape'; payload = the seed survival_slot_count column.
  vtable_index
      -> kind_form='slot_target'; population DEFERRED -- the row is emitted with
         kind_form set and the target-hash payload EMPTY (gated on the runtime-
         vtable verification path). derives_from is still carried when the seed
         supplies the base ref; the index already lives on the av row's
         vtable_slot/value column, so no redundant index column is added here.

EMPTY-NOT-GUESSED: when a kind's seed survival column has not been filled yet
(step 5.2 hasn't authored it), the survival row is STILL emitted with kind_form
set and the payload field(s) left empty. The builder NEVER guesses a value and
NEVER parses the `notes` prose -- an empty seed column -> an empty payload.

derives_from RESOLUTION: the seed carries `survival_derives_from` as a kcdx_id
(the entity this row depends on). The survival table's `derives_from` is an FK to
address_versions.id. The CALLER maps the kcdx_id -> the dependency entity's
curated address_versions.id and passes the resolved av_id in (the builder stays a
pure column-assembly function, no DB/lookup access -- mirroring row_builder.py's
"the caller resolves ids" discipline).
"""

from .schema import FUNCTION_KINDS


# Map an ADDRESS_KIND to its survival kind_form. One entry per kind; the dict is
# the single source of the kind -> kind_form mapping both writers share.
_KIND_TO_FORM = {
    "function":           "function_hash",
    "function_no_sig":    "function_hash",
    "function_variadic":  "function_hash",
    "callsite":           "aob",
    "instruction_anchor": "aob",
    "string_anchor":      "literal",
    "data_slot":          "derivation",
    "vtable_base":        "table_shape",
    "vtable_index":       "slot_target",
}


def survival_kind_form(kind):
    """The survival kind_form for an ADDRESS_KIND. Fails LOUD on an unknown kind
    (a new ADDRESS_KIND must declare its survival form here, else the survival
    pass has no dispatch for it)."""
    form = _KIND_TO_FORM.get(kind)
    if form is None:
        raise RuntimeError(
            f"survival_builder: no survival kind_form for address kind {kind!r} "
            f"(every ADDRESS_KIND must map to a kind_form in _KIND_TO_FORM)")
    return form


def build_survival_row(av_id, kind, *, survival_aob=None, anchor_string=None,
                       rule=None, slot_count=None, expect_unique=None,
                       derives_from_av_id=None,
                       content_hash=None, length=None):
    """THE shared survival-row constructor: one curated address_versions row ->
    its `survival` column dict (1:1).

    Args:
      av_id              -- the curated address_versions.id this survives (FK).
      kind               -- the ADDRESS_KIND (decides kind_form + payload column).
      survival_aob       -- seed survival_aob (aob form), already format-validated;
                            None/'' -> empty payload.
      anchor_string      -- seed survival_anchor_string (literal form).
      rule               -- seed survival_rule (derivation form).
      slot_count         -- seed survival_slot_count as int (table_shape form).
      expect_unique      -- seed survival_expect_unique as int 0/1 (aob +
                            literal forms: the AOB-unique / unique-xref
                            assertion); None for the forms that don't use it.
      derives_from_av_id -- the dependency entity's RESOLVED address_versions.id
                            (the caller maps survival_derives_from kcdx_id -> av_id),
                            or None.
      content_hash       -- the av row's content_hash BLOB (function_hash form);
                            reused, not authored.
      length             -- the av row's length int (function_hash form); reused.

    Returns the survival column dict (all `survival` SCHEMA columns present, the
    ones a kind doesn't use left None). The 'id' column is omitted -- it is the
    table's autoincrement PK, assigned by SQLite at INSERT (the same handle-vs-
    payload split build_curated_row uses for address_versions.id under apply)."""
    form = survival_kind_form(kind)

    # Base row: every column NULL except address_version_id + kind_form + the
    # always-carried derives_from. Per-kind payload is filled in below.
    v = {
        "address_version_id": av_id,
        "kind_form": form,
        "derives_from": derives_from_av_id,
        "aob": None,
        "anchor_string": None,
        "rule": None,
        "slot_count": None,
        "expect_unique": None,
        "content_hash": None,
        "length": None,
    }

    if kind in FUNCTION_KINDS:
        # function_hash: carry the body fingerprint already on the av row. REUSED
        # -- no seed authoring. When the av row carries no fingerprint these are
        # simply NULL (a function with no bulk baseline; the survival pass treats
        # an absent hash as not-yet-fingerprinted, never as a forged datum).
        v["content_hash"] = content_hash
        v["length"] = length
    elif form == "aob":
        # callsite / instruction_anchor: the AOB pattern (with the ? wildcard mask
        # folded in) + the AOB-unique assertion. Empty until step 5.2.
        v["aob"] = survival_aob or None
        v["expect_unique"] = expect_unique
    elif form == "literal":
        # string_anchor: the literal bytes + the unique-xref assertion. Empty
        # until step 5.2.
        v["anchor_string"] = anchor_string or None
        v["expect_unique"] = expect_unique
    elif form == "derivation":
        # data_slot: the derivation rule (+ derives_from, already set above from
        # the resolved kcdx_id). Empty rule until step 5.2.
        v["rule"] = rule or None
    elif form == "table_shape":
        # vtable_base: the expected slot count. Empty until step 5.2.
        v["slot_count"] = slot_count
    elif form == "slot_target":
        # vtable_index: DEFERRED population. Emit the row with kind_form set and
        # the target-hash payload EMPTY (gated on the runtime-vtable path). The
        # base ref is carried via derives_from when the seed supplies it; the
        # index already lives on the av row's vtable_slot/value column (no
        # redundant index column here). NEVER guess the target hash.
        pass

    return v
