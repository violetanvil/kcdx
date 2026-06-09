"""cross_impl_fixture.py -- the cross-implementation per-kind survival fixture
(the TEST-OF-RECORD, TRD D27). The single source-of-truth table of
`(kcdx_id, kind) -> on-disk byte-slice + pinned expected survival verdict` that the
JS<->Python (Phase 2) and JS<->C++ (Phase 3) agreement tests all pin against.

WHY THIS EXISTS
---------------
The verify engine ships in three implementations (Python reference checker, the
in-browser JS checker, the C++ engine survival pass). All three must reproduce the
SAME survival verdict on the SAME on-disk bytes for each address kind. This module is
the one place that declares those ground-truth verdicts; every agreement test builds
the SAME documented byte-slices on its own side and asserts the SAME pinned verdict
here. It extends the EXISTING version-read agreement precedent
(test_version_resolver_js_agreement.py + makeFakePE.ts: a shared documented byte block
`AGREE_INTERN` + a pinned `AGREE_EXPECTED`) from the version read to the per-kind
survival checks.

THE VERDICT MODEL (corrected D25; data/maintainer-tool/design.md)
----------------------------------------------------------------
The version-applicability check hashes/scans the body at the rva from the ON-DISK DLL
FILE (not the loaded image), comparing to the recorded datum. So each slice's expected
verdict is an ON-DISK byte/hash/AOB verdict:

  - Unchanged    -- the on-disk slice matches the recorded survival datum
                    (version matches; "resolves+works").
  - Changed      -- the bytes differ (a build diverged; "wrong-target").
  - Ambiguous    -- a locating kind no longer resolves to a single site (multiple AOB
                    hits) -- the locator went stale into non-uniqueness.
  - CannotCheck  -- the kind cannot be survival-checked statically from the on-disk DLL
                    alone (vtable_index -- no RVA, needs a runtime slot target;
                    deferred within this design).

Reachability (resolve-into-live-.text) is an engine-only LIVE check, NOT part of this
static byte fixture -- the fixture covers ONLY the on-disk byte/hash/AOB checks the
JS<->Python<->C++ agreement pins.

NO CHECKER LOGIC HERE
---------------------
This module is fixture DATA + loaders only. It does NOT implement any per-kind survival
check -- that is the Phase-1 Python reference checker (and the JS/C++ ports). The verdict
each row carries is the GROUND-TRUTH answer those checkers must reproduce, derived from
data/maintainer-tool/fingerprint-per-kind.md ("Per-kind fingerprint table"), never from a
checker's output. Trust level: PRIMARY EVIDENCE -- each slice's bytes + verdict trace to a
real seed row, a real mini-dump row, a captured probe finding, or a documented-and-
reproducible synthetic span (.claude/rules/working-artifacts.md trust axis).

THE BYTES THAT MATTER
---------------------
Like the version-read precedent (the PE header scaffolding around the interns is
irrelevant to what the regex sees), each slice here is JUST the bytes the kind's check
operates on -- a function-body span, a `.text`-like block for an AOB scan, a `.rdata`-
like block for a literal search, a qword table, an anchor instruction. No PE scaffolding;
the check sees exactly these bytes. A Phase-2/3 agreement test builds the SAME bytes on
its side (the bytes are documented byte-for-byte below) and asserts the SAME verdict.

RUN
---
    python -m pytest tests/test_cross_impl_fixture.py
"""

# ---------------------------------------------------------------------------
# The verdict vocabulary (data/maintainer-tool/fingerprint-per-kind.md).
# ---------------------------------------------------------------------------
VERDICT_UNCHANGED = "Unchanged"
VERDICT_CHANGED = "Changed"
VERDICT_AMBIGUOUS = "Ambiguous"
VERDICT_CANNOT_CHECK = "CannotCheck"

VERDICTS = frozenset({
    VERDICT_UNCHANGED, VERDICT_CHANGED, VERDICT_AMBIGUOUS, VERDICT_CANNOT_CHECK,
})

# The in-scope checkable kinds (one fixture row -- often several slices -- per kind).
# vtable_index is the CannotCheck case, declared separately below.
IN_SCOPE_KINDS = (
    "function",
    "callsite",
    "string_anchor",
    "instruction_anchor",
    "data_slot",
    "vtable_base",
)


# ---------------------------------------------------------------------------
# Slice + row datatypes (plain dicts so any consumer -- a JS-side port reading an
# exported JSON, the C++ side, a Python test -- reads the same shape; no class
# coupling). A row is one (kcdx_id, kind) with a verdict-per-slice list.
# ---------------------------------------------------------------------------
def _slice(name, body, verdict, detail):
    """One documented on-disk byte-slice + its pinned verdict.

    body    -- the exact bytes the kind's check operates on (bytes).
    verdict -- one of VERDICTS: the GROUND-TRUTH a checker must reproduce on `body`.
    detail  -- the fingerprint-per-kind.md rule the verdict derives from + why this
               slice lands that verdict (the falsifiable claim the agreement pins).
    """
    assert verdict in VERDICTS, "unknown verdict %r" % (verdict,)
    assert isinstance(body, (bytes, bytearray)), "slice body must be bytes"
    return {"name": name, "body": bytes(body), "verdict": verdict, "detail": detail}


def _row(kcdx_id, kind, name, datum, source, slices):
    """One (kcdx_id, kind) fixture row.

    datum   -- the recorded survival datum the check compares against (the body
               BLAKE3 for function, the AOB pattern for callsite, the literal for
               string_anchor, the slot count for vtable_base, the derivation triple
               for the derivation kinds). The exact thing fingerprint-per-kind.md
               says to store, mirrored here so the slice's verdict is self-contained.
    source  -- provenance of the datum + verdict (the seed row / mini-dump row /
               _research finding / documented synthetic), the trust-axis record.
    slices  -- list of _slice(): at least one Unchanged + (where applicable) the
               Changed/Ambiguous counter-slices that make the verdict falsifiable.
    """
    assert kind in IN_SCOPE_KINDS or kind == "vtable_index"
    return {
        "kcdx_id": kcdx_id, "kind": kind, "name": name,
        "datum": datum, "source": source, "slices": list(slices),
    }


# ===========================================================================
# function -- BODY HASH (fingerprint-per-kind.md "function ... -- body hash").
# Check: re-hash [rva, rva+length) on-disk, compare to the recorded content_hash.
# Slice = a function-body byte span. Unchanged when its BLAKE3 == the recorded
# hash; Changed when a byte is flipped.
#
# The slice is a documented 38-byte synthetic body span (a length matching the real
# mini-dump function row rva=0x1020 length=38, so the slice mirrors a real body's
# size). Its content_hash is the genuine BLAKE3 of these exact bytes, computed once
# via the repo's vetted BLAKE3 oracle
# (data/refdata-extractor/ghidra/blake3/Blake3Hex.java -- the SAME BLAKE3 the
# production Ghidra extractor stamps into a function's content_hash). The Phase-1+
# checker re-hashes the slice with its own BLAKE3 and MUST reproduce this hash ->
# Unchanged. (A real recorded content_hash is NOT carried in the seed CSV -- it is a
# derived/bulk-promote datum -- so a documented-reproducible synthetic span is the
# correct ground-truth here, per the step's "synthetic span with a computed BLAKE3"
# allowance.)
# ===========================================================================
_FN_BODY = bytes.fromhex(
    "48895c2408574883ec20488bf933db4885c9740ae8112233448bd885c07505488bcfff15aabb"
)
# BLAKE3(_FN_BODY), computed via data/refdata-extractor/ghidra/blake3/Blake3Hex.java
# (OpenJDK 21, the repo's vetted Apache-codec Blake3). Reproduce:
#   printf '<the 38 bytes>' | java -cp <Blake3Hex classpath> Blake3Hex
_FN_BODY_BLAKE3 = "369a169b998ceea094cebe3f3d4e8206a5b227906306d071c2f9eb8e78dc0b88"
# A one-byte-flipped copy (last byte 0xbb -> 0xbc). Its BLAKE3 != _FN_BODY_BLAKE3, so
# the recorded-hash comparison yields Changed -- the falsifiable counter-slice.
_FN_BODY_FLIPPED = _FN_BODY[:-1] + b"\xbc"

_ROW_FUNCTION = _row(
    kcdx_id=999001, kind="function", name="synthetic_body_hash_fixture",
    datum={"content_hash": _FN_BODY_BLAKE3, "length": len(_FN_BODY)},
    source=(
        "Documented synthetic 38-byte body span (length mirrors the real mini-dump "
        "function row rva=0x1020 length=38); content_hash is the genuine BLAKE3 of "
        "these exact bytes via the repo's BLAKE3 oracle "
        "(data/refdata-extractor/ghidra/blake3/Blake3Hex.java)."
    ),
    slices=[
        _slice(
            "unchanged", _FN_BODY, VERDICT_UNCHANGED,
            "fingerprint-per-kind.md function body-hash: re-hash [rva,rva+length) and "
            "compare to content_hash. BLAKE3(body) == the recorded content_hash -> "
            "Unchanged. FALSIFIES if a checker's BLAKE3 of this span != the stored hash.",
        ),
        _slice(
            "changed_byte_flip", _FN_BODY_FLIPPED, VERDICT_CHANGED,
            "Same span with the final byte flipped (0xbb->0xbc): BLAKE3 differs from "
            "the recorded content_hash -> Changed. FALSIFIES if a flipped body still "
            "hashes equal (a broken hash) or is reported Unchanged.",
        ),
    ],
)


# ===========================================================================
# callsite -- AOB RE-MATCH (fingerprint-per-kind.md "callsite -- AOB re-match").
# Check: scan a `.text`-like block for the stored AOB (bytes + mask). Unchanged =
# exactly one hit; Changed = zero hits; Ambiguous = multiple hits.
#
# REAL DATUM: seed id 7 (IsInCombat_callsite_26b), kind=callsite, survival_aob =
# "48 8B 41 08 ... 3C 02" (26 bytes, no wildcards), survival_expect_unique=1.
# ===========================================================================
_CALLSITE_AOB = bytes.fromhex(
    # the exact 26-byte survival_aob from seed id 7 (no wildcards):
    "488B4108488B889000000048" "81C1600B0000" "488B01" "FF5008" "3C02"
)
assert len(_CALLSITE_AOB) == 26, len(_CALLSITE_AOB)

_CS_LEAD = bytes.fromhex("90909090cccccccc")   # filler so the hit is not at offset 0
_CS_TAIL = bytes.fromhex("9090")

_ROW_CALLSITE = _row(
    kcdx_id=7, kind="callsite", name="IsInCombat_callsite_26b",
    datum={"aob": _CALLSITE_AOB.hex(" "), "expect_unique": 1},
    source=(
        "Real seed row id 7 (address_versions_seed.csv) -- survival_aob "
        "'48 8B 41 08 ... 3C 02' (26 bytes), survival_expect_unique=1, "
        "evidence_kind=live_test_plugin."
    ),
    slices=[
        _slice(
            "unchanged_unique_hit", _CS_LEAD + _CALLSITE_AOB + _CS_TAIL,
            VERDICT_UNCHANGED,
            "fingerprint-per-kind.md callsite AOB re-match: the AOB occurs exactly ONCE "
            "in the `.text`-like block -> Unchanged. FALSIFIES if a scan finds 0 or >1.",
        ),
        _slice(
            "changed_zero_hits", _CS_LEAD + _CS_TAIL, VERDICT_CHANGED,
            "Block without the AOB anywhere: zero hits -> Changed (the site is gone). "
            "FALSIFIES if a scan reports a phantom hit.",
        ),
        _slice(
            "ambiguous_two_hits",
            _CS_LEAD + _CALLSITE_AOB + _CS_TAIL + _CALLSITE_AOB + _CS_TAIL,
            VERDICT_AMBIGUOUS,
            "Block with the AOB twice: multiple hits -> Ambiguous (the pattern is no "
            "longer a unique locator). FALSIFIES if a scan collapses two hits to "
            "Unchanged.",
        ),
    ],
)


# ===========================================================================
# string_anchor -- LITERAL PRESENCE (fingerprint-per-kind.md "string_anchor").
# Check: search a `.rdata`-like block for the stored literal. Present -> Unchanged;
# absent -> Changed.
#
# REAL DATUM: seed id 12 (string_exec_autoexec_cfg), kind=string_anchor,
# survival_anchor_string = "exec autoexec.cfg", survival_expect_unique=1.
# ===========================================================================
_ANCHOR_LITERAL = b"exec autoexec.cfg"
_RD_LEAD = b"\x00\x00\x00\x00"
_RD_TAIL = b"\x00OtherString\x00"

_ROW_STRING_ANCHOR = _row(
    kcdx_id=12, kind="string_anchor", name="string_exec_autoexec_cfg",
    datum={"anchor_string": _ANCHOR_LITERAL.decode("ascii"), "expect_unique": 1},
    source=(
        "Real seed row id 12 (address_versions_seed.csv) -- survival_anchor_string "
        "'exec autoexec.cfg', survival_expect_unique=1, evidence_kind=maintainer_ghidra."
    ),
    slices=[
        _slice(
            "unchanged_present", _RD_LEAD + _ANCHOR_LITERAL + _RD_TAIL,
            VERDICT_UNCHANGED,
            "fingerprint-per-kind.md string_anchor literal presence: the literal is "
            "present in the `.rdata`-like block -> Unchanged. FALSIFIES if a search "
            "misses a present literal.",
        ),
        _slice(
            "changed_absent", _RD_LEAD + b"\x00unrelated.cfg\x00" + _RD_TAIL,
            VERDICT_CHANGED,
            "Block WITHOUT the literal: absent -> Changed (the anchor is gone, every "
            "row re-deriving from it is now unresolvable). FALSIFIES if a search "
            "reports a phantom present.",
        ),
    ],
)


# ===========================================================================
# instruction_anchor -- DERIVATION CHAIN (fingerprint-per-kind.md
# "instruction_anchor -- resolver-chain re-derivation"; the disp32-follow probe 0.2
# validated). The anchor instruction's shape signature + its disp32-follow re-derives
# the expected target. Unchanged = the derivation lands on the expected target;
# Changed = it lands wrong / the shape no longer matches.
#
# REAL DATUM: probe 0.2 finding -- id 9 (gEnv_pConsole_mov_instruction) anchor RVA
# 0x0086AD99, raw 7 bytes `48 8b 0d 08 0b 0c 04` (mov rcx,[rip+disp32]),
# decoded disp32 0x040C0B08, derived target RVA 0x0492B8A8 (== id 10 ground truth,
# EXACT). seed id 9 survival_aob "48 8B 0D ?? ?? ?? ??".
#
# The slice is the 7 anchor-instruction bytes; the verdict is the derivation outcome
# (lands on the expected target RVA). The Changed slice flips the opcode so the
# instruction-shape no longer matches the stored AOB shape.
# ===========================================================================
_IA_ANCHOR_BYTES = bytes.fromhex("488b0d080b0c04")   # mov rcx,[rip+0x040C0B08]
assert len(_IA_ANCHOR_BYTES) == 7
_IA_ANCHOR_RVA = 0x0086AD99
_IA_EXPECTED_TARGET_RVA = 0x0492B8A8  # == seed id 10 data_slot ground truth
# A shape mismatch: opcode 0x8b (MOV) -> 0x8d (LEA) keeps a valid RIP-relative form but
# is NOT the stored MOV shape; (and more bluntly, ModRM byte corrupted) -> the shape
# check fails / the chain no longer matches the expected MOV anchor.
_IA_ANCHOR_WRONG_SHAPE = bytes.fromhex("48ff0d080b0c04")  # 0xff in the opcode slot: not the MOV/LEA shape

_ROW_INSTRUCTION_ANCHOR = _row(
    kcdx_id=9, kind="instruction_anchor", name="gEnv_pConsole_mov_instruction",
    datum={
        "aob": "48 8B 0D ?? ?? ?? ??",
        "anchor_rva": _IA_ANCHOR_RVA,
        "rule": "disp32@9",
        "expected_target_rva": _IA_EXPECTED_TARGET_RVA,
        "expect_unique": 1,
    },
    source=(
        "Real seed row id 9 (survival_aob '48 8B 0D ?? ?? ?? ??') + probe 0.2 finding "
        "(_research/maintainer-tool-verification-engine/probe-0.2-x86-decoder-finding.md): "
        "anchor RVA 0x0086AD99, raw 7 bytes '48 8b 0d 08 0b 0c 04', decoded disp32 "
        "0x040C0B08, derived target RVA 0x0492B8A8 == id 10 ground truth (EXACT match)."
    ),
    slices=[
        _slice(
            "unchanged_lands_on_target", _IA_ANCHOR_BYTES, VERDICT_UNCHANGED,
            "fingerprint-per-kind.md instruction_anchor derivation: decode the 7-byte "
            "MOV `48 8B 0D` + disp32 and follow it -- target = (0x0086AD99 + 7) + "
            "0x040C0B08 = 0x0492B8A8 (the expected target, id 10). The shape matches "
            "the stored AOB '48 8B 0D ?? ?? ?? ??' and the derivation lands on the "
            "expected RVA -> Unchanged. FALSIFIES if the disp32-follow lands != "
            "0x0492B8A8 or the shape does not match.",
        ),
        _slice(
            "changed_wrong_shape", _IA_ANCHOR_WRONG_SHAPE, VERDICT_CHANGED,
            "Anchor with a non-MOV/LEA opcode byte (0x48 0xff ...): the instruction "
            "shape no longer matches the stored '48 8B 0D ...' AOB shape, so the "
            "derivation chain cannot complete on the expected shape -> Changed. "
            "FALSIFIES if a checker decodes a non-RIP-relative-MOV as the anchor.",
        ),
    ],
)


# ===========================================================================
# data_slot -- DERIVATION CHAIN (fingerprint-per-kind.md "data_slot -- structural";
# probe 0.2's disp32-follow). A data_slot is reached by following the disp32 from its
# instruction_anchor (rule `disp32@<kid>`). Unchanged = the derivation still REACHES
# the slot at the expected target; Changed = it lands wrong. There is NO content hash
# (a byte hash of a data_slot is an anti-signal; .data holds relocated pointers).
#
# REAL DATUM: seed id 10 (gEnv_pConsole), kind=data_slot, survival_rule = "disp32@9",
# survival_derives_from = 9; resolves THROUGH id 9's anchor instruction. probe 0.2:
# the disp32-follow at id 9 lands EXACTLY on id 10's RVA 0x0492B8A8.
#
# The slice is the SAME 7 anchor-instruction bytes the derivation runs over (the
# data_slot's survival is its derivation, which operates on its anchor's bytes -- the
# slot's own bytes are not hashed). Unchanged when the disp32-follow reaches the
# expected slot RVA; Changed when the displacement is altered so it lands elsewhere.
# ===========================================================================
_DS_DERIVED_SLOT_RVA = 0x0492B8A8
# Anchor whose disp32 is altered (+0x10) so the follow lands at slot+0x10, NOT the
# recorded slot RVA -> the derivation reaches a different `.data` offset -> Changed.
_DS_ANCHOR_WRONG_DISP = bytes.fromhex("488b0d180b0c04")  # disp32 0x040C0B18 (+0x10)

_ROW_DATA_SLOT = _row(
    kcdx_id=10, kind="data_slot", name="gEnv_pConsole",
    datum={
        "rule": "disp32@9",
        "derives_from": 9,
        "anchor_rva": _IA_ANCHOR_RVA,
        "expected_slot_rva": _DS_DERIVED_SLOT_RVA,
    },
    source=(
        "Real seed row id 10 (survival_rule 'disp32@9', survival_derives_from 9) + "
        "probe 0.2 finding: following the disp32 at id 9 (0x0086AD99) lands EXACTLY on "
        "id 10's slot RVA 0x0492B8A8 (anchor-instruction RVA -> disp32 -> target RVA, "
        "EXACT). No content hash -- a data_slot's survival is its derivation."
    ),
    slices=[
        _slice(
            "unchanged_derivation_reaches_slot", _IA_ANCHOR_BYTES, VERDICT_UNCHANGED,
            "fingerprint-per-kind.md data_slot derivation: re-run rule 'disp32@9' over "
            "the anchor (id 9) bytes -- (0x0086AD99 + 7) + 0x040C0B08 = 0x0492B8A8, the "
            "recorded slot RVA -> Unchanged. FALSIFIES if the derivation reaches a "
            "different offset or a byte hash of the slot is used instead.",
        ),
        _slice(
            "changed_derivation_lands_wrong", _DS_ANCHOR_WRONG_DISP, VERDICT_CHANGED,
            "Anchor with disp32 altered by +0x10 (0x040C0B18): the follow lands at "
            "0x0492B8B8, NOT the recorded 0x0492B8A8 -> Changed (the derivation no "
            "longer reaches the verified slot). FALSIFIES if a checker ignores the "
            "displacement and reports Unchanged.",
        ),
    ],
)


# ===========================================================================
# vtable_base -- TABLE-SHAPE (fingerprint-per-kind.md "vtable_base -- table-shape").
# Check: at the stored RVA read N qwords; Unchanged iff there are N and each resolves
# into `.text`. A shrunk/grown table or non-pointer contents -> Changed. NOT a byte
# hash (the pointers relocate every build).
#
# REAL DATUM: seed id 138 (kind=vtable_base, survival_slot_count=3) -- a compact real
# vtable_base with a small N so the slice is small. (The .text range is the structural
# assertion "each qword resolves into .text"; the window is the REAL WHGame.dll .text
# range [0x1000, 0x3A01E1A), and the three qwords are real id-138 .text RVAs that
# resolve into it -- so the agreement is over the SHAPE check, N qwords each a code
# pointer into .text. The pointer VALUES are not hashed; they relocate per build.)
# ===========================================================================
_VB_SLOT_COUNT = 3
# The `.text` RVA window the qwords must fall inside (the structural assertion) -- the
# real WHGame.dll .text range [0x1000, 0x3A01E1A); all three id-138 RVAs resolve into it.
_VB_TEXT_LO = 0x1000
_VB_TEXT_HI = 0x3A01E1A


def _qwords(*vals):
    return b"".join(int(v).to_bytes(8, "little") for v in vals)


_VB_GOOD_TABLE = _qwords(0x0071A5A4, 0x00667B24, 0x03993898)   # 3 real id-138 .text RVAs (all in [0x1000,0x3A01E1A))
# Shrunk to 2 valid pointers + a non-pointer (0 is not a `.text` RVA) -> the N-qword
# shape no longer holds.
_VB_BAD_TABLE = _qwords(0x0071A5A4, 0x00667B24, 0x00000000)

_ROW_VTABLE_BASE = _row(
    kcdx_id=138, kind="vtable_base", name="vtable_base_slot3_fixture",
    datum={
        "slot_count": _VB_SLOT_COUNT,
        "text_range": [_VB_TEXT_LO, _VB_TEXT_HI],
    },
    source=(
        "Real seed row id 138 (kind=vtable_base, survival_slot_count=3). The qword "
        "values are real id-138 `.text` RVAs that resolve into the real WHGame.dll "
        ".text window [0x1000, 0x3A01E1A) (vtable pointers relocate per build and are "
        "NOT hashed -- the survival check is the SHAPE: N qwords each resolving into "
        "`.text`)."
    ),
    slices=[
        _slice(
            "unchanged_n_valid_pointers", _VB_GOOD_TABLE, VERDICT_UNCHANGED,
            "fingerprint-per-kind.md vtable_base table-shape: read N=3 qwords; each is a "
            "real id-138 `.text` RVA (within [0x1000,0x3A01E1A)) -> Unchanged. "
            "FALSIFIES if a qword outside `.text` is accepted or the count check is "
            "skipped.",
        ),
        _slice(
            "changed_non_pointer_slot", _VB_BAD_TABLE, VERDICT_CHANGED,
            "Third qword is 0x0 (not a `.text` pointer): the N-qword all-resolve-into-"
            "`.text` shape no longer holds -> Changed. FALSIFIES if a non-pointer slot "
            "is treated as valid.",
        ),
    ],
)


# ===========================================================================
# vtable_index -- CANNOTCHECK (fingerprint-per-kind.md "vtable_index ... deferred
# within this design"). This kind has NO RVA and cannot be survival-checked statically
# from the on-disk DLL alone (the "intended method" identity lives at
# vtable_base[index], which needs a runtime slot target). The fixture row declares the
# CannotCheck expectation -- the verdict every implementation returns for this kind at
# this design stage.
#
# REAL DATUM: seed id 19 (IGame_CompleteInit_vtable_idx), kind=vtable_index, no rva,
# vtable_slot=4, status unverified (deferred on the runtime-vtable path).
# ===========================================================================
_ROW_VTABLE_INDEX = _row(
    kcdx_id=19, kind="vtable_index", name="IGame_CompleteInit_vtable_idx",
    datum={"vtable_slot": 4, "deferred": True},
    source=(
        "Real seed row id 19 (kind=vtable_index, no rva, vtable_slot=4; status "
        "unverified). fingerprint-per-kind.md marks vtable_index 'deferred within this "
        "design' -- its survival datum (resolve base, take slot, hash target body) "
        "needs the runtime-vtable verification path."
    ),
    slices=[
        # No byte-slice: there is nothing on-disk to check (no RVA). The verdict is the
        # CannotCheck declaration itself -- a slice with an empty body carrying the
        # CannotCheck verdict, so the agreement tests pin the same "this kind is not
        # statically checkable" answer across all three implementations.
        _slice(
            "cannot_check_no_static_target", b"", VERDICT_CANNOT_CHECK,
            "fingerprint-per-kind.md vtable_index is DEFERRED: no RVA, the slot target "
            "needs a runtime resolve -> CannotCheck. FALSIFIES if any implementation "
            "returns Unchanged/Changed for a vtable_index from on-disk bytes alone.",
        ),
    ],
)


# ---------------------------------------------------------------------------
# THE FIXTURE TABLE -- the source-of-truth list every agreement test pins against.
# Ordered: the 6 in-scope checkable kinds, then the vtable_index CannotCheck row.
# ---------------------------------------------------------------------------
FIXTURE_ROWS = (
    _ROW_FUNCTION,
    _ROW_CALLSITE,
    _ROW_STRING_ANCHOR,
    _ROW_INSTRUCTION_ANCHOR,
    _ROW_DATA_SLOT,
    _ROW_VTABLE_BASE,
    _ROW_VTABLE_INDEX,
)


# ---------------------------------------------------------------------------
# Loaders -- the API the Phase-1/2/3 agreement tests call.
# ---------------------------------------------------------------------------
def load_fixture():
    """Return the full fixture table (list of rows). Each row:
    {kcdx_id, kind, name, datum, source, slices:[{name, body, verdict, detail}]}."""
    return list(FIXTURE_ROWS)


def rows_for_kind(kind):
    """Every fixture row for `kind`."""
    return [r for r in FIXTURE_ROWS if r["kind"] == kind]


def expected_verdict(kcdx_id, slice_name):
    """The pinned verdict for one (kcdx_id, slice_name) -- the value a checker must
    reproduce. Raises KeyError if the pair is not in the fixture."""
    for r in FIXTURE_ROWS:
        if r["kcdx_id"] == kcdx_id:
            for s in r["slices"]:
                if s["name"] == slice_name:
                    return s["verdict"]
    raise KeyError("no fixture slice (%r, %r)" % (kcdx_id, slice_name))


# ---------------------------------------------------------------------------
# JSON EXPORT -- the cross-LANGUAGE contract (the Python source-of-truth -> JSON ->
# the C++ engine-agreement consumer). The Phase-3 (JS<->C++) agreement test runs the
# C++ engine static check over THESE byte-slices; the C++ side cannot import this
# Python module, so the fixture is exported to a JSON file the C++ test reads. This is
# the anticipated shape the header doc names ("an exported JSON, the C++ side, a Python
# test -- reads the same shape; no class coupling").
#
# The JSON is LOSSLESS (working-artifacts.md trust axis; AP14 -- never silently drops a
# row): every FIXTURE_ROWS row + every slice + every verdict round-trips. A slice's
# `body` is hex-encoded (JSON has no byte type); a checker re-derives the exact bytes
# with bytes.fromhex(slice["body"]). The derivation rows carry `anchor_rva` (the RVA the
# anchor instruction is planted at in a synthetic PE so the disp32-follow re-derives the
# recorded target); the datum carries the kind's recorded survival datum so a consumer
# builds the engine Payload without re-deriving it.
#
# The format is FLAT + self-describing: a top-level object {format_version, in_scope_kinds,
# verdicts, rows:[...]} so a C++ JSON reader walks rows -> slices with no schema lookup.
# ---------------------------------------------------------------------------
JSON_FORMAT_VERSION = 1


def _datum_to_json(datum):
    """Render a row's `datum` dict to a JSON-safe shape. The datum is already plain
    JSON types (strings/ints/bools/lists) for every kind -- no bytes live in a datum
    (the bytes are the slice bodies) -- so the datum passes through unchanged. Asserts
    that so a future datum that smuggles in bytes is caught loudly, never silently
    dropped (AP14)."""
    def _check(v):
        assert not isinstance(v, (bytes, bytearray)), \
            "datum carries raw bytes %r -- bytes belong in a slice body, not a datum" % (v,)
        if isinstance(v, dict):
            for vv in v.values():
                _check(vv)
        elif isinstance(v, (list, tuple)):
            for vv in v:
                _check(vv)
    _check(datum)
    return datum


def fixture_to_json_obj():
    """Build the lossless JSON-serializable object mirroring FIXTURE_ROWS. Every row +
    slice + verdict is present; slice bodies are hex strings (bytes.fromhex round-trips
    them exactly). This is the in-memory form; dump_fixture_json() writes it to disk."""
    rows = []
    for r in FIXTURE_ROWS:
        slices = [
            {
                "name": s["name"],
                "body": bytes(s["body"]).hex(),   # "" for an empty body (vtable_index).
                "verdict": s["verdict"],
                "detail": s["detail"],
            }
            for s in r["slices"]
        ]
        rows.append({
            "kcdx_id": r["kcdx_id"],
            "kind": r["kind"],
            "name": r["name"],
            "datum": _datum_to_json(r["datum"]),
            "source": r["source"],
            "slices": slices,
        })
    return {
        "format_version": JSON_FORMAT_VERSION,
        "in_scope_kinds": list(IN_SCOPE_KINDS),
        "verdicts": sorted(VERDICTS),
        "rows": rows,
    }


def parse_json_obj(obj):
    """The inverse of fixture_to_json_obj(): parse a loaded JSON object back into the
    FIXTURE_ROWS shape (slice bodies decoded from hex back to bytes). Used by the
    round-trip test to prove load_fixture() == the JSON's parse -- so a drift between
    the Python source-of-truth and the emitted JSON is caught. Raises on a malformed
    object (a missing key / a bad verdict), never a silent partial parse."""
    rows = []
    for r in obj["rows"]:
        slices = []
        for s in r["slices"]:
            verdict = s["verdict"]
            if verdict not in VERDICTS:
                raise ValueError("unknown verdict %r in JSON slice %r" % (verdict, s.get("name")))
            slices.append({
                "name": s["name"],
                "body": bytes.fromhex(s["body"]),
                "verdict": verdict,
                "detail": s["detail"],
            })
        rows.append({
            "kcdx_id": r["kcdx_id"],
            "kind": r["kind"],
            "name": r["name"],
            "datum": r["datum"],
            "source": r["source"],
            "slices": slices,
        })
    return rows


def fixture_json_text():
    """The deterministic JSON text of the fixture (sorted keys, 2-space indent, trailing
    newline). The single rendering used by both the committed .json file and the embedded
    C++ header, so the two can never disagree about the bytes."""
    import json
    return json.dumps(fixture_to_json_obj(), indent=2, sort_keys=True) + "\n"


def dump_fixture_json(path):
    """Write the lossless fixture JSON to `path` (the committed cross-language contract
    the C++ engine-agreement consumer reads). Deterministic output (sorted keys,
    2-space indent, trailing newline) so a re-emit produces a byte-identical file unless
    the fixture changed -- a git diff then shows EXACTLY what changed in the contract."""
    import os
    obj = fixture_to_json_obj()
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(fixture_json_text())
    return obj


def fixture_header_text():
    """Render the C++ engine-embedded header carrying the fixture JSON as a raw string
    literal. The C++ agreement consumer (the cap-NN in-engine self-test) includes this
    header + parses the embedded JSON with its compact reader, so it runs over the EXACT
    bytes the Python source-of-truth pins -- no runtime file I/O, no deploy step, no path
    resolution. The JSON is emitted as a C++ raw string literal (R"FIXJSON(...)FIXJSON")
    so the embedded text needs no per-character escaping; the delimiter is chosen to never
    occur in the JSON. GENERATED -- do not hand-edit; re-run the Python emitter."""
    text = fixture_json_text()
    delim = "FIXJSON"
    assert (")" + delim + '"') not in text, \
        "the raw-string delimiter %r collides with the JSON content" % (delim,)
    lines = [
        "#pragma once",
        "",
        "// GENERATED FILE -- DO NOT EDIT. The cross-implementation per-kind survival",
        "// fixture (TRD D27), emitted from the Python source-of-truth",
        "// data/refdata-extractor/python/seeds_shared/cross_impl_fixture.py (FIXTURE_ROWS)",
        "// via fixture_header_text(). Regenerate by running",
        "// `python tests/test_cross_impl_fixture_json.py` (it re-emits this header + the",
        "// committed .json). The cap-NN engine agreement self-test embeds this JSON, plants",
        "// each slice in a synthetic PE, runs the REAL engine static check, and asserts the",
        "// engine verdict == the pinned verdict (== the Python == the JS).",
        "//",
        "// The JSON is the lossless contract: every FIXTURE_ROWS row + slice + verdict",
        "// round-trips (a Python round-trip test pins it). A drift between this header and",
        "// the fixture is caught by test_cross_impl_fixture_json.py (header-current check).",
        "",
        "namespace kcdx::survival_agreement_fixture {",
        "",
        "// The fixture JSON, verbatim from the Python emitter (raw string -- no escaping).",
        'inline constexpr const char* kFixtureJson = R"' + delim + "(",
        text.rstrip("\n"),
        ")" + delim + '";',
        "",
        "}  // namespace kcdx::survival_agreement_fixture",
        "",
    ]
    return "\n".join(lines)


def dump_fixture_header(path):
    """Write the C++ engine-embedded fixture header to `path`. GENERATED; the round-trip
    test re-emits it so a fixture change never leaves the embedded contract stale."""
    import os
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "w", encoding="utf-8", newline="\n") as f:
        f.write(fixture_header_text())
