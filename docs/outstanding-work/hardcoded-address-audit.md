# Hard-coded Address Audit

## Summary

**Total confirmed findings: 44** (every occurrence counted separately; the same literal in N files = N rows).

By kind:

| kind | count |
|---|---|
| pattern (AOB byte signature) | 21 |
| rva (image-base-relative code/data address) | 17 |
| vtable_slot (interface slot index) | 5 |
| struct_offset (vtable byte offset) | 1 |

By db_status:

| db_status | count |
|---|---|
| already_in_db (hygiene — relit existing seed) | 27 |
| not_in_db (coverage — new seed row owed) | 17 |
| uncertain | 0 |

**Resolved since the audit — the migration sweep (4 phases, commits `a556486` / `2ce7b62` / `e266c81` / `83be074`).** Every in-scope operative locator now resolves by name through the Address Library; 6 new seed entities (ids 144–149) were added, all with binary-verified RVAs + dump-promoted `content_hash`/`length`.

- **Phase 1 — `src/save_load_hooks.cpp` (ids 144–148).** The 5 operative locators (4 `*_SIG` patterns + `SLOT_RESOLVER_SIG`) → resolve by name. **Live-verified** (the 5 resolve `verification_state=verified` and install; the load-path hooks fire in-game). The 3 comment-only RVA rows (`.cpp` :49/:55, `.h` :34) are `comment-only` (no runtime dependency).
- **Phase 2a — `src/hooks.cpp` (ids 1, 2).** `lua_pcall` + `CGame_Update` engine-bootstrap hooks: `FindUniqueSig(*_SIG)` → `refdb::ResolveAddrByName`. **Live-verified** (both resolve + install; `lua_pcall` fires, PROBE Q reads zero).
- **Phase 2b — `test-plugins/cap-03-hook-lua-callback/plugin.lua` (id 4).** Relit AOB pattern → `target = "CGame_per_frame_ui_pump"` (the name carries address + ABI). **Live-verified** (resolves by name; CAP-03 PASS).
- **Phase 3 — `src/probes/loc_dump_probe.cpp` (id 149, new entity `CLocalizedStringsManager_ctor`).** Hardcoded `kCtorRva = 0x9f0ce4` → resolve by name. **STATICALLY verified, runtime-UNEXERCISED by design:** the loc_dump probe is deliberately disarmed (`dllmain.cpp:338` commented out — its `LocalizeString` hook fires ~11.6k×/session with no remaining diagnostic purpose), so the ctor-resolution path does not execute at runtime. The resolution is correct by construction — id 149 carries `rva = 0x9f0ce4` (identical to the deleted constant), `ResolveAddrByName` = `WhgameBase() + rva`, and the identical mechanism is live-proven by ids 144–148 — but the in-game install-via-name is not exercised while the probe is off. This is a weaker (but legitimate) verification tier than Phases 1/2.

**Deliberately NOT migrated (correctly left as-is, not findings):** `cap-32-scan` (the raw AOB is the test input for `kcdx.scan{pattern=}`); `cap-33-author-targets` (the pattern row tests the by-name-pattern author-target path); the LocalizeString vtable slots 21/22 (C++ vtable *ordinals* read off the live object — not game addresses; recording them as `vtable_index` rows awaits a runtime vtable-hook consumer that does not exist); the `examples/archive/v0.1-schema/**` legacy demos + pure-prose comment RVAs (out of the chosen scope: production `src/` + active `test-plugins/`).

**Latent cleanup owed (working-artifacts).** `src/probes/loc_dump_probe.cpp` is a disarmed/commented-out probe in live source — which `working-artifacts.md` says should leave no corpse in `src/` (extract to `_research/` + remove). The Phase-3 migration curated id 149 for it; eventually loc_dump should either graduate to a real feature or be extracted-and-removed, and id 149 follows whichever happens (deprecate the entity if the probe is removed).

**By-status recount (the original snapshot's 44 occurrences, after the sweep).** The 27 `already_in_db` rows split: the operative relits in `hooks.cpp` (2), `cap-03` (1), and the `fopen_override_probe` slot/`gEnv` comment relits are migrated or comment-only; the `examples/archive/**` rows stay relit-but-out-of-scope (archived). The 17 `not_in_db` rows: 5 save/load + 1 loc-ctor are now seeded (ids 144–149); the rest (the BugSplat disproven literals, the archived `post_bracket_probe` frame-4 RVA) stay un-seeded by design (disproven / archived). The original by-kind / by-status tables above are the **audit snapshot** — kept as the historical baseline; this note is the post-sweep status of record.

**Method.** The codebase was fanned out into per-region slices (engine hooks/save, resolve/scan, lua-binders, probes, mod-absorb, rom-misc, public-headers, test-plugins C++, test-plugins Lua/TOML, builtin/examples/docs). Each slice was read in full and hex-grepped; every candidate literal was then re-read in place by an independent verifier and cross-referenced against both seed CSVs (`data/seeds/address_names_seed.csv` and `data/seeds/address_versions_seed.csv`) to set `db_status`. A **finding** here is an in-source literal that locates a position in the game binary `WHGame.dll` — an RVA/VA (code or data), an AOB byte pattern used as a scan locator, a vtable slot index, or a vtable byte offset reaching a game-interface method — that lives outside the address database. Literals that are *not* game-binary locators (OS error codes, hash constants, alignment/capacity math, file-format magic, CryEngine flag bitsets passed to named calls, kcdx-owned asset paths, deliberately-bogus `DEADBEEF` sentinels) are out of scope and listed in Appendix A.

**Intentionally excluded as the legitimate homes for raw addresses:** `data/seeds/` (the seed CSVs ARE the DB source-of-truth), `_research/` (Ghidra dumps / RE working notes), `third-party-ghidra/` (the analyzed project + binaries), and `vendor/` (third-party code). Literals there are expected and are not findings.

---

## Findings by location

Grouped by file (sorted). Every occurrence is its own row; duplicates within a file are NOT collapsed.

### [`docs/VERIFY_PHASE4.md`](../../docs/VERIFY_PHASE4.md)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 31 | `0x5605B8` | rva | IsInCombat wrapper function entry (RVA-4 of seed id 7's pattern-hit 0x005605BC) | already_in_db | id 7 `IsInCombat_callsite_26b` (entry-of). Archived/superseded doc; low hygiene weight. |

### [`docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md`](../../docs/known-issues/BugSplat%20dmp%20files%20don't%20reach%20disk%20for%20AV%20crashes.md)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 29 | `0x183aa3508` | rva | Colon-form app-name string ('Kingdom Come: Deliverance II') in WHGame.dll .rdata (RVA 0x3AA3508) | not_in_db | Historical/disproven. Genuine .rdata data-address literal. |
| 31 | `0x183e18290` | rva | No-colon app-name string ('Kingdom Come Deliverance II') in WHGame.dll .rdata (RVA 0x3E18290) | not_in_db | Historical/disproven. |
| 144 | `0x1824599e7` | rva | LEA instruction VA (RVA 0x24599E7) once hypothesized to feed BugSplat filename; disproven | not_in_db | Same VA as the load_order.toml occurrence. Patch-reference table. |

### [`examples/archive/v0.1-schema/conflict-test-hook-on-hook/kcdx.toml`](../../examples/archive/v0.1-schema/conflict-test-hook-on-hook/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 21 (cited 17) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | 26-byte IsInCombat() callsite AOB; conflict_hook_A | already_in_db | id 7 `IsInCombat_callsite_26b`. Cited line is the `[[hook]]` header; pattern row is 21. |
| 29 (cited 30) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | Same AOB; conflict_hook_B (deliberately resolves to same entry) | already_in_db | id 7. Distinct occurrence per no-dedupe rule. |

### [`examples/archive/v0.1-schema/conflict-test-hook-on-patch/kcdx.toml`](../../examples/archive/v0.1-schema/conflict-test-hook-on-patch/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 22 (cited 36) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | 26-byte IsInCombat() AOB; `[[patch]]` (identity no-op at entry) | already_in_db | id 7. File is 35 lines; cited line is past EOF — actual is 22. |
| 32 (cited 49) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | Same AOB; `[[hook]]` (rel32 jmp at entry, offset=-4) | already_in_db | id 7. Second occurrence in this file; cited line past EOF — actual is 32. |

### [`examples/archive/v0.1-schema/conflict-test-patch-on-hook/kcdx.toml`](../../examples/archive/v0.1-schema/conflict-test-patch-on-hook/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 18 & 26 (cited 50) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | 26-byte IsInCombat() AOB; on BOTH the `[[hook]]` (line 18) AND `[[patch]]` (line 26) | already_in_db | id 7. File is 31 lines; cited line 50 does not exist — two real occurrences at lines 18 and 26. |

### [`examples/archive/v0.1-schema/no-combat-state-hook/kcdx.toml`](../../examples/archive/v0.1-schema/no-combat-state-hook/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 21 (cited 19) | `0x5605B8` | rva | IsInCombat wrapper (FUN_1805605b8) function entry; RVA prose comment | already_in_db | id 7 (entry-of). Cited line off by 2; actual prose is line 21. |
| 37 (cited 24) | `48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02` | pattern | 26-byte IsInCombat() AOB; offset=-4 to entry; detours to 'xor eax,eax; ret' | already_in_db | id 7. Cited line is `[[hook]]` header; pattern row is 37. |

### [`examples/archive/v0.1-schema/outfit-swap-dll/outfit-swap-dll.cpp`](../../examples/archive/v0.1-schema/outfit-swap-dll/outfit-swap-dll.cpp)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 24 | `48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | 23-byte Tier-2 context AOB for outfit-swap callsite; +20 'mov r14b,al' rewritten to 'xor r14d,r14d' | already_in_db | id 6 `outfit_swap_callsite_context` (RVA 0x00561745). Passed to `mem->ScanPattern`. |

### [`examples/archive/v0.1-schema/outfit-swap-in-combat/kcdx.toml`](../../examples/archive/v0.1-schema/outfit-swap-in-combat/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 21 (cited 18) | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | 16-byte Tier-1 outfit-swap callsite AOB; +13 'mov r14b,al' rewrite | already_in_db | id 5 `outfit_swap_callsite_aob` (RVA 0x0056174C). Cited line is `[[patch]]` header. |
| 30 (cited 26) | `48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | 23-byte Tier-2 context AOB (uniqueness anchor for same site) | already_in_db | id 6 `outfit_swap_callsite_context` (RVA 0x00561745). `context=` literal at line 30. |

### [`examples/archive/v0.1-schema/outfit-swap-lua-gate/kcdx.toml`](../../examples/archive/v0.1-schema/outfit-swap-lua-gate/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 29 (cited 12) | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | 16-byte Tier-1 outfit-swap AOB; `[[mid_hook]]` at +13 lets Lua zero r14 | already_in_db | id 5 `outfit_swap_callsite_aob`. Cited line is in the comment block; pattern row is 29. |

### [`examples/archive/v0.1-schema/phase5f-lua-callback-test/kcdx.toml`](../../examples/archive/v0.1-schema/phase5f-lua-callback-test/kcdx.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 24 (cited 18) | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | 16-byte Tier-1 outfit-swap AOB; `[[hook]]` at offset=0 calls a pak-Lua callback | already_in_db | id 5 `outfit_swap_callsite_aob`. Cited line off by ~6; pattern row is 24. |

### [`kcdx-engine/load_order.toml`](../../kcdx-engine/load_order.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 16 | `VA 0x1824599e7` | rva | LEA in WHGame.dll once hypothesized to feed BugSplat dmp-filename (RVA 0x24599E7); disproven | not_in_db | Prose in a config file. fix is enabled=false; descriptive/historical, low hygiene weight. |

### [`src/hooks.cpp`](../../src/hooks.cpp)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 60 | `48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8` | pattern | WHGame.dll lua_pcall (RVA 0x0071A5A4) | already_in_db | id 1 `lua_pcall`. `PCALL_SIG`, scanned at line 892 via FindUniqueSig — should `refdb`-resolve by name. Seed prose carries the identical literal. |
| 61 | `48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1` | pattern | WHGame.dll CGame::Update per-frame tick (RVA 0x00667B24) | already_in_db | id 2 `CGame_Update`. `UPDATE_SIG`, scanned at line 893 — should resolve by name. |

### [`src/mod_absorb/post_bracket_probe.cpp`](../../src/mod_absorb/post_bracket_probe.cpp)

(Whole file is an ARCHIVED probe under `#if 0` — compile-disabled, not in production. Literals are still real.)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 47 | `0x019C6268` | rva | WHGame.dll 'frame-4' per-frame CSystem main-loop dispatch fn; base+RVA → MH_CreateHook detour target | not_in_db | `kFrame4Rva`. Live (but archived) base+RVA AP1 dependency. |
| 131 | `0x0492B8A8` | rva | WHGame.dll .data slot gEnv->pConsole; base+RVA, SEH-read as singleton-instance pointer | already_in_db | id 10 `gEnv_pConsole` (gEnv 0x0492B800 + 0xA8). |
| 147 | `0xB8` | struct_offset | IConsole vtable +0xB8 = slot 23 = IConsole::GetCVar (reached via the gEnv_pConsole singleton chain) | already_in_db | id 16 `IConsole_GetCVar` (recorded as vtable[23]). Probe READS the slot (observation), doesn't call. |
| 159 | `0x0492B8A8` | rva | Same .data slot gEnv->pConsole; documentation echo inside a LOG_INFO_KV detail string (same literal drives the line-131 executable read) | already_in_db | id 10 `gEnv_pConsole`. |

### [`src/probes/fopen_override_probe.cpp`](../../src/probes/fopen_override_probe.cpp)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 56–57, 88–89 | `0x004614A0`, `0x0492B850`, `0x1804609d0` | rva | 0x004614A0 = CCryPak_FOpen body; 0x0492B850 = gEnv_pCryPak static slot; 0x1804609d0 = FClose (vtable +0x1B8) | already_in_db | id 131 (0x004614A0), id 132 (0x0492B850); FClose covered by id 131 prose. **Comment-only** — live path resolves by name (lines 365/385). Hygiene/relit-in-comment. |
| 64 | `36` (0x120/8) | vtable_slot | ICryPak::FOpen vtable slot 36 (offset +0x120) on live *gEnv->pCryPak; one-shot reach-consistency assertion | already_in_db | id 131 `CCryPak_FOpen` prose curates this slot. Detour itself is name-resolved; only the slot literal is relit. |

### [`src/probes/loc_dump_probe.cpp`](../../src/probes/loc_dump_probe.cpp)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 54 | `0x9f0ce4` | rva | CLocalizedStringsManager ctor (FUN_1809f0ce4); base+RVA → MH_CreateHook detour target | not_in_db | `kCtorRva`. Self-labeled 'USER-APPROVED DEFERRAL' (seed promotion deferred to feature graduation). |
| 64–65 | `21`, `22` (offsets 0xA8 / 0xB0) | vtable_slot | CLocalizedStringsManager vtable slots 21 (CryStringT LocalizeString overload) & 22 (raw C-string overload) | not_in_db | `kLocSlot21`/`kLocSlot22`. Overload RVAs read off live vtable (clean); the slot indices themselves are the hard-coded locators (AP3). |

### [`src/probes/loc_dump_probe.h`](../../src/probes/loc_dump_probe.h)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 24, 26–27 | `0x9f0ce4`, slot 21 (0xA8), slot 22 (0xB0) | rva | Documentation-prose form of the ctor RVA + LocalizeString vtable slots 21/22 | not_in_db | 'Verified RE facts' block embeds the same un-seeded locators. Also an AP16 public/private concern (FUN_/RVA prose in a published header). |

### [`src/save_load_hooks.cpp`](../../src/save_load_hooks.cpp)

**RESOLVED (the 5 operative locators).** The 4 SIG-pattern rows + the slot-resolver SIG below were the live runtime locators; all five sites now resolve through the Address Library by name (`Install()` calls `refdb::ResolveAddrByName`), and the `*_SIG` constants were deleted. The five entities are seed rows ids 144–148 (`SaveGame` / `LoadGame_wrapper` / `PostLoadGame` / `DeleteSavegame` / `SaveGameRecord_SlotResolver`), exercised by `test-plugins/cap-67-save-load-resolve`. The remaining rows here are comment-only provenance (no runtime dependency); the slot-resolver inline-disasm block was kept as provenance. Line numbers below are pre-resolution.

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 33 | `4C 8B DC 49 89 5B 08 49 89 73 18 49 89 7B 20 55 41 54 41 55 41 56 41 57 48 8B EC 48 83 EC 50 40 8A 7D 58 48 8D 05 B2 A6` | pattern | WHGame.dll SaveGame (7-arg __fastcall) | resolved | Was `SAVE_GAME_SIG`; deleted. Now seed id 144 `SaveGame`, resolved by name. |
| 37 | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 41 8B D8 8B FA 48 8B F1 E8 C0 F1 FF FF 48 8D 8E C8 00 00 00 66 C7 86 C0 00` | pattern | WHGame.dll LoadGame_wrapper (char __fastcall (self, playline, slot)) | resolved | Was `LOAD_GAME_WRAPPER_SIG`; deleted. Now seed id 145 `LoadGame_wrapper`, resolved by name. |
| 41 | `48 89 5C 24 10 55 56 57 41 56 41 57 48 8D 6C 24 C9 48 81 EC 90 00 00 00 49 8B F8 8B DA 48 8B F1 E8 7F F1 35 FE 4C 8B B8` | pattern | WHGame.dll PostLoadGame (char __fastcall (self, arg2, arg3)) | resolved | Was `POST_LOAD_GAME_SIG`; deleted. Now seed id 146 `PostLoadGame`, resolved by name. |
| 45 | `48 89 5C 24 08 48 89 6C 24 10 48 89 74 24 18 57 41 56 41 57 48 83 EC 40 48 63 EA 45 8B F8 8B D5 48 8B F1 E8 40 19 42 FF` | pattern | WHGame.dll DeleteSavegame (char __fastcall (self, int32 slot, u32 flags)) | resolved | Was `DELETE_SAVEGAME_SIG`; deleted. Now seed id 147 `DeleteSavegame`, resolved by name. |
| 49 | `0x1819DDE78` | rva | SaveGameManager slot-resolver function entry (RVA 0x019DDE78); inline-disasm header comment | comment-only | Provenance comment block (kept). Runtime resolution is now seed id 148 `SaveGameRecord_SlotResolver`, by name. |
| 55 | `0x180703c0c` | rva | Internal jmp target inside the resolver body (vector_get / SaveGameRecord-lookup helper) | comment-only | Comment-only disassembly transcription; no runtime dependency. Weakest finding (RE working-note in a .cpp comment). |
| 62 | `48 63 C2 48 8D 14 C0 48 8D 0C D1 41 8B D0 48 83 C1 08 E9 7D 5D D2 FE` | pattern | WHGame.dll SaveGameRecord slot resolver @ 0x1819DDE78 (RVA 0x019DDE78); returns SaveGameRecord* in rax | resolved | Was `SLOT_RESOLVER_SIG`; deleted. Now seed id 148 `SaveGameRecord_SlotResolver`, resolved by name. |

### [`src/save_load_hooks.h`](../../src/save_load_hooks.h)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 34 | `0x1819DDE78` | rva | SaveGameManager slot-resolver function entry (the LoadGameSelected hook target); doc comment | comment-only | Comment-only provenance; runtime resolution is now seed id 148 `SaveGameRecord_SlotResolver`, resolved by name. |

### [`test-plugins/cap-03-hook-lua-callback/plugin.lua`](../../test-plugins/cap-03-hook-lua-callback/plugin.lua)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 29–30 | `48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 80 B9 C1 05 00 00 00 48 8B D9` | pattern | CGame_per_frame_ui_pump (direct CGame::Update callee, RVA 0x00865FB4) | already_in_db | id 4 / kcdx_id 1003 `CGame_per_frame_ui_pump`. Plugin's 'no Address Library name' comment is STALE — seed notes carry this exact 25-byte pattern. Should use `target="CGame_per_frame_ui_pump"`. |

### [`test-plugins/cap-32-scan/plugin.lua`](../../test-plugins/cap-32-scan/plugin.lua)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 29 | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | pattern | outfit_swap-in-combat mid-function callsite (RVA 0x0056174C) | already_in_db | id 5 / kcdx_id 1004 `outfit_swap_callsite_aob`. Diagnostic `kcdx.scan`, but still a relit DB-covered locator. |

### [`test-plugins/cap-33-author-targets/targets.toml`](../../test-plugins/cap-33-author-targets/targets.toml)

| line | literal | kind | targets | db_status | note |
|---|---|---|---|---|---|
| 36 | `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D` | pattern | luaL_openlibs entry prologue (RVA 0x01449600, sig 'void (ptr L)') | already_in_db | kcdx_id 115 `luaL_openlibs` (the cap-33 fixture + docs originally mis-cited it as id 1190; corrected to 115). Sibling `[[target]]` rows in the same file use `address_id =`; this one relits the AOB. Deliberate expert-hatch demo, but duplicates a curated entity. |

---

## Already in the DB (hygiene — code relits a literal that is already a curated seed entity)

These are the cheapest migrations: the entity already exists, the code just needs to resolve it by name/id. **27 occurrences.**

| file:line | literal | existing seed entity | the by-name/by-id call that should replace it |
|---|---|---|---|
| `src/hooks.cpp:60` | `48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8` | id 1 `lua_pcall` (rva 0x0071A5A4) | resolve `"lua_pcall"` via `refdb::ResolveAddrByName` instead of `FindUniqueSig(whgame, PCALL_SIG, ...)` |
| `src/hooks.cpp:61` | `48 8B C4 ... 48 8B F1` (81-byte) | id 2 `CGame_Update` (rva 0x00667B24) | resolve `"CGame_Update"` instead of `FindUniqueSig(whgame, UPDATE_SIG, ...)` |
| `src/probes/fopen_override_probe.cpp:56–57,88–89` | `0x004614A0`, `0x0492B850`, `0x1804609d0` | id 131 `CCryPak_FOpen`, id 132 `gEnv_pCryPak`, FClose (id 131 sibling slot +0x1B8) | comment-only; drop the relit RVAs (live path already name-resolves) |
| `src/probes/fopen_override_probe.cpp:64` | `36` (0x120/8) | id 131 `CCryPak_FOpen` (prose: 'ICryPak vtable slot 36 / +0x120') | compare the assertion against an entity-attached slot constant rather than in-source `36` |
| `src/mod_absorb/post_bracket_probe.cpp:131` | `0x0492B8A8` | id 10 `gEnv_pConsole` | resolve `"gEnv_pConsole"` instead of `g_whgameBase + kSingletonGlobalRva` |
| `src/mod_absorb/post_bracket_probe.cpp:147` | `0xB8` | id 16 `IConsole_GetCVar` (vtable[23]) | use the named-slot entity (slot 23) instead of literal `0xB8` |
| `src/mod_absorb/post_bracket_probe.cpp:159` | `0x0492B8A8` | id 10 `gEnv_pConsole` | log-string echo of the line-131 dependency; resolve once by name |
| `test-plugins/cap-03-hook-lua-callback/plugin.lua:29–30` | `48 89 5C 24 08 ... 48 8B D9` (25-byte) | id 4 / 1003 `CGame_per_frame_ui_pump` | `kcdx.hook{ target="CGame_per_frame_ui_pump", ... }` instead of `pattern=PATTERN` (fix the stale comment too) |
| `test-plugins/cap-32-scan/plugin.lua:29` | `48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0` | id 5 / 1004 `outfit_swap_callsite_aob` | `kcdx.scan{ name=..., target="outfit_swap_callsite_aob" }` (or by-id) |
| `test-plugins/cap-33-author-targets/targets.toml:36` | `48 89 5C 24 08 57 48 83 EC 20 48 8B F9 48 8D 1D` | kcdx_id 115 `luaL_openlibs` | `address_id = 115` (matching the sibling `[[target]]` rows) |
| `examples/archive/v0.1-schema/outfit-swap-dll/outfit-swap-dll.cpp:24` | `48 8B 88 90 ... 44 8A F0` (23-byte) | id 6 `outfit_swap_callsite_context` (rva 0x00561745) | resolve by name/id instead of `mem->ScanPattern(..., kPattern)` |
| `examples/archive/v0.1-schema/conflict-test-hook-on-hook/kcdx.toml:21` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/conflict-test-hook-on-hook/kcdx.toml:29` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/conflict-test-patch-on-hook/kcdx.toml:18` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/conflict-test-patch-on-hook/kcdx.toml:26` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/conflict-test-hook-on-patch/kcdx.toml:22` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/conflict-test-hook-on-patch/kcdx.toml:32` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/no-combat-state-hook/kcdx.toml:37` | `48 8B 41 08 ... 3C 02` (26-byte) | id 7 `IsInCombat_callsite_26b` | `target="IsInCombat_callsite_26b"` |
| `examples/archive/v0.1-schema/no-combat-state-hook/kcdx.toml:21` | `0x5605B8` | id 7 `IsInCombat_callsite_26b` (entry RVA-4) | prose comment; reference the entity, not the raw RVA |
| `examples/archive/v0.1-schema/outfit-swap-in-combat/kcdx.toml:21` | `48 81 C1 60 0B ... 44 8A F0` (16-byte) | id 5 `outfit_swap_callsite_aob` | `target="outfit_swap_callsite_aob"` |
| `examples/archive/v0.1-schema/outfit-swap-in-combat/kcdx.toml:30` | `48 8B 88 90 ... 44 8A F0` (23-byte) | id 6 `outfit_swap_callsite_context` | `target="outfit_swap_callsite_context"` |
| `examples/archive/v0.1-schema/outfit-swap-lua-gate/kcdx.toml:29` | `48 81 C1 60 0B ... 44 8A F0` (16-byte) | id 5 `outfit_swap_callsite_aob` | `target="outfit_swap_callsite_aob"` |
| `examples/archive/v0.1-schema/phase5f-lua-callback-test/kcdx.toml:24` | `48 81 C1 60 0B ... 44 8A F0` (16-byte) | id 5 `outfit_swap_callsite_aob` | `target="outfit_swap_callsite_aob"` |
| `docs/VERIFY_PHASE4.md:31` | `0x5605B8` | id 7 `IsInCombat_callsite_26b` (entry RVA-4) | reference the entity in prose (doc is archived/superseded) |

> The `0x004614A0`/`0x0492B850`/`0x1804609d0` triple at `fopen_override_probe.cpp:56–57,88–89` is one finding row but covers three seed-mapped RVAs; the FClose RVA (0x1804609d0) has no dedicated row and is covered by id 131's curated sibling-slot prose (+0x1B8 = FClose). All three are comment-only.

**Archived/low-weight note:** the `post_bracket_probe.cpp` rows live in a `#if 0` block; the `examples/archive/v0.1-schema/**` and `docs/VERIFY_PHASE4.md` rows are archived/superseded material. They are real relits but carry low hygiene urgency.

---

## Missing from the DB (coverage — genuinely new seed rows needed)

Grouped by the game function/data each targets so duplicate sites collapse to one proposed entity. **17 occurrences across the groups below.**

| consuming file:line(s) | literal | kind | what it targets | proposed new seed entity name |
|---|---|---|---|---|
| `src/save_load_hooks.cpp:33` | `4C 8B DC 49 89 5B 08 ...` (40-byte) | pattern | WHGame.dll SaveGame, 7-arg `char __fastcall(self, filename, reason, flag_a, arg5, flag_b, description)` | `SaveGame` |
| `src/save_load_hooks.cpp:37` | `48 89 5C 24 08 48 89 74 24 10 ...` (40-byte) | pattern | WHGame.dll LoadGame_wrapper, `char __fastcall(self, playline, slot)` | `LoadGame_wrapper` |
| `src/save_load_hooks.cpp:41` | `48 89 5C 24 10 55 56 57 41 56 41 57 ...` (40-byte) | pattern | WHGame.dll PostLoadGame, `char __fastcall(self, arg2, arg3)` | `PostLoadGame` |
| `src/save_load_hooks.cpp:45` | `48 89 5C 24 08 48 89 6C 24 10 ...` (40-byte) | pattern | WHGame.dll DeleteSavegame, `char __fastcall(self, int32 slot, u32 flags)` | `DeleteSavegame` |
| `src/save_load_hooks.cpp:62` (locator); `src/save_load_hooks.cpp:49` + `src/save_load_hooks.h:34` (RVA provenance comments); `src/save_load_hooks.cpp:55` (internal jmp comment) | `48 63 C2 48 8D 14 C0 ...` (24-byte) / `0x1819DDE78` (RVA 0x019DDE78) / internal `0x180703c0c` | pattern + rva | WHGame.dll SaveGameRecord slot resolver, `void* __fastcall(sub_object, int32 playline_idx, int32 slot_idx)`, returns SaveGameRecord* in rax; tail-jumps to vector_get helper at 0x180703c0c | `SaveGameRecord_SlotResolver` (record the 24-byte AOB as the locator + RVA 0x019DDE78 + the vector_get jmp target as prose) |
| `src/probes/loc_dump_probe.cpp:54`; `src/probes/loc_dump_probe.h:24` | `0x9f0ce4` | rva | WHGame.dll CLocalizedStringsManager ctor (FUN_1809f0ce4) | `CLocalizedStringsManager_ctor` |
| `src/probes/loc_dump_probe.cpp:64–65`; `src/probes/loc_dump_probe.h:26–27` | `21`, `22` (offsets 0xA8 / 0xB0) | vtable_slot | CLocalizedStringsManager LocalizeString overloads — slot 21 (CryStringT) FUN_18051d514, slot 22 (raw C-string) FUN_18242e770 | `CLocalizedStringsManager_LocalizeString` (3000-band vtable rows for slots 21 & 22) |
| `src/mod_absorb/post_bracket_probe.cpp:47` | `0x019C6268` | rva | WHGame.dll 'frame-4' per-frame CSystem main-loop dispatch fn (single this-arg fastcall) | `CSystem_frame4_dispatch` (archived probe — defer until revived) |
| `kcdx-engine/load_order.toml:16`; `docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md:144` | `0x1824599e7` (VA; RVA 0x24599E7) | rva | WHGame.dll LEA once hypothesized to feed BugSplat dmp-filename construction; **disproven** | (none — disproven site; capture as RE prose only, not a behavioral seed, unless re-needed) |
| `docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md:29` | `0x183aa3508` (VA; RVA 0x3AA3508) | rva | WHGame.dll .rdata colon-form app-name string 'Kingdom Come: Deliverance II' | (data-string; capture as RE prose only — historical/disproven) |
| `docs/known-issues/BugSplat dmp files don't reach disk for AV crashes.md:31` | `0x183e18290` (VA; RVA 0x3E18290) | rva | WHGame.dll .rdata no-colon app-name string 'Kingdom Come Deliverance II' | (data-string; capture as RE prose only — historical/disproven) |

**Notable coverage gap:** the entire save/load lifecycle (`SaveGame`, `LoadGame_wrapper`, `PostLoadGame`, `DeleteSavegame`, `SaveGameRecord_SlotResolver`) is absent from the DB — five real, runtime-consumed AOB locators in `src/save_load_hooks.cpp` with no seed entity. These are the highest-value coverage additions (production engine code, actively scanned at install). The localization manager (`CLocalizedStringsManager_ctor` + LocalizeString slots) is the next gap, currently a documented user-approved deferral. The `post_bracket_probe` and BugSplat literals are archived/disproven and lower priority.

---

## Uncertain / needs maintainer adjudication

No findings carried `db_status=uncertain` after verification. The two reader-flagged uncertainties were both resolved on seed re-read:

- `src/mod_absorb/post_bracket_probe.cpp:131` (`0x0492B8A8`) — reader said `not_in_db`; verifier corrected to **already_in_db** = id 10 `gEnv_pConsole` (gEnv 0x0492B800 + 0xA8; the reader's proposed id 132 `gEnv_pCryPak` at 0x0492B850 is a different slot 0x58 lower).
- `docs/known-issues/…:144` (`0x1824599e7`) — reader said `uncertain`; verifier confirmed **not_in_db** (RVA 0x24599E7 absent from both seed CSVs).

Two **low-confidence** findings the maintainer may wish to scope-adjudicate (kept as findings, but borderline):

- `src/save_load_hooks.cpp:55` (`0x180703c0c`, confidence low) — an internal `jmp` operand inside an inline-disassembly comment transcribing the resolver body. It is a real WHGame.dll code RVA physically in `src/`, but it is pure RE provenance with **no runtime dependency** (kcdx never resolves/scans/hooks it). Open question: should descriptive disassembly inside a `.cpp` comment be treated as an in-source address literal, or as RE working-notes that merely live outside `_research/`? If the latter, it (and the comment-only RVA provenance at `save_load_hooks.cpp:49` / `save_load_hooks.h:34`) belong in seed prose / `_research/`, not flagged as code locators.

---

## Appendix A — checked and cleared

Candidates the audit considered and rejected as out-of-scope (not game-binary locators):

| file:line | literal | reason cleared |
|---|---|---|
| `src/console.cpp:208` | `0x00080000` | CryEngine `VF_RESTRICTEDMODE` flag bitmask passed to the named `IConsole::AddCommand` — config, not a locator. |
| `src/ki0001_node_classifier_selftest.cpp:75` | `0xC0000374` | NTSTATUS STATUS_HEAP_CORRUPTION in a PASS-message string. |
| `src/ki0001_node_classifier_selftest.cpp:87` | `0xC0000374` | Same NTSTATUS code in the FAIL-branch snprintf string. |
| `src/ki0001_node_classifier_selftest.h:4` | `0xC0000374` | STATUS_HEAP_CORRUPTION in a comment. |
| `src/hooks.cpp:215` | `0xC0000374` | NTSTATUS code in an archived-probe comment header. |
| `src/hooks.cpp:787` | `0xC0000374` | NTSTATUS code in a regression-description comment. |
| `src/crash_guard.cpp:55` | `0xE06D7363` | Microsoft C++ SEH exception code in a switch over exception codes. |
| `src/modification_inventory.cpp:50` | `0x9E3779B97F4A7C15` | Fibonacci/golden-ratio hashing multiplier. |
| `src/trampoline.cpp:52` | `0x7FFF0000` | rel32 reach-window safety margin (addressing math). |
| `src/trampoline.cpp:74` | `0x80000000` | rel32 signed-32-bit window bound in a comment. |
| `src/trampoline.cpp:146` | `0x10000` | 64 KB allocation-granularity alignment. |
| `src/trampoline.cpp:164` | `0x10000` | Same 64 KB alignment constant (local-pool path). |
| `src/serialization.cpp:53` | `0x58444358` | kcdx cosave file-format magic ('KCDX'). |
| `src/serialization.cpp:1081` | `0x811C9DC5` | FNV-1a 32-bit offset basis. |
| `src/serialization.cpp:1086` | `0x01000193` | FNV-1a 32-bit prime. |
| `src/survival.cpp:79` | `0x40000000` | 1 GB ReadFile chunk-capacity clamp. |
| `src/probes/fopen_override_probe.cpp:135` | `0x10006` | FOpen `nFlags` bitset passed to the named, resolved FOpen call — config. |
| `src/probes/fopen_override_probe.cpp:119` | `scripts/cheat/cheat_util.lua` | Game-data virtual path used as a runtime string-compare selector, not a binary AOB. |
| `src/probes/fopen_override_probe.cpp:134` | `Data/kcdx_assets/cheat_util_override.lua` | kcdx-authored substitute asset path, not a game-binary locator. |
| `test-plugins/comp-12-target-collision/comp_12_target_b/targets.toml:13` | `DE AD BE EF ... ×20` | Deliberately-bogus sentinel that matches nothing — name-collision test fixture, never a working locator. |
| `test-plugins/cap-35-uninstall/plugin.lua:127` | `DE AD BE EF ... ×16` | Well-formed-but-non-matching sentinel for the uninstall teaching-error path — matches nothing. |

---

## Appendix B — coverage

Per-region read coverage and any noted gaps.

- **engine-hooks-save** — Read all 19 assigned files in full (hooks.h/cpp, save_load_hooks.h/cpp, console.h/cpp, trampoline*.h/cpp, serialization.h/cpp, survival*.cpp, modification_inventory.cpp, crash_guard.h/cpp, ki0001_node_classifier_selftest.h/cpp); full hex-literal grep. All non-finding hex literals were OS error/exception codes, math/hash constants, alignment/capacity constants, file-format magic, x86-64 rel32 bounds, or CryEngine flag enums. Save/load RVAs in comments are documentation; runtime resolution is via AOB scan (FindUniqueSig).
- **engine-resolve-scan** — Read in full: address_library.h/cpp, refdb.h/cpp (refdb.cpp first 100), scan_engine.h/cpp (cpp first 80), patch_engine.h, hook_engine.h, hook_chain.h (first 120), symbols.h, hook_payload.h, hook_signature.h, conflict_engine.h (first 80), detour_hook.h, declared_targets.h, declare_interface.h, target_manifest.h, pe_helpers.h, version_compat.h, version_check_cache.cpp (first 50). Grep for 5–8-hex literals hit only out-of-region files. Region relies entirely on runtime resolution; **0 findings**.
- **engine-lua-binders** — Read in full: all 22 lua_bind_*.cpp/h, lua_memory*, lua_lifecycle*, lua_cosave_serial.cpp, lua_registry.cpp, dynamic_call_jit.cpp/h, bytes_interface.cpp/h, hook_interface.cpp/h, scripting*.cpp, messaging.cpp, interfaces.cpp. Exhaustive grep for hex/Lua-constants/patterns/slots/offsets. Bridge correctly uses Address Library IDs + refdb; Lua vendor constants come from lua.h. Findings here are the AOB/pattern locators in hooks.cpp and save_load_hooks.cpp.
- **engine-probes** — Fully read all six probe files (bugsplat_ctor_probe.cpp/h, fopen_override_probe.cpp/h, loc_dump_probe.cpp/h). Searched hex literals, byte patterns, vtable slots/offsets, struct offsets. The archived PROBE Z in bugsplat_ctor_probe.cpp (`#if 0`, lines 121–211) carries only asmjit/MinHook machinery — no address literals. No other files in src/probes/.
- **engine-mod-absorb** — All files in src/mod_absorb/ read in full (ctor_bracket, select_detour, record_synth, record_validate, mod_manifest, pak_mod_registry, enabled_list_builder, order_persist, post_bracket_probe, all *_selftest.cpp). Production code uses refdb::ResolveByName/ResolveAddrByName. Struct-field/module-relative offsets in comments (0x00…0x68, 0x2B30, 0x2440C85, 0x0B60) are record-layout constants, not RVAs. Only hard-coded RVAs are in the archived `#if 0` post_bracket_probe.cpp (lines 47, 131).
- **engine-rom-misc** — Read systematically: rom_borrowed (asmjit_helper, runtime_func_t, type_info_t — clean), dllmain.cpp, ldr_notify, init_phase, zone_gate, load_order, plugin_loader, lua_plugin_loader, config, paths, dev, log, task, test, watchdog_spawn, watchdog/main.cpp, loader/main.cpp, blake3 (hash constants only). Findings limited to loc_dump_probe.cpp, fopen_override_probe.cpp, and the post_bracket_probe.cpp log-string echo. All else clean.
- **public-headers** — Interfaces.h (2377 lines) + Kcdx.h (609 lines) read complete. No RVAs/patterns/slots/offsets beyond doc comments; all hex literals are version constants, sentinels, or example format strings. Cross-referenced both seed CSVs — no conflicts. No private references. **0 findings.**
- **test-plugins-cpp** — All C++ sources under test-plugins/ read (52 files enumerated, 26 actual .cpp read: cap-04/05/07-10/12-13/20-22/29/36-42/62-65, comp-02-03/14, engine-self-test, probe-crash-trigger, probe-comp-crash). No hard-coded game-address literals — these plugins exemplify correct named-target resolution. **0 findings.**
- **test-plugins-lua-toml** — All .lua/.toml under test-plugins/ audited (50+ files); key files read in full + comprehensive grep for hex/AOB/address_id/rva=/pattern=/signature=. Excluded as out-of-scope: RVA-citation comments (cap-01 line 40, cap-03 line 6 — documentation, code uses target= names), test-only dummies (cap-30 0xDEADBEEF, cap-50 0x12345678 'bogus VA never reached'), intentional collision patterns (comp-12, cap-35 DEADBEEF). Four findings = relit DB-covered locators (cap-03, cap-32, cap-33; see also the outfit_swap AOBs in examples).
- **builtin-examples-docs** — Examined kcdx-engine/builtin/bugsplat-filename-fix/kcdx.toml, kcdx-engine/load_order.toml, all 11 examples/archive/v0.1-schema/**/kcdx.toml, 2 example .cpp (hello-plugin.cpp, outfit-swap-dll.cpp), and major docs/**/*.md (design.md, known-issues/*, outstanding-work/*, verification/architecture). Excluded examples/archive/**/build/** (CMake) and _research/. **Gap:** a complete grep of ALL .cpp/.h under examples/archive/ was not done — only .toml + 2 key .cpp were hand-read; the docs/ comprehensive pattern search hit size limits but covered the major files. Public-facing docs (docs/lua/, docs/cpp/, docs/*.md) were prioritized.
