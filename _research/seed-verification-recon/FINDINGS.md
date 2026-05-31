# Seed verification recon — Address Library seeds vs WHGame.dll

**Date:** 2026-05-31. **Binary:** `third-party-ghidra/WHGame.dll` (KCD2 release_1_5_1164953_841, image base 0x180000000). **Scope:** all 143 rows of `data/seeds/address_versions_seed.csv` + `address_names_seed.csv`. **READ-ONLY** verification — no seed edited, no commit. Produces the verdict report another flow acts on.

## Method (reuse-first ladder)

- **Tier 5 (scripted static)** — `verify_seeds.py`: load WHGame.dll once via pefile, check every row mechanically: RVA in-range + correct section; function-entry prologue plausibility; callsite/instruction AOB byte-match at RVA (+ .text uniqueness where asserted); string_anchor literal; data_slot derivation re-run; vtable_base contiguous-code-pointer slot count. Output: `_verify_out.txt`.
- `check_unusual.py`: disassembled the 13 rows whose first 4 bytes weren't in the prologue-hint set, to confirm each RVA is a real instruction boundary (preceded by `ret`/`int3` padding) — all confirmed legitimate entries. Output: `_unusual_out.txt`.
- **Vtable rows (ids 19–24)** — `verify_vtable_slots.py` + `verify_igame_region.py`: located the concrete CGame (IGame), CScriptSystem, CScriptTable vtables in `.rdata`, anchored on a known member (CGame::Update = seed id 2 at canonical slot 7), dumped each asserted slot's target + disassembled the body to match the named method. Predecessor headers (`_research/predecessor-sigs/muyuanjin-kcd2db/.../{IGame,IScriptSystem}.h`) used as the method-ORDER lead only (AP3).
- **ABI spot-check** — `phase6_abi_walker.py` on the production hook targets (ids 1, 2), CCryPak_FOpen (131), and the bespoke mod-loader RE (133–137); cross-checked the mod-loader register ABI against `init-cycle-recon/FINDINGS.md` (the tier-2 source the signatures came from).

## Verdict summary

| verdict | count | rows |
|---|---|---|
| CONFIRMED (mechanical fact agrees with binary) | 143 | all |
| MISMATCH | 0 | — |
| UNVERIFIABLE | 0 | — |

No seed change is owed. Every RVA resolves in-image to the correct section; every AOB matches; every derivation re-runs; every vtable slot and slot-count matches; all 6 vtable_index slots confirmed against the real vtable.

## Key confirmations

- **gEnv chain (ids 9→10→11→132, +17):** id9 `mov rcx,[rip+disp32]` @ 0x86AD99, disp32=0x40C0B08 → pConsole_ptr RVA **0x492B8A8** (=id10) → −0xA8 → gEnv **0x492B800** (=id11) → +0x50 → pCryPak **0x492B850** (=id132). Exact.
- **vtable_index ids 19–24:** CGame vtable base = RVA 0x3dc1868 (Update at slot 7 = 0x180667B24, the unique `.rdata` ref). CompleteInit[4]=`xor eax,eax;ret`; GetLongName[12]=0x52ec70 + GetName[13]=0x9ae5f0 (adjacent `const char*` getters, flag `[rcx+0x5c2]` + `[rcx+0x20]→[+0x108]` walk); GetIGameFramework[16]=0x3b70f0 (`ret 0` shared stub — same RVA as the stubbed luaopen_io id104, a KCD2 build trait, slot index correct). CScriptSystem[13]=0x71a204 `CreateTable(bool)` (calls lua_createtable, distinct from slot 12=0x71f724). CScriptTable[7]=0x71e52c SetValueAny (4-arg). The seed's "+1 inserted virtual" prose is an explanatory aside; the asserted integers all verify.
- **id108 luaL_addstring** tail-jmps to **0xB9D8E4 = id72 luaL_addlstring** — confirms the seed's JMP-scan identification AND cross-links the two rows.
- **id104 luaopen_io** = 3-byte `ret 0` (`C2 00 00`) — confirms the seed "STUBBED" note.
- **ABI:** no checked function reads entry_rsp+0x28 or beyond → none exceeds 4 register args, consistent with every seed signature (all ≤4 args). Mod-loader register ABI (id134 ctor rcx=outResult/rdx=sys/r8=modsDir, returns outResult ptr) matches `init-cycle-recon/FINDINGS.md`.

## Caveat on the ABI rung

The mechanical pass + abi_walker confirm: (a) RVA-is-real-entry for all function rows, (b) no function takes >4 args (no hidden stack args). The per-arg TYPE assertions (e.g. `cstr` vs `ptr`, `i32` vs `u32`, `f32` returns) on the ~100 Lua C-API rows were NOT each re-derived from the body — they carry `verified_by=maintainer_ghidra` and match canonical Lua 5.1 ABIs. They are accepted as previously-verified, not re-confirmed here. A row-by-row arg-type re-derivation is available via the abi_walker if a specific signature is ever doubted.
