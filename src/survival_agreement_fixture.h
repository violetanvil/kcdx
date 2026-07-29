#pragma once

// GENERATED FILE -- DO NOT EDIT. The cross-implementation per-kind survival
// fixture (TRD D27), emitted from the Python source-of-truth
// data/refdata-extractor/python/seeds_shared/cross_impl_fixture.py (FIXTURE_ROWS)
// via fixture_header_text(). Regenerate by running
// `python tests/test_cross_impl_fixture_json.py` (it re-emits this header + the
// committed .json). The cap-NN engine agreement self-test embeds this JSON, plants
// each slice in a synthetic PE, runs the REAL engine static check, and asserts the
// engine verdict == the pinned verdict (== the Python == the JS).
//
// The JSON is the lossless contract: every FIXTURE_ROWS row + slice + verdict
// round-trips (a Python round-trip test pins it). A drift between this header and
// the fixture is caught by test_cross_impl_fixture_json.py (header-current check).

namespace kcdx::survival_agreement_fixture {

// The fixture JSON, verbatim from the Python emitter (raw string -- no escaping).
inline constexpr const char* kFixtureJson = R"FIXJSON(
{
  "format_version": 1,
  "in_scope_kinds": [
    "function",
    "callsite",
    "string_anchor",
    "instruction_anchor",
    "data_slot",
    "vtable_base"
  ],
  "rows": [
    {
      "datum": {
        "content_hash": "369a169b998ceea094cebe3f3d4e8206a5b227906306d071c2f9eb8e78dc0b88",
        "length": 38
      },
      "kcdx_id": 999001,
      "kind": "function",
      "name": "synthetic_body_hash_fixture",
      "slices": [
        {
          "body": "48895c2408574883ec20488bf933db4885c9740ae8112233448bd885c07505488bcfff15aabb",
          "detail": "fingerprint-per-kind.md function body-hash: re-hash [rva,rva+length) and compare to content_hash. BLAKE3(body) == the recorded content_hash -> Unchanged. FALSIFIES if a checker's BLAKE3 of this span != the stored hash.",
          "name": "unchanged",
          "verdict": "Unchanged"
        },
        {
          "body": "48895c2408574883ec20488bf933db4885c9740ae8112233448bd885c07505488bcfff15aabc",
          "detail": "Same span with the final byte flipped (0xbb->0xbc): BLAKE3 differs from the recorded content_hash -> Changed. FALSIFIES if a flipped body still hashes equal (a broken hash) or is reported Unchanged.",
          "name": "changed_byte_flip",
          "verdict": "Changed"
        }
      ],
      "source": "Documented synthetic 38-byte body span (length mirrors the real mini-dump function row rva=0x1020 length=38); content_hash is the genuine BLAKE3 of these exact bytes via the repo's BLAKE3 oracle (data/refdata-extractor/ghidra/blake3/Blake3Hex.java)."
    },
    {
      "datum": {
        "aob": "48 8b 41 08 48 8b 88 90 00 00 00 48 81 c1 60 0b 00 00 48 8b 01 ff 50 08 3c 02",
        "expect_unique": 1
      },
      "kcdx_id": 7,
      "kind": "callsite",
      "name": "IsInCombat_callsite_26b",
      "slices": [
        {
          "body": "90909090cccccccc488b4108488b88900000004881c1600b0000488b01ff50083c029090",
          "detail": "fingerprint-per-kind.md callsite AOB re-match: the AOB occurs exactly ONCE in the `.text`-like block -> Unchanged. FALSIFIES if a scan finds 0 or >1.",
          "name": "unchanged_unique_hit",
          "verdict": "Unchanged"
        },
        {
          "body": "90909090cccccccc9090",
          "detail": "Block without the AOB anywhere: zero hits -> Changed (the site is gone). FALSIFIES if a scan reports a phantom hit.",
          "name": "changed_zero_hits",
          "verdict": "Changed"
        },
        {
          "body": "90909090cccccccc488b4108488b88900000004881c1600b0000488b01ff50083c029090488b4108488b88900000004881c1600b0000488b01ff50083c029090",
          "detail": "Block with the AOB twice: multiple hits -> Ambiguous (the pattern is no longer a unique locator). FALSIFIES if a scan collapses two hits to Unchanged.",
          "name": "ambiguous_two_hits",
          "verdict": "Ambiguous"
        }
      ],
      "source": "Real seed row id 7 (address_versions_seed.csv) -- survival_aob '48 8B 41 08 ... 3C 02' (26 bytes), survival_expect_unique=1, evidence_kind=live_test_plugin."
    },
    {
      "datum": {
        "anchor_string": "exec autoexec.cfg",
        "expect_unique": 1
      },
      "kcdx_id": 12,
      "kind": "string_anchor",
      "name": "string_exec_autoexec_cfg",
      "slices": [
        {
          "body": "0000000065786563206175746f657865632e636667004f74686572537472696e6700",
          "detail": "fingerprint-per-kind.md string_anchor literal presence: the literal is present in the `.rdata`-like block -> Unchanged. FALSIFIES if a search misses a present literal.",
          "name": "unchanged_present",
          "verdict": "Unchanged"
        },
        {
          "body": "0000000000756e72656c617465642e63666700004f74686572537472696e6700",
          "detail": "Block WITHOUT the literal: absent -> Changed (the anchor is gone, every row re-deriving from it is now unresolvable). FALSIFIES if a search reports a phantom present.",
          "name": "changed_absent",
          "verdict": "Changed"
        }
      ],
      "source": "Real seed row id 12 (address_versions_seed.csv) -- survival_anchor_string 'exec autoexec.cfg', survival_expect_unique=1, evidence_kind=maintainer_ghidra."
    },
    {
      "datum": {
        "anchor_rva": 8826265,
        "aob": "48 8B 0D ?? ?? ?? ??",
        "expect_unique": 1,
        "expected_target_rva": 76724392,
        "rule": "disp32@9"
      },
      "kcdx_id": 9,
      "kind": "instruction_anchor",
      "name": "gEnv_pConsole_mov_instruction",
      "slices": [
        {
          "body": "488b0d080b0c04",
          "detail": "fingerprint-per-kind.md instruction_anchor derivation: decode the 7-byte MOV `48 8B 0D` + disp32 and follow it -- target = (0x0086AD99 + 7) + 0x040C0B08 = 0x0492B8A8 (the expected target, id 10). The shape matches the stored AOB '48 8B 0D ?? ?? ?? ??' and the derivation lands on the expected RVA -> Unchanged. FALSIFIES if the disp32-follow lands != 0x0492B8A8 or the shape does not match.",
          "name": "unchanged_lands_on_target",
          "verdict": "Unchanged"
        },
        {
          "body": "48ff0d080b0c04",
          "detail": "Anchor with a non-MOV/LEA opcode byte (0x48 0xff ...): the instruction shape no longer matches the stored '48 8B 0D ...' AOB shape, so the derivation chain cannot complete on the expected shape -> Changed. FALSIFIES if a checker decodes a non-RIP-relative-MOV as the anchor.",
          "name": "changed_wrong_shape",
          "verdict": "Changed"
        }
      ],
      "source": "Real reference row id 9 (survival_aob '48 8B 0D ?? ?? ?? ??') + x86-decoder verification: anchor RVA 0x0086AD99, raw 7 bytes '48 8b 0d 08 0b 0c 04', decoded disp32 0x040C0B08, derived target RVA 0x0492B8A8 == id 10 ground truth (EXACT match)."
    },
    {
      "datum": {
        "anchor_rva": 8826265,
        "derives_from": 9,
        "expected_slot_rva": 76724392,
        "rule": "disp32@9"
      },
      "kcdx_id": 10,
      "kind": "data_slot",
      "name": "gEnv_pConsole",
      "slices": [
        {
          "body": "488b0d080b0c04",
          "detail": "fingerprint-per-kind.md data_slot derivation: re-run rule 'disp32@9' over the anchor (id 9) bytes -- (0x0086AD99 + 7) + 0x040C0B08 = 0x0492B8A8, the recorded slot RVA -> Unchanged. FALSIFIES if the derivation reaches a different offset or a byte hash of the slot is used instead.",
          "name": "unchanged_derivation_reaches_slot",
          "verdict": "Unchanged"
        },
        {
          "body": "488b0d180b0c04",
          "detail": "Anchor with disp32 altered by +0x10 (0x040C0B18): the follow lands at 0x0492B8B8, NOT the recorded 0x0492B8A8 -> Changed (the derivation no longer reaches the verified slot). FALSIFIES if a checker ignores the displacement and reports Unchanged.",
          "name": "changed_derivation_lands_wrong",
          "verdict": "Changed"
        }
      ],
      "source": "Real seed row id 10 (survival_rule 'disp32@9', survival_derives_from 9) + probe 0.2 finding: following the disp32 at id 9 (0x0086AD99) lands EXACTLY on id 10's slot RVA 0x0492B8A8 (anchor-instruction RVA -> disp32 -> target RVA, EXACT). No content hash -- a data_slot's survival is its derivation."
    },
    {
      "datum": {
        "slot_count": 3,
        "text_range": [
          4096,
          60825114
        ]
      },
      "kcdx_id": 138,
      "kind": "vtable_base",
      "name": "vtable_base_slot3_fixture",
      "slices": [
        {
          "body": "a4a5710000000000247b6600000000009838990300000000",
          "detail": "fingerprint-per-kind.md vtable_base table-shape: read N=3 qwords; each is a real id-138 `.text` RVA (within [0x1000,0x3A01E1A)) -> Unchanged. FALSIFIES if a qword outside `.text` is accepted or the count check is skipped.",
          "name": "unchanged_n_valid_pointers",
          "verdict": "Unchanged"
        },
        {
          "body": "a4a5710000000000247b6600000000000000000000000000",
          "detail": "Third qword is 0x0 (not a `.text` pointer): the N-qword all-resolve-into-`.text` shape no longer holds -> Changed. FALSIFIES if a non-pointer slot is treated as valid.",
          "name": "changed_non_pointer_slot",
          "verdict": "Changed"
        }
      ],
      "source": "Real seed row id 138 (kind=vtable_base, survival_slot_count=3). The qword values are real id-138 `.text` RVAs that resolve into the real WHGame.dll .text window [0x1000, 0x3A01E1A) (vtable pointers relocate per build and are NOT hashed -- the survival check is the SHAPE: N qwords each resolving into `.text`)."
    },
    {
      "datum": {
        "deferred": true,
        "vtable_slot": 4
      },
      "kcdx_id": 19,
      "kind": "vtable_index",
      "name": "IGame_CompleteInit_vtable_idx",
      "slices": [
        {
          "body": "",
          "detail": "fingerprint-per-kind.md vtable_index is DEFERRED: no RVA, the slot target needs a runtime resolve -> CannotCheck. FALSIFIES if any implementation returns Unchanged/Changed for a vtable_index from on-disk bytes alone.",
          "name": "cannot_check_no_static_target",
          "verdict": "CannotCheck"
        }
      ],
      "source": "Real seed row id 19 (kind=vtable_index, no rva, vtable_slot=4; status unverified). fingerprint-per-kind.md marks vtable_index 'deferred within this design' -- its survival datum (resolve base, take slot, hash target body) needs the runtime-vtable verification path."
    }
  ],
  "verdicts": [
    "Ambiguous",
    "CannotCheck",
    "Changed",
    "Unchanged"
  ]
}
)FIXJSON";

}  // namespace kcdx::survival_agreement_fixture
