# Survival-seed RE pass (step 5.2) — findings

Verifies, against `WHGame.dll` (KCD2 `release_1_5_1164953_841`, in-repo copy
`third-party-ghidra/WHGame.dll`, image base `0x180000000`), the per-kind
survival datum for each non-function curated Address Library entity named in the
step-5.2 brief. Static only (`pefile` + `capstone`); no live game run.

Method: reuse ladder. Tier 1 = the seed `notes` prose (re-verified, never
trusted). Tier 2 = prior `_research/` dumps — the gEnv resolver
(`phase7-recon/find_genv.py`, `extend_genv_sig.py`), the pak resolver gEnv+0x50
fact (`phase8.5-pak-resolver/FINDINGS.md`), the I_Mod / C_ModManager vtables
(`init-cycle-recon/FINDINGS.md`). Tier 5 (fresh capstone) only to re-derive the
exact bytes and counts and assert uniqueness.

Scripts + raw output in this dir:
- `verify_survival_fields.py` / `_survival_out.txt` — all clusters, first pass.
- `verify_callsites_and_imod.py` / `_callsites_imod_out.txt` — tightened ids
  5/6 (prose spans) + ids 138/139 (reloc-aware vtable-boundary).

---

## callsite — AOB pattern + .text-uniqueness (ids 5, 6, 7, 8)

All four AOBs match at their stored RVA AND are **.text-unique** (exactly one
hit) at this game version. Survival datum = the AOB bytes (no wildcards needed —
none of these spans contains a RIP-relative disp32).

| id | RVA | AOB (survival datum) | bytes | .text hits |
|---|---|---|---|---|
| 5 | 0x0056174C | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | 16 | 1 (UNIQUE) |
| 6 | 0x00561745 | `48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | 23 | 1 (UNIQUE) |
| 7 | 0x005605BC | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | 26 | 1 (UNIQUE) |
| 8 | 0x00566040 | `48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01` | 30 | 1 (UNIQUE) |

- **id 5** (outfit_swap_callsite_aob): the seed prose's 16-byte canonical AOB is
  the span `add rcx,0xb60; mov rax,[rcx]; call [rax+8]; mov r14b,al; …`. Offset
  +13 is `44 8A F0` = `mov r14b, al` (the byte the patch flips to
  `xor r14d,r14d`), matching the prose. The 16-byte span is already .text-unique
  on its own — id 6's upward extension is belt-and-suspenders, not a fix for an
  ambiguous id 5.
- **id 6** (outfit_swap_callsite_context): id 5's site extended 7 bytes upward to
  start at `mov rcx, [rax+0x90]`, exactly as the prose says ("extended 7 bytes
  upward to include the call-into-IsInCombat sequence"). 23 bytes, also unique.
- **ids 7, 8**: the prose hex matches the binary verbatim. id 7 ends `3C 02`
  (`cmp al, 2`), id 8 ends `3C 01` (`cmp al, 1`) — two distinct call sites to the
  same IsInCombat vtable slot with different combat-state thresholds, exactly as
  the prose documents. Both unique.

No ambiguity at this version → no `expect_extend` flag needed; the callsite
ambiguity posture (design doc open-decision #3) is not exercised here.

## string_anchor — literal presence + unique LEA xref (id 12)

`"exec autoexec.cfg"` is present in `.rdata` at RVA `0x04095E58` (VA
`0x184095E58`), occurs exactly **once** in `.rdata`, and has exactly **one**
`.text` `LEA r64,[rip+disp32]` xref — `lea rdx, [rip->str]` at RVA `0x0086ADB0`.

- Literal bytes (with terminating NUL): `65 78 65 63 20 61 75 74 6F 65 78 65 63 2E 63 66 67 00`.
- Survival datum = the string bytes; `expect_unique_xref = true` HOLDS (one LEA
  xref). This is the load-bearing property the gEnv resolver chain (id 9) relies
  on — confirmed.

## instruction_anchor — resolver-chain re-derivation (id 9)

The 3-step resolver in the seed prose re-derives cleanly:

1. string anchor (id 12) found in `.rdata` at `0x04095E58`.
2. exactly one `48 8D 15 ?? ?? ?? ??` LEA (`lea rdx,[rip+str]`) references it, at
   RVA `0x0086ADB0`; the 7 bytes preceding it are `4C 8B 92 18 01 00 00` (the
   V1.4+ context `mov r10, [rdx+0x118]`).
3. V1.4+ layout → the pConsole MOV is at LEA − 0x17 = RVA **`0x0086AD99`**
   (matches the seed-stored id 9 RVA exactly).

- MOV bytes at `0x86AD99`: `48 8B 0D 08 0B 0C 04` = `mov rcx, [rip+0x40C0B08]`.
- **Expected instruction shape (survival datum)**: `48 8B 0D ?? ?? ?? ??`
  (`mov rcx, [rip+disp32]`; disp32 wildcarded — it relocates per build). The
  resolved site matches this shape.
- `derives_from` = id 12 (string_anchor).
- Chain tail (feeds the data_slots below): pConsole ptr slot =
  MOV_VA + 7 + disp32 = RVA `0x0492B8A8` (== seed id 10); gEnv = that − 0xA8 =
  RVA `0x0492B800` (== seed id 11). Both match.

## data_slot — derivation rule + derives_from (ids 10, 11, 132)

No content hash (per the design — `.data` slots hold relocated/runtime pointer
values). Survival datum = the derivation rule + its anchor row.

| id | name | derivation rule (survival datum) | derives_from | verified |
|---|---|---|---|---|
| 10 | gEnv_pConsole | follow disp32 from the MOV at instruction_anchor: `slot = MOV_VA + 7 + disp32` | id 9 | → RVA 0x0492B8A8 ✓ |
| 11 | gEnv | `id 10 RVA − 0xA8` | id 10 | 0x0492B8A8 − 0xA8 = 0x0492B800 ✓ |
| 132 | gEnv_pCryPak | `id 11 (gEnv) RVA + 0x50` | id 11 | 0x0492B800 + 0x50 = 0x0492B850 ✓ |

The id 132 offset (+0x50) is the binary-confirmed pCryPak field from
`phase8.5-pak-resolver/FINDINGS.md` (DAT_18492b850, the global 350+ functions
deref to call FOpen, == gEnv+0x50).

## vtable_base — contiguous .text-pointer slot count (ids 119, 138, 139, 140)

Slot count = the number of contiguous reloc'd `.text`-range qwords starting at
the stored vtable RVA, bounded by the next non-`.text` qword (an RTTI
completeObjectLocator pointer into `.rdata`, or string data — a clean MSVC table
boundary, NOT garbage). Each vtable's qwords are base-relocation entries
(`IMAGE_REL_BASED_DIR64`), so the on-disk values are already image-based and the
`.text`-range test is valid.

| id | name | RVA | slot count (survival datum) | boundary qword | prose said |
|---|---|---|---|---|---|
| 119 | CScriptSystem_vtable | 0x03B8AF70 | **69** | 0x1840BB8D0 (.rdata, next COL) | 69 ✓ |
| 138 | ImodVtable_primary | 0x046AAF00 | **3** | 0x184186670 (.rdata COL) | (n/a) |
| 139 | ImodVtable_subobject | 0x046AAED8 | **4** | 0x1841865F8 (.rdata COL) | (n/a) |
| 140 | C_ModManager_vtable | 0x03AA2E60 | **18** | 0x6D726F6674616C50 ("Platform") | **8 (WRONG)** |

- **id 119**: 69 slots, matching the prose exactly. Slot[6] = RVA 0x4D46E4
  (ExecuteBuffer), slots [12]/[13] both → 0x71F098 (lua_createtable) — consistent
  with the prose's slot semantics.
- **ids 138 + 139** sit back-to-back in `.rdata`, each preceded by its own RTTI
  COL pointer: `COL@0x46AAED0 → vtable139 (4 slots) → COL@0x46AAEF8 →
  vtable138 (3 slots)`. The qword after each table's last method is a `.rdata`
  COL pointer (verified section = `.rdata`), the canonical boundary.
- **id 140 — slot count is 18, NOT 8.** The init-cycle probe and the seed prose
  both say "8 function pointers"; that was a *runtime* probe reading only the
  first 8 speculatively, not a structural boundary read. The static structure
  shows **18** contiguous reloc'd `.text` pointers before the boundary qword
  `0x6D726F6674616C50` (ASCII "Platform…", a string — a clean non-pointer
  boundary). Survival datum = **18**. *The seed `notes` for id 140 and the
  init-cycle FINDINGS "8 function pointers" claim should be corrected to 18 when
  next touched (out of scope for this data-fill pass — a notes UPDATE).*

## vtable_index — DEFERRED (ids 19–24)

Not filled. Per the design (`fingerprint-per-kind.md` §vtable_index) and the
brief, these resolve at runtime to `vtable_base[index]` and their survival datum
(base-ref + index + expected-slot-target body hash) is only populatable once the
runtime-vtable verification path gives the slot a verified target. The seed rows
for ids 19–24 are already empty (rva/signature/audit all blank). Survival cells
left EMPTY — an empty-payload survival row is the correct output for an unfilled
column.

---

## Provenance one-liners (paste-ready, private-citation-free)

For when these land in the seed (a later reviewed edit — NOT this pass):

- callsite 5/6/7/8: `AOB .text-unique against the binary (1 hit), verified by capstone scan.`
- string_anchor 12: `present once in .rdata; exactly one .text LEA(rip) xref — unique-anchor property holds, capstone-verified.`
- instruction_anchor 9: `resolver chain re-derives to RVA 0x86AD99; final instruction shape mov rcx,[rip+disp32], capstone-verified.`
- data_slot 10/11/132: `derivation re-runs to the stored RVA, capstone-verified (disp-follow / fixed offset).`
- vtable_base 119/138/139/140: `contiguous .text-pointer slot count read from the table in .rdata, bounded by the next RTTI object — capstone-verified.`
