# Probe 0.2 finding — minimal JS x86 decoder follows a RIP-relative disp32 to a real anchor target

**Kind:** durable process-output (a captured probe finding the Phase-2 step-2 decoder build reuses).
**Trust level:** primary evidence — fresh decode run against the real binary, checked against the
seed-resolved ground truth (not an interpretation).
**Date:** 2026-06-05.
**Step:** maintainer-tool verification-engine Phase 0, step 0.2 (de-risks the 2 derivation kinds —
`instruction_anchor` + `data_slot` — the hardest part of the browser checker; TRD D26 "minimal
in-browser x86 decoder, NOT a full disassembler").

## Question

Can a MINIMAL JS x86 decoder (just enough to follow a RIP-relative LEA/MOV `disp32`) correctly
re-derive a real `instruction_anchor` / `data_slot` target on the real WHGame.dll, checked against a
Ghidra-confirmed ground truth? The 4 pure-byte kinds (function/callsite hash, string_anchor presence,
vtable_base shape) need no decoder; only `instruction_anchor` + `data_slot` follow a displacement.

## Ground truth (reuse-first — the seed rows, NOT a fresh disassembly)

The anchor chain and the resolved target both came from EXISTING evidence: the seed CSVs
(`data/seeds/address_versions_seed.csv` + `address_names_seed.csv`), which ARE the maintainer's
test-of-record. No fresh Ghidra was run (reuse-first ladder, `reverse-engineering.md` — the seed row
carries the verified fact). The chain:

| id | name | kind | RVA (seed) | survival cells |
|---|---|---|---|---|
| 9 | `gEnv_pConsole_mov_instruction` | `instruction_anchor` | `0x0086AD99` | `survival_aob = 48 8B 0D ?? ?? ?? ??`, `survival_derives_from = 12`, `survival_expect_unique = 1` |
| 10 | `gEnv_pConsole` | `data_slot` | `0x0492B8A8` | `survival_derives_from = 9`, `survival_rule = disp32@9` |

- id 9 is the `mov rcx, [rip+disp32]` instruction; its RVA is the MOV site.
- id 10 is the `.data` pointer slot the MOV loads; its RVA is the **answer** — the resolved target.
- id 10's `survival_rule = disp32@9` + `survival_derives_from = 9` is the derivation under test:
  "follow the RIP-relative disp32 at the instruction id 9 names." id 9's notes column gives the exact
  arithmetic the production resolver uses: `pConsole_ptr_VA = MOV_VA + 7 + disp32(MOV+3..6)`.

So the clean (anchor-instruction RVA → disp32 → target RVA) triple to check against is:
**`0x0086AD99` → disp32 → `0x0492B8A8`**. Expected disp32 = `0x0492B8A8 − (0x0086AD99 + 7)` =
`0x040C0B08`.

## The minimal decoder (what the probe built)

At a given instruction RVA, decode just enough to follow a RIP-relative LEA/MOV `disp32`:

- **Encodings handled** (exactly the two derivation kinds use, per `fingerprint-per-kind.md`
  §instruction_anchor + §data_slot):
  - `LEA r64, [rip+disp32]` = `REX.W(0x48) | 0x8D | ModRM(mod=00, rm=101)` + disp32
  - `MOV r64, [rip+disp32]` = `REX.W(0x48) | 0x8B | ModRM(mod=00, rm=101)` + disp32
  - Both are 7 bytes: REX(1) + opcode(1) + ModRM(1) + disp32(4).
- **The follow logic:** read disp32 as a **little-endian SIGNED i32** (bytes MOV+3..6), then
  `target = next_instruction_RVA + disp32 = (rva + 7) + disp32` — RIP-relative is relative to the END
  of the instruction, hence `+7`.
- **RVA→file-offset:** reuse the PE section-table parse from `versionResolver.ts` `parsePe`
  (the same parse step 0.1 reused), generalized to map any `.text` RVA to a file offset:
  `fileOffset = rva − section.virtualAddress + section.rawDataOffset`.
- **Out of scope (deliberately):** any encoding that is NOT `0x48` REX.W + `0x8B`/`0x8D` +
  RIP-relative ModRM is reported as "unhandled" rather than guessed — that report IS the ROW-2
  signal (the minimal scope being too narrow). The minimal decoder does not parse the full
  ModRM/SIB/prefix space; it does exactly the displacement-follow the 2 kinds need.

## Result — DECODED target RVA vs ground truth

Run against the real `third-party-ghidra/WHGame.dll` (89,176,576 bytes):

| Fact | Value |
|---|---|
| anchor RVA (id 9) | `0x0086AD99` |
| file offset of anchor | `0x0086A199` |
| **raw 7 bytes at anchor** | **`48 8b 0d 08 0b 0c 04`** |
| decoded | `mov` reg=1 (rcx), RIP-relative |
| decoded disp32 | `0x040C0B08` (67898120) |
| expected disp32 | `0x040C0B08` (67898120) |
| **DECODED target RVA** | **`0x0492B8A8`** |
| **GROUND-TRUTH target (id 10)** | **`0x0492B8A8`** |
| match | **EXACT** |

The raw bytes `48 8b 0d ...` are precisely the `mov rcx, [rip+disp32]` the seed's `survival_aob`
(`48 8B 0D ?? ?? ?? ??`) predicted — opcode `48 8B` (REX.W MOV r64), ModRM `0D` (mod=00, reg=001=rcx,
rm=101=RIP-relative). The decoder followed the disp32 and landed EXACTLY on the data_slot id 10 RVA.

## Outcome→map verdict

| Outcome (pre-committed, flat) | Lands? |
|---|---|
| Decoded `disp32` follow == the Ghidra/seed-confirmed target → **minimal decoder suffices** | **ROW 1 — YES.** |
| Lands wrong / needs more instruction forms than minimal LEA/MOV → scope too narrow | No. |
| The anchor chain itself is mis-modeled → STOP, re-verify the chain | No. |

**ROW 1 — the minimal decoder suffices for the derivation kinds.** A 7-byte REX.W + `0x8B`/`0x8D` +
RIP-relative-ModRM decode, following a signed little-endian disp32 to `(rva + 7) + disp32`,
re-derived the real `instruction_anchor`→`data_slot` target on the real binary with an exact match.
**Phase 2 step 2 builds the production decoder to this minimal scope** (no full disassembler needed).

## The exact disp32-follow recipe that worked (for the Phase-2 build)

```
1. RVA → file offset: parse the PE section table (reuse versionResolver.parsePe's offsets), find
   the section the anchor RVA falls in, fileOffset = rva - section.virtualAddress + section.rawDataOffset.
2. At fileOffset, read 3 opcode bytes:
     byte0 (REX)    must be 0x48        (REX.W — 64-bit operand; minimal scope)
     byte1 (opcode) 0x8D = LEA | 0x8B = MOV
     byte2 (ModRM)  mod = (b>>6)&3 must be 0; rm = b&7 must be 5  (RIP-relative disp32)
3. disp32 = signed little-endian int32 at fileOffset+3 (bytes 3..6).
4. targetRva = rva + 7 + disp32.   (+7 = instruction length; RIP is relative to the NEXT instruction)
```

The instruction length is fixed at 7 for these two encodings (REX + opcode + ModRM=mod00/rm101 +
4-byte disp, no SIB, no immediate). data_slot derivation (`disp32@<kid>`) = run step 2-4 at the
anchor instruction the `<kid>` names; the `<kid>-0x<hex>` / `<kid>+0x<hex>` rule form (e.g. id 11
`gEnv = id 10 − 0xA8`) is a pure RVA arithmetic on another slot, no decode needed.

## Caveats Phase 2 should carry forward (sizing notes, not blockers)

- **The minimal scope is REX.W `0x48` only.** This anchor's MOV destination is rcx (`reg=001`,
  within REX.W's range). A future anchor whose instruction loads r8–r15 would use `REX.WR` (`0x4C`)
  or `REX.WB` (`0x49`), which this minimal decoder reports as "unhandled" rather than decoding — by
  design, so a wider encoding surfaces as ROW 2 (a widened-scope DECISION for the user) rather than a
  silently-wrong follow. Phase 2 widens the REX handling only if/when a real seed anchor needs it.
- **id 9 also carries a backward-walk step in its notes** (the production resolver checks 7 bytes
  PRECEDING a matched LEA to choose MOV-at-LEA-0x17 vs LEA-7 across game layouts). This probe checked
  the FORWARD disp32-follow at the already-resolved MOV RVA — the core of the derivation. The
  backward LEA→MOV walk (the re-FIND of the anchor from the string_anchor at a fresh game version) is
  a separate re-find concern Phase 2 builds on top; this probe proves the displacement-follow LANDS
  RIGHT, which is the load-bearing decoder primitive.

## Reproduce

From inside `data/maintainer-tool/frontend/` (the spaced-path Vitest requirement, see its
`TESTING.md`):

```
npx vitest run src/dll-resolver/__probes__/x86-decoder-anchor.probe.test.ts --disableConsoleIntercept
```

`--disableConsoleIntercept` surfaces the `console.log` block carrying the decode + verdict. The test
SKIPS (still green) when the DLL is absent.

## Harness state

The throwaway harness stays in-tree as a portable named probe at
`data/maintainer-tool/frontend/src/dll-resolver/__probes__/x86-decoder-anchor.probe.test.ts`,
`it.skipIf(!existsSync(DLL))` so it is a no-op when the DLL is absent (mirrors the accepted 0.1
probe). It is clean (skips DLL-absent, asserts the load-bearing facts when present), NOT dead code in
a production file — it lives in the `__probes__/` tree, never imported by production code.
