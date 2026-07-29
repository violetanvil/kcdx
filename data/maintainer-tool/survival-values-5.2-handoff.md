# Step 5.2 — verified survival values (handoff to step 5.1's columns)

**Dependency: this is a HANDOFF, not a seed edit.** Step 5.1 (the survival
column SHAPE in `address_versions_seed.csv` + the format law in
`data/seeds/policy.md`) had NOT landed when this RE pass ran — the
`address_versions_seed.csv` header still carries only
`kcdx_id,valid_from_version,module,rva,signature,last_verified_at_version,verified_by,verified_date,evidence_kind`,
with no `survival_*` columns. So the verified values below are recorded keyed by
the design doc's per-kind datum names (`data/maintainer-tool/fingerprint-per-kind.md`),
NOT written into invented columns. **Once 5.1's columns land, slot these in as
seed UPDATEs to the existing (already-approved) rows** — one update per
`(kcdx_id, datum)` below. No new entities, no new version rows → AP18's
new-row-approval gate does not apply.

All values verified against `WHGame.dll` (KCD2 `release_1_5_1164953_841`,
`WHGame.dll`, image base `0x180000000`). Evidence: the field offsets, callsites, and vtable
were verified by scripted disassembly against that binary.
Every value is binary-verified (AP1/AP2/AP3) — no prose-recall, no header-guess.

---

## callsite — `aob_pattern` (bytes+mask) + uniqueness

`kind_form = aob`. Survival datum = the AOB pattern the resolver re-matches.
All four are `.text`-unique at this version; none needs a `?` wildcard (no
RIP-relative disp32 in the span). `derives_from` = (none — callsites are
self-locating).

| kcdx_id | datum: `aob_pattern` | datum: `expect_unique` |
|---|---|---|
| 5 | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | true (1 hit @ 0x0056174C) |
| 6 | `48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | true (1 hit @ 0x00561745) |
| 7 | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | true (1 hit @ 0x005605BC) |
| 8 | `48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01` | true (1 hit @ 0x00566040) |

## string_anchor — `string_bytes` + `expect_unique_xref`

`kind_form = function_hash`-sibling (literal-presence). `derives_from` = (none —
this is the seed anchor others derive from).

| kcdx_id | datum: `string_bytes` (incl. NUL) | datum: `expect_unique_xref` |
|---|---|---|
| 12 | `65 78 65 63 20 61 75 74 6F 65 78 65 63 2E 63 66 67 00` (`"exec autoexec.cfg"`) | true (1 .text LEA xref @ 0x0086ADB0; 1 .rdata occurrence) |

## instruction_anchor — `instr_shape` + `derives_from`

`kind_form = derivation`. Survival datum = the expected instruction-shape
pattern at the resolved site.

| kcdx_id | datum: `instr_shape` | datum: `derives_from` |
|---|---|---|
| 9 | `48 8B 0D ?? ?? ?? ??` (`mov rcx, [rip+disp32]`; disp32 wildcarded) | 12 (string_anchor) |

Resolver re-derivation (the procedure the survival check re-runs): find id-12
string → its single `48 8D 15` LEA xref (@0x86ADB0, V1.4+ context
`4C 8B 92 18 01 00 00`) → MOV at LEA−0x17 = RVA 0x0086AD99 (matches stored id-9
RVA) → assert the shape above.

## data_slot — `derivation_rule` + `derives_from` (NO content hash)

`kind_form = derivation`. Survival datum = the offset/disp-follow rule; there is
deliberately no byte hash (`.data` pointer contents legitimately vary).

| kcdx_id | datum: `derivation_rule` | datum: `derives_from` |
|---|---|---|
| 10 | follow disp32 from the anchor MOV: `slot_rva = MOV_VA + 7 + disp32` | 9 (instruction_anchor) |
| 11 | `id-10 RVA − 0xA8` | 10 (gEnv_pConsole) |
| 132 | `id-11 (gEnv) RVA + 0x50` | 11 (gEnv) |

Re-derivation lands on the stored RVAs: id 10 → 0x0492B8A8; id 11 → 0x0492B800;
id 132 → 0x0492B850. All match.

## vtable_base — `slot_count` (structural)

`kind_form = table_shape`. Survival datum = the expected contiguous
`.text`-pointer slot count, bounded by the next RTTI object / string. No table
byte hash (slot pointers relocate per build). `derives_from` = (none).

| kcdx_id | datum: `slot_count` | boundary (verified non-method qword) |
|---|---|---|
| 119 | 69 | 0x1840BB8D0 (.rdata, next COL) |
| 138 | 3 | 0x184186670 (.rdata COL) |
| 139 | 4 | 0x1841865F8 (.rdata COL) |
| 140 | 18 | 0x6D726F6674616C50 (ASCII "Platform…", string boundary) |

**id 140 notes discrepancy — RESOLVED.** The id-140 seed `notes` formerly said
"8 function pointers"; the binary structural count is **18** (18 contiguous
reloc'd `.text` qwords before the "Platform" string boundary; the "8" was a
runtime probe that read only the first 8 speculatively). Both the survival
`slot_count` (= 18) and the id-140 `notes` prose now state 18.
(Older recon notes may still carry the legacy "8" — the seed is the
authoritative value.)

## vtable_index — DEFERRED (ids 19–24)

Left EMPTY. Survival datum (`kind_form = slot_target`: base-ref + index +
expected-slot-target body hash) is gated on the runtime-vtable verification path
that gives each slot a verified target. The seed rows are already fully empty
(rva/signature/audit blank). 5.1's machinery emits an empty-payload survival row
for an unfilled column — the correct output. No guess.

---

## Summary — what filled, what's empty

- **Filled (binary-verified):** 5, 6, 7, 8 (callsite AOB); 12 (string anchor +
  unique xref); 9 (instruction shape + derives_from 12); 10, 11, 132 (data_slot
  derivation rules + derives_from chain); 119, 138, 139, 140 (vtable_base slot
  counts). 14 rows.
- **Left empty (correctly):** 19, 20, 21, 22, 23, 24 (vtable_index — deferred on
  the runtime-vtable path). 6 rows.
- **Side finding owed as a separate notes UPDATE:** id 140 `notes` says "8";
  binary says 18.
