# seed.csv → DB verified-overlay migration mapping (139 rows)

**Audience:** the DB agent, to confirm the sibling `verified`-overlay representation covers every row kind before running the `seed.csv → DB` bootstrap migration. **Private** working artifact (planning; not published).

**Context.** The reference DB becomes the authoring source-of-truth for verified function facts; the seed CSVs under `data/seeds/` become a maintainer-generated cache projected from the DB's verified overlay. Bootstrap direction is `seed.csv → DB → generated seed CSVs`. The 139 current `seed.csv` rows are the only verified source; they seed the overlay. The generator (my side) then round-trips overlay → seed CSV byte-equivalent. (The original plan also projected a compiled-in `kEntries[]` C++ array; that array was removed when the DB took ownership of the cache, so there is no longer an in-source mirror to generate — the seed CSVs are the sole projection target.)

**The generator's correctness contract is lossless round-trip of all 8 seed.csv columns** — `id, game_version, rva, status, name, source, notes, signature`. It does NOT interpret `kind`. So `kind`/`offset` are for the DB's own representational clarity (a row that isn't a function-with-signature shouldn't be forced to mean "unknown ABI" via an empty `signature`), not a field the generator consumes. **The migration must preserve every original column value exactly**; `kind`/`offset` are ADDED metadata, derived from the notes, never replacing an original field.

## Column mapping (every row)

| seed.csv column | DB verified-overlay column | Notes |
|---|---|---|
| `id` | `kcdx_id` (PK) | append-only, never renumber/recycle |
| `game_version` | `game_version` | encoded `kcdxMakeGameVersion` at runtime; store the string `1.5.1164953` form as in CSV |
| `rva` | `rva` | hex string in CSV; NULL/empty for the 6 `vtable_index` rows (id 3000–3005) |
| `status` | `status` | `verified` (133) / `unverified` (6) |
| `name` | `name` | the resolution key (`IsInCombat`, etc.) |
| `source` | `source` (or fold into status — agent's call, **don't drop silently**) | currently all 139 = `verified`; a second provenance field |
| `notes` | `notes` (**dev-DB only**) | the provenance prose; NOT in the user DB; stays public-clean for the projected output |
| `signature` | `signature` | empty for 31 rows (see kinds below) |
| — (new) | `kind` | derived; see taxonomy |
| — (new) | `offset` | derived; only the callsite rows carry one |

## Row-kind taxonomy (108 plain functions + 31 non-plain)

108 rows are `kind=function` (verified + non-empty DSL signature) — the clean case, no per-row confirmation needed. The 31 rows below are everything that is NOT a plain function-with-signature. **These are what need your confirmation that the sibling table represents them.**

| id | name | rva | status | kind | offset | why this kind |
|---|---|---|---|---|---|---|
| 1004 | outfit_swap_callsite_aob | 0x0056174C | verified | `callsite` | `+13` | mid-function site; notes: "offset +13 is the mov r14b,al"; NOT a function entry |
| 1005 | outfit_swap_callsite_context | 0x00561745 | verified | `callsite` | (anchor; see note) | same site as 1004 extended 7 bytes upward; a longer AOB anchor, no consumer offset |
| 1006 | IsInCombat_callsite_26b | 0x005605BC | verified | `callsite` | `-4` | "function entry is at RVA-4; consumers apply offset=-4" |
| 1007 | IsInCombat_callsite_with_stack_frame | 0x00566040 | verified | `callsite` | (entry-anchor) | a different IsInCombat call site (`cmp al,1` vs 1006's `cmp al,2`); callsite, no stated consumer offset |
| 1008 | gEnv_pConsole_mov_instruction | 0x0086AD99 | verified | `instruction_anchor` | — | a specific `mov rcx,[rip+pConsole]` instruction RVA; runtime-resolved via id 1011, not a standalone AOB |
| 1009 | gEnv_pConsole | 0x0492B8A8 | verified | `data_slot` | — | static `.data` pointer slot (IConsole**), not code |
| 1010 | gEnv | 0x0492B800 | verified | `data_slot` | — | static `.data` SSystemGlobalEnvironment base |
| 1011 | string_exec_autoexec_cfg | 0x04095E58 | verified | `string_anchor` | — | `.rdata` string literal "exec autoexec.cfg"; the re-derivation seed anchor |
| 1116 | luaC_step | 0x0071FCE8 | verified | `function_no_sig` | — | real function entry; ABI just not structured into the DSL yet |
| 1152 | lua_pushfstring | 0x03993134 | verified | `function_variadic` | — | `(L, fmt, ...)` — DSL has no `...`; signature left empty by design |
| 1161 | luaL_error | 0x0399375C | verified | `function_variadic` | — | `(L, fmt, ...)` variadic |
| 1187 | luaG_runerror | 0x0399614C | verified | `function_variadic` | — | `(L, fmt, ...)` variadic |
| 1188 | luaO_pushfstring | 0x03998368 | verified | `function_variadic` | — | `(L, fmt, ...)` variadic |
| 1189 | lua_newstate | 0x014492A8 | verified | `function_no_sig` | — | real function; ABI in prose, not yet DSL'd |
| 1194 | CScriptSystem_vtable | 0x03B8AF70 | verified | `vtable_base` | — | a vtable BASE ADDRESS in `.rdata` (69 slots), not a function, not an index |
| 1195 | CScriptSystem_ctor | 0x01448E60 | verified | `function_no_sig` | — | real function (constructor); ABI not DSL'd |
| 1196 | CScriptSystem_Init | 0x01448F38 | verified | `function_no_sig` | — | real function; the Lua-boot anchor; ABI not DSL'd |
| 1197 | CScriptSystem_dtor | 0x039AD63C | verified | `function_no_sig` | — | real function (destructor); ABI not DSL'd |
| 1206 | CCryPak_FOpen | 0x004614A0 | verified | `function` | — | **plain function WITH signature** `ptr (ptr this, cstr pName, cstr szMode, u32 nFlags)`; ALSO reachable as vtable slot 36 (+0x120) per notes, but the row IS a function-by-RVA — listed here only because my auto-classifier mis-flagged it; confirm `kind=function` |
| 1207 | gEnv_pCryPak | 0x0492B850 | verified | `data_slot` | — | static `.data` pointer slot (ICryPak**) |
| 2000 | IConsole_AddCommand | 0x00B9A2B0 | verified | `function_no_sig` | — | real function (vtable[33] func-ptr overload); callback-ABI in prose, row signature empty |
| 2005 | IConsole_AddCommand_script_overload | 0x0100A3D4 | verified | `function_no_sig` | — | real function (vtable[32] script-string overload); signature empty |
| 3000 | IGame_CompleteInit_vtable_idx | (none) | unverified | `vtable_index` | — | integer vtable SLOT (slot 4), NOT an RVA; `rva` empty; resolves to 0 until `[[vtable_hook]]` |
| 3001 | IScriptSystem_CreateTable_vtable_idx | (none) | unverified | `vtable_index` | — | vtable slot 13 |
| 3002 | IScriptTable_SetValueAny_vtable_idx | (none) | unverified | `vtable_index` | — | vtable slot 7 |
| 3003 | IGame_GetIGameFramework_vtable_idx | (none) | unverified | `vtable_index` | — | vtable slot 16 |
| 3004 | IGame_GetLongName_vtable_idx | (none) | unverified | `vtable_index` | — | vtable slot 12 |
| 3005 | IGame_GetName_vtable_idx | (none) | unverified | `vtable_index` | — | vtable slot 13 |
| 3104 | ModManager_ParseManifest | 0x0243E7B8 | verified | `function_no_sig` | — | real function `(modRecord* /*rcx*/)`; ABI in prose, row signature empty |
| 3105 | ImodVtable_primary | 0x046AAF00 | verified | `vtable_base` | — | a vtable BASE ADDRESS (data RVA written into a synthesized record's +0x00); notes explicitly say "NOT a vtable index" |
| 3106 | ImodVtable_subobject | 0x046AAED8 | verified | `vtable_base` | — | vtable BASE ADDRESS for record +0x18; "NOT a vtable index" |

## Kind taxonomy summary (the enum the sibling table needs)

The agent's proposed `kind = function|callsite|aob` is too narrow. The real set is **8 kinds**:

1. `function` (108) — verified entry + DSL signature.
2. `function_no_sig` (9: 1116, 1189, 1195, 1196, 1197, 2000, 2005, 3104 + 1007-if-treated-as-entry) — real function entry, ABI not yet DSL'd (signature empty, but it IS a function).
3. `function_variadic` (4: 1152, 1161, 1187, 1188) — real function, signature empty because the DSL can't express `...`.
4. `callsite` (3–4: 1004, 1005, 1006, and 1007) — mid-function site; carries an `offset` (the `+13` / `-4` consumers apply). `aob` collapses into this (the AOB pattern lives in the notes; the row is the SITE).
5. `data_slot` (3: 1009, 1010, 1207) — static `.data` pointer/struct address, not code.
6. `string_anchor` (1: 1011) — `.rdata` string literal RVA (a resolver seed anchor).
7. `instruction_anchor` (1: 1008) — a specific instruction RVA (resolver intermediate).
8. `vtable_base` (3: 1194, 3105, 3106) — a vtable BASE ADDRESS (data RVA), distinct from a vtable index.
9. `vtable_index` (6: 3000–3005) — integer vtable SLOT, NOT an RVA; `rva` empty; `status=unverified`.

(Counts are indicative — the `function_no_sig` vs `callsite` call on **1007** is the one I want your eyes on: its name says callsite and its notes describe a call site, but it has no stated consumer offset like 1006's `-4`. Treat as `callsite` with no offset, or `function_no_sig`? It doesn't affect the generator round-trip either way — both preserve the empty signature + the rva — but it affects how the DB represents it.)

## What I need confirmed before the migration runs

1. The sibling `verified` table's `kind` column accepts all 8–9 kinds above (not just function/callsite/aob), OR a representation that distinguishes them (e.g. `data_slot`/`string_anchor`/`instruction_anchor`/`vtable_base`/`vtable_index` aren't all "function").
2. `rva` is nullable (the 6 `vtable_index` rows have no RVA — they carry an integer slot, which today lives encoded in the notes; decide whether the slot integer gets its own column or stays in notes).
3. `offset` is nullable and only the `callsite` rows populate it.
4. `signature` is nullable (31 rows empty).
5. `source` is preserved (carry it or fold into status — your call, not dropped).
6. The **1007** kind call (callsite vs function_no_sig).
