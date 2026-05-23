#include "address_library.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>
#include <set>
#include <string>
#include <vector>

#include "kcdx/Interfaces.h"
#include "log.h"
#include "plugin_loader.h"  // for g_runtimeGameVersion

namespace kcdx::address_library {

namespace {

// One row of the address library. Seeded from
// data/address-library/seed.csv. Status maps to the policy in
// data/address-library/policy.md — only "verified" rows resolve;
// "unverified" rows are tracked but return 0 from Resolve() because
// we don't promise their RVAs are correct yet.
//
// game_version uses the kcdxMakeGameVersion encoding: KCD2 build
// 1.5.1164953 → (1<<24) | (5<<16) | (1164953 & 0xFFFF) = 0x010579D9.
struct Entry {
    uint64_t    id;
    uint32_t    game_version;   // encoded via kcdxMakeGameVersion
    uintptr_t   rva;            // 0 = no RVA known for this row
    const char* status;         // "verified" or "unverified"
    const char* name;           // snake_case / source-level identifier
    const char* description;    // one-line provenance + signature; from seed CSV's notes column
    // Machine-readable function signature in the kcdx.hook DSL (see
    // src/hook_signature.h), STRUCTURED from the verified ABI prose in
    // `description`. "" = no verified ABI to structure yet (we never
    // invent one — AP2). Appended at the END of the struct so the
    // positional kEntries[] initializers below stay in lockstep: every
    // row gains the signature value as its LAST field. This struct is
    // engine-internal (anonymous namespace, never in a kcdx*Interface),
    // so adding a field is not a plugin-ABI break (AP11 covers only the
    // plugin-facing interface structs).
    const char* signature;
};

// Game-version constant for the build the seed CSV targets:
// release_1_5_1164953_841 → 1.5.1164953.
constexpr uint32_t kGV_1_5_1164953 = kcdxMakeGameVersion(1, 5, 1164953);

// Seed table — every row from data/address-library/seed.csv, in the
// same order. When the CSV changes, regenerate this table. The
// canonical source is data/address-library/seed.csv; this in-source
// mirror is the compiled-in fallback.
constexpr Entry kEntries[] = {
    // Every row mirrors data/address-library/seed.csv exactly. The seed
    // CSV is canonical; this in-source array is the compiled-in fallback
    // returned by Resolve() / ResolveByName() / ForEachResolvable().
    //
    // Each row carries a one-line description (from the seed's notes
    // column) that ResolveDescription() exposes to plugin code. Plugin
    // authors call kcdx_addr_description("lua_pcall") to see what they
    // got back.

    { 1000, kGV_1_5_1164953, 0x0071A5A4, "verified", "lua_pcall", "CryEngine-bundled Lua 5.1 lua_pcall(lua_State* L, int nargs, int nresults, int errfunc). __fastcall(rcx=L, edx=nargs, r8d=nresults, r9d=errfunc). kcdx engine hooks this in production via src/hooks.cpp:37 + 257; pattern '48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8'.", "i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)" },
    { 1001, kGV_1_5_1164953, 0x00667B24, "verified", "CGame_Update", "CGame::Update(haveFocus, updateFlags) — per-frame engine tick. __fastcall(rcx=IGame*, edx=haveFocus, r8d=updateFlags). kcdx engine hooks this in production via src/hooks.cpp:38 + 258. Main-thread by construction. Pattern is the 81-byte canonical CGame::Update prologue from yobson1.", "void (ptr self, bool haveFocus, u32 updateFlags)" },
    { 1002, kGV_1_5_1164953, 0x03993898, "verified", "luaL_loadfile", "luaL_loadfile(lua_State* L, const char* filename) -> int. __fastcall(rcx=L, rdx=filename). Called many times during boot as Scripts/**/*.lua load. Main thread. Live-verified by yobson1's shipping mod against KCD2 1.5. 50-byte pattern includes 0x240 stack-subtract literal.", "i32 (ptr L, cstr filename)" },
    { 1003, kGV_1_5_1164953, 0x00865FB4, "verified", "CGame_per_frame_ui_pump", "Per-tick UI/menu pump; direct callee of CGame::Update. __fastcall(rcx=this). Body has 5-iteration loop guarded by global flags; sets this->byte_at_0x2A2F. Used by test-plugins/cap-03-hook-lua_callback live-verified production. 25-byte sig is .text-unique; pattern '48 89 5C 24 08 48 89 74 24 10 57 48 83 EC 20 80 B9 C1 05 00 00 00 48 8B D9'.", "void (ptr self)" },
    { 1004, kGV_1_5_1164953, 0x0056174C, "verified", "outfit_swap_callsite_aob", "Inner mid-function site for the outfit-swap-in-combat patch (offset +13 is the 'mov r14b, al' that becomes 'xor r14d, r14d'). NOT a function entry — site is in the middle of the player-action handler that calls IsInCombat. Live-verified by shipping mempatch-plugins/outfit-swap-in-combat and kcdx examples. 16-byte canonical AOB; consider context extension when used as a sole locator.", "" },
    { 1005, kGV_1_5_1164953, 0x00561745, "verified", "outfit_swap_callsite_context", "Same site as outfit_swap_callsite_aob (id 1004) extended 7 bytes upward to include the call-into-IsInCombat sequence ('mov rcx, [rax+0x90]; ...'). 23 bytes. Use as a more uniquely-positioned anchor when the 16-byte AOB feels too short.", "" },
    { 1006, kGV_1_5_1164953, 0x005605BC, "verified", "IsInCombat_callsite_26b", "26-byte sig for the IsInCombat() callsite prologue: 'mov rax, [rcx+8]; mov rcx, [rax+0x90]; add rcx, 0x0B60; mov rax, [rcx]; call qword ptr [rax+8]; cmp al, 2'. Pattern '48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 02'. Used by kcdx/examples/conflict-test-{hook-on-hook,hook-on-patch,patch-on-hook} as their hook target. RVA stored is the pattern-hit position; function entry is at RVA-4 (consumers using this as a function-entry anchor apply offset = -4, as in comp-02-hook-on-patch and the conflict-test mods). Live-verified by Phase 4b.3 conflict-matrix live tests.", "" },
    { 1007, kGV_1_5_1164953, 0x00566040, "verified", "IsInCombat_callsite_with_stack_frame", "30-byte sig prefixed with 'sub rsp, 0x28' — a different call site to the same IsInCombat() vtable method from a calling routine with its own stack frame. Pattern '48 83 EC 28 48 8B 41 08 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 3C 01' ends in `3C 01` (cmp al, 1), NOT `3C 02` (cmp al, 2). The `3C 02` variant is id 1006 at RVA 0x005605BC — two distinct call sites in WHGame.dll invoking the same IsInCombat() vtable slot but checking different combat-state thresholds (1 vs 2). Used by test-plugins/comp-03-hook-on-hook-A and -B for first-wins testing. Live-verified.", "" },
    { 1008, kGV_1_5_1164953, 0x0086AD99, "verified", "gEnv_pConsole_mov_instruction", "The 'mov rcx, [rip+pConsole]' instruction inside the autoexec-executor function. RVA derived by muyuanjin's three-step resolver: (1) find 'exec autoexec.cfg' string in .rdata; (2) scan .text for any `48 8D 15 ? ? ? ?` LEA and check RIP-relative target == string address; (3) check 7 bytes preceding the matched LEA: if `4C 8B 92 18 01 00 00`, the MOV is at LEA-0x17 (V1.4+ layout, used by KCD2 1.5); else the MOV is at LEA-7 (older). pConsole_ptr_VA = MOV_VA + 7 + disp32(MOV+3..6); gEnv_VA = pConsole_ptr_VA - 0xA8. NOT a single-pattern AOB — must be resolved via the anchor (id 1011) at runtime. Live-verified by muyuanjin's shipping plugin against KCD2 1.4+. See _research/predecessor-sigs/muyuanjin-kcd2db/src/kcd2db.cpp.", "" },
    { 1009, kGV_1_5_1164953, 0x0492B8A8, "verified", "gEnv_pConsole", "Static pointer slot in .data: gEnv->pConsole. Read this 8-byte pointer to get IConsole*. RVA derived by following the RIP-relative disp32 from id 1008. Will move per game build — Address Library updates it. Subtract 0xA8 to get gEnv base (id 1010).", "" },
    { 1010, kGV_1_5_1164953, 0x0492B800, "verified", "gEnv", "Static SSystemGlobalEnvironment instance base (gEnv). Derived as id 1009 RVA - 0xA8. Plugins read fields at known offsets: pScriptSystem (+0x28), pGame (+0x90), pConsole (+0xA8), etc. Live-verified by muyuanjin/kcd2db.", "" },
    { 1011, kGV_1_5_1164953, 0x04095E58, "verified", "string_exec_autoexec_cfg", "String literal 'exec autoexec.cfg' in .rdata. Single LEA xref in .text from inside the engine's autoexec executor. Use as the seed anchor when the gEnv resolver SIG needs to be re-derived after a game update — find this string, find its single LEA xref, walk backwards to the pConsole MOV. Lives in .rdata not .text — status reflects RVA stability for the lifetime of this game build.", "" },
    { 2000, kGV_1_5_1164953, 0x00B9A2B0, "verified", "IConsole_AddCommand", "IConsole::AddCommand(const char* sCommand, ConsoleCommandFunc func, int nFlags, const char* sHelp). __thiscall (rcx=IConsole*). Callback ABI: void __fastcall (IConsoleCmdArgs*). **vtable[33]** (NOT slot 32 — the canonical CryEngine ordering is swapped in this build). Originally recorded as slot 32 by the kcdx Phase 7 vtable-dump probe; corrected 2026-05-20 after Ghidra investigation showed slot 32 is the script-string overload. Three independent confirmations: (a) slot 33's body stores r8 raw at record [+0x20]; slot 32 runs r8 through CryString-assign at 0x1804f6ac8; (b) slot 33's duplicate-registration error says 'console command [%s] is already registered' while slot 32 says 'script command [%s] is already registered'; (c) the engine's own static wrapper at 0x180B99098 (used to register playerGoto / freeze / etc.) calls vtable[33] via [pConsole->vtable + 0x108]. Full evidence at _research/phase7-recon/DISPATCH-INVESTIGATION.md.", "" },
    { 2001, kGV_1_5_1164953, 0x0100955C, "verified", "IConsole_RemoveCommand", "IConsole::RemoveCommand(const char* sName). __thiscall. vtable[34]. Live-verified by the Phase 7 probe; small function body matches the expected map-erase shape. Used for [[command]] cleanup on plugin unload (if/when v0.2 supports unload). -> bool (success flag; SETNZ BL / MOV AL,BL before RET — the engine returns the map-erase result, diverging from the canonical CryEngine 'void RemoveCommand' header per AP3). Verified by Ghidra 2026-05-22, _research/phase7-recon/_abi_console_return_types.txt.", "bool (ptr self, cstr sName)" },
    { 2002, kGV_1_5_1164953, 0x007A5818, "verified", "IConsole_ExecuteString", "IConsole::ExecuteString(const char* command, bool bSilentMode, bool bDeferExecution). __thiscall. vtable[35]. Live-verified by the Phase 7 probe. Useful for plugins that want to programmatically execute console commands (e.g., binding a hotkey to a console command via Lua). -> void (entry-point 0x007A5818 and helper 0x007A586C both return with no eax/rax write — pure register-restore before RET; matches the canonical CryEngine header, binary-verified). Verified by Ghidra 2026-05-22, _research/phase7-recon/_abi_console_return_types.txt.", "void (ptr self, cstr command, bool bSilentMode, bool bDeferExecution)" },
    { 2003, kGV_1_5_1164953, 0x009DF818, "verified", "IConsole_GetCVar", "IConsole::GetCVar(const char* name) -> ICVar*. __thiscall. vtable[23]. Live-verified by the Phase 7 probe (canonical CryEngine slot). Underpins kcdx.get_cvar_bool / get_cvar_int / get_cvar_float (Lua surface) and the C-side CVar lookup.", "ptr (ptr self, cstr name)" },
    { 2004, kGV_1_5_1164953, 0x00B99098, "verified", "IConsole_AddCommand_static_wrapper", "Engine-side static function that resolves pConsole from its known .data slot and calls vtable[33] (AddCommand func-overload). Signature: void __fastcall(const char* name, ConsoleCommandFunc func, int nFlags, const char* sHelp). Used by the engine's own boot-time registrations (playerGoto, freeze, etc.). Plugins may prefer this over directly calling AddCommand because (a) no need to fetch pConsole first, (b) survives vtable shuffles across game patches as long as the wrapper itself stays at this RVA. Discovered by the Ghidra DISPATCH-INVESTIGATION.", "void (cstr name, ptr func, i32 nFlags, cstr sHelp)" },
    { 2005, kGV_1_5_1164953, 0x0100A3D4, "verified", "IConsole_AddCommand_script_overload", "IConsole::AddCommand(const char* sName, const char* sScriptFunc, int nFlags, const char* sHelp). **vtable[32]** — the SCRIPT-STRING overload (originally mis-labeled as the func-pointer overload at id 2000). Registers a console command whose body is a Lua source string (e.g., \\Game.Connect(%1)\\\"). Plugins typically want id 2000 (function-pointer form) instead", "" },
    { 3000, kGV_1_5_1164953, 0, "unverified", "IGame_CompleteInit_vtable_idx", "IGame::CompleteInit() vtable index. muyuanjin confirms slot 4 (0-indexed) in V1.4+. __thiscall(rcx=IGame*). Fires exactly once during engine init, main thread. NOT a function-entry RVA — this is a vtable-slot integer. Address Library schema would need [[vtable_hook]] support (Phase 5+) to consume directly; for now, record as a known constant. Status unverified at the Address-Library API level until [[vtable_hook]] ships.", "" },
    { 3001, kGV_1_5_1164953, 0, "unverified", "IScriptSystem_CreateTable_vtable_idx", "IScriptSystem::CreateTable() vtable index = 13 (0-indexed). +1 from canonical 12 due to inserted unknown virtual. __thiscall. Same status caveat as id 3000.", "" },
    { 3002, kGV_1_5_1164953, 0, "unverified", "IScriptTable_SetValueAny_vtable_idx", "IScriptTable::SetValueAny() vtable index = 7 (0-indexed). +1 from canonical 6 due to inserted unknown virtual. __thiscall. Same caveat.", "" },
    { 3003, kGV_1_5_1164953, 0, "unverified", "IGame_GetIGameFramework_vtable_idx", "IGame::GetIGameFramework() vtable index = 16 (0-indexed). Returns IGameFramework*. __thiscall. Documented by muyuanjin in IDA at offset 0x80 (== 16 * 8). Same caveat.", "" },
    { 3004, kGV_1_5_1164953, 0, "unverified", "IGame_GetLongName_vtable_idx", "IGame::GetLongName() vtable index = 12 (0-indexed). __thiscall. Documented by muyuanjin at offset 0x60. Useful for plugin-side game identification. Same caveat.", "" },
    { 3005, kGV_1_5_1164953, 0, "unverified", "IGame_GetName_vtable_idx", "IGame::GetName() vtable index = 13 (0-indexed). __thiscall. Documented by muyuanjin at offset 0x68. Same caveat.", "" },
    { 1100, kGV_1_5_1164953, 0x0071A0D8, "verified", "luaL_checktype", "luaL_checktype(L, narg, t). Cross-confirmed across many luaB_*/db_* wrappers: str_dump + luaB_next + luaB_rawget + luaB_rawset + tconcat + tinsert + tremove + db_sethook all call this RVA at the matching source position. See _research/phase8-fix-a/lua_rvas.csv.", "void (ptr L, i32 narg, i32 t)" },
    { 1101, kGV_1_5_1164953, 0x0071A49C, "verified", "lua_insert", "lua_insert(L, idx). Cross-confirmed in luaB_pcall (after lua_pcall) and luaB_xpcall (between settop and pcall); both match source's lua_insert call.", "void (ptr L, i32 idx)" },
    { 1102, kGV_1_5_1164953, 0x0071A4E4, "verified", "lua_remove", "lua_remove(L, idx). Call in luaopen_math matching lua_remove(L, -2) which removes the _LOADED table after registering; cross-confirmed in db_gethook.", "void (ptr L, i32 idx)" },
    { 1103, kGV_1_5_1164953, 0x0071CA90, "verified", "lua_type", "lua_type(L, idx) -> int. Cross-confirmed in luaB_type body + first call in luaB_setmetatable + db_sethook + luaB_tostring switch dispatch.", "i32 (ptr L, i32 idx)" },
    { 1104, kGV_1_5_1164953, 0x0071D06C, "verified", "lua_rawgeti", "lua_rawgeti(L, idx, n). Fourth call in tconcat (loop body lua_rawgeti(L, 1, i)); cross-confirmed in tinsert + tremove.", "void (ptr L, i32 idx, i32 n)" },
    { 1105, kGV_1_5_1164953, 0x0071E46C, "verified", "lua_pushlstring", "lua_pushlstring(L, s, len). db_gethook calls it with rcx=L rdx=\"external hook\" r8=13 (length); auxresume call with literal \"cannot resume dead coroutine\" r8=0x1C (length 28) — PGO specialized lua_pushfstring at compile time to lua_pushlstring with literal when args are constant.", "void (ptr L, cstr s, u64 len)" },
    { 1106, kGV_1_5_1164953, 0x0071E4D4, "verified", "lua_getmetatable", "lua_getmetatable(L, objindex) -> int. Second call in luaB_getmetatable matching if (!lua_getmetatable(L, 1)).", "i32 (ptr L, i32 objindex)" },
    { 1107, kGV_1_5_1164953, 0x0071E77C, "verified", "lua_settop", "lua_settop(L, idx). Cross-confirmed across str_dump + luaB_next + luaB_rawget + luaB_rawset + luaB_error + luaB_setmetatable + luaB_xpcall + db_setlocal + db_sethook.", "void (ptr L, i32 idx)" },
    { 1108, kGV_1_5_1164953, 0x0071ECEC, "verified", "lua_touserdata", "lua_touserdata(L, idx) -> void*. Call in ll_require matching lua_touserdata(L, -1) == sentinel check.", "ptr (ptr L, i32 idx)" },
    { 1109, kGV_1_5_1164953, 0x0071EF54, "verified", "lua_pushstring", "lua_pushstring(L, s). Fourth call in luaB_type body with rcx=L rdx=string (2 args); matches lua_pushstring(L, luaL_typename(...)) exactly.", "void (ptr L, cstr s)" },
    { 1110, kGV_1_5_1164953, 0x0071F098, "verified", "lua_createtable", "lua_createtable(L, narr, nrec). Call in base_open after the auxopen pair matching lua_createtable(L, 0, 1) for the newproxy weak table.", "void (ptr L, i32 narr, i32 nrec)" },
    { 1111, kGV_1_5_1164953, 0x0071F100, "verified", "lua_pushvalue", "lua_pushvalue(L, idx). Call in luaB_getfenv with edx=0xFFFFD8EE (= -10002 = LUA_GLOBALSINDEX); matches lua_pushvalue(L, LUA_GLOBALSINDEX); cross-confirmed in luaB_setfenv + luaB_tostring + db_getlocal + db_sethook.", "void (ptr L, i32 idx)" },
    { 1112, kGV_1_5_1164953, 0x0071F128, "verified", "lua_setmetatable", "lua_setmetatable(L, objindex) -> int. Fifth call in luaB_setmetatable body matching lua_setmetatable(L, 1) after type/checktype/getmetafield/settop.", "i32 (ptr L, i32 objindex)" },
    { 1113, kGV_1_5_1164953, 0x0071F4BC, "verified", "lua_next", "lua_next(L, idx). Third call in luaB_next body matching final lua_next(L, 1) in source.", "i32 (ptr L, i32 idx)" },
    { 1114, kGV_1_5_1164953, 0x0071F6A8, "verified", "lua_tolstring", "lua_tolstring(L, idx, len). Call in db_getinfo after lua_pushfstring(L, \">%s\", options) matching options = lua_tostring(L, -1) macro = lua_tolstring(L, -1, NULL).", "cstr (ptr L, i32 idx, ptr len)" },
    { 1115, kGV_1_5_1164953, 0x0071F6FC, "verified", "lua_gettable", "lua_gettable(L, idx). Call in str_gsub's add_value helper at LUA_TTABLE case matching lua_gettable(L, 3).", "void (ptr L, i32 idx)" },
    { 1116, kGV_1_5_1164953, 0x0071FCE8, "verified", "luaC_step", "luaC_step(L) — internal GC stepper, NOT LUA_API. Originally mis-tagged as lua_newthread in the first commit; corrected after body-shape analysis showed it reads g->totalbytes/GCthreshold via [L+0x20]+offsets and calls 0x71FDAC (luaC_singlestep) — the GC step dispatch, not thread creation. luaB_cocreate's apparent lua_newthread call was actually the inlined-lua_newthread's call to luaC_checkGC (macro expanding to luaC_step). lua_newthread itself appears inlined; no separate RVA known.", "" },
    { 1117, kGV_1_5_1164953, 0x00718464, "verified", "lua_rawget", "lua_rawget(L, idx). Fourth call in luaB_rawget matching final lua_rawget(L, 1) in source.", "void (ptr L, i32 idx)" },
    { 1118, kGV_1_5_1164953, 0x00720738, "verified", "lua_rawset", "lua_rawset(L, idx). Fifth call in luaB_rawset matching final lua_rawset(L, 1); cross-confirmed in db_sethook.", "void (ptr L, i32 idx)" },
    { 1119, kGV_1_5_1164953, 0x0041C200, "verified", "lua_tonumber", "lua_tonumber(L, idx) -> lua_Number. Call in luaB_setfenv after lua_isnumber check matching lua_tonumber(L, 1); returns xmm0 (float per CryEngine's LUA_NUMBER=float).", "f32 (ptr L, i32 idx)" },
    { 1120, kGV_1_5_1164953, 0x0041C230, "verified", "lua_isnumber", "lua_isnumber(L, idx) -> int. Body-shape match: calls index2adr (0x71DD7C) then cmp [rax+8] 3 (LUA_TNUMBER → return 1); fast-path for lua_isnumber.", "i32 (ptr L, i32 idx)" },
    { 1121, kGV_1_5_1164953, 0x00B9CC48, "verified", "lua_checkstack", "lua_checkstack(L, n) -> int. Body-shape match: computes (L->top - L->base) + size > LUAI_MAXCSTACK with constant 0x800 (= CryEngine's LUAI_MAXCSTACK=2048); also called 2x from auxresume.", "i32 (ptr L, i32 n)" },
    { 1122, kGV_1_5_1164953, 0x00B9CE78, "verified", "lua_typename", "lua_typename(L, tp) -> const char*. Third call in luaB_type matching luaL_typename macro = lua_typename(L, lua_type(L,i)).", "cstr (ptr L, i32 tp)" },
    { 1123, kGV_1_5_1164953, 0x00B9D764, "verified", "lua_objlen", "lua_objlen(L, idx) -> size_t. Reached via luaL_getn macro in tconcat + tremove + tinsert; also directly from table.getn wrapper.", "u64 (ptr L, i32 idx)" },
    { 1124, kGV_1_5_1164953, 0x00B9C1AC, "verified", "lua_toboolean", "lua_toboolean(L, idx) -> int. Body-shape match: calls index2adr (0x71DD7C) then cmp [rax+8] 0 (LUA_TNIL → return 0); cmp eax 1 (LUA_TBOOLEAN check) then checks bvalue.", "i32 (ptr L, i32 idx)" },
    { 1125, kGV_1_5_1164953, 0x00B9C2A4, "verified", "lua_rawseti", "lua_rawseti(L, idx, n). Reached 2x in tinsert (the swap loop) + 2x in tremove (the shift loop) — matches source.", "void (ptr L, i32 idx, i32 n)" },
    { 1126, kGV_1_5_1164953, 0x00B9CAFC, "verified", "lua_tointeger", "lua_tointeger(L, idx) -> lua_Integer. Call near top of db_traceback matching (int)lua_tointeger(L, arg+2); cross-confirmed in db_getinfo.", "i64 (ptr L, i32 idx)" },
    { 1127, kGV_1_5_1164953, 0x00B9D0C0, "verified", "lua_newuserdata", "lua_newuserdata(L, sz) -> void*. Second call in luaB_newproxy after lua_settop matching lua_newuserdata(L, 0).", "ptr (ptr L, u64 sz)" },
    { 1128, kGV_1_5_1164953, 0x00B9D11C, "verified", "lua_pushcclosure", "lua_pushcclosure(L, fn, n). Call inside luaopen_math's library-population loop matching lua_pushcclosure(L, l->func, 0) (upvalue-copy loop skipped when nup=0).", "void (ptr L, ptr fn, i32 n)" },
    { 1129, kGV_1_5_1164953, 0x00ADB058, "verified", "luaL_findtable", "luaL_findtable(L, idx, fname, szhint). First call in luaopen_math with edx=-10000 (LUA_REGISTRYINDEX) r8=\"_LOADED\" r9d=size; called again in same fn with edx=LUA_GLOBALSINDEX.", "cstr (ptr L, i32 idx, cstr fname, i32 szhint)" },
    { 1130, kGV_1_5_1164953, 0x00ADB44C, "verified", "lua_getfield", "lua_getfield(L, idx, k). Second call in luaopen_math matching lua_getfield(L, -1, libname); 3 args.", "void (ptr L, i32 idx, cstr k)" },
    { 1131, kGV_1_5_1164953, 0x00A2D840, "verified", "lua_setfield", "lua_setfield(L, idx, k). Call in luaopen_math's library setup matching lua_setfield(L, -3, libname); cross-confirmed via lua_setfield(L, -3, \"pi\") and \"huge\".", "void (ptr L, i32 idx, cstr k)" },
    { 1132, kGV_1_5_1164953, 0x00A2D8B8, "verified", "lua_gc", "lua_gc(L, what, data). Called 2x in luaB_collectgarbage matching source's 2 lua_gc invocations (main op + GCCOUNTB in the GCCOUNT case).", "i32 (ptr L, i32 what, i32 data)" },
    { 1133, kGV_1_5_1164953, 0x00A2DA68, "verified", "lua_sethook", "lua_sethook(L, func, mask, count). Call in db_sethook with 4 args (L1, hookf, mask, count); matches lua_sethook signature exactly.", "i32 (ptr L, ptr func, i32 mask, i32 count)" },
    { 1134, kGV_1_5_1164953, 0x00CAE9A8, "verified", "lua_getstack", "lua_getstack(L, level, ar) -> int. Call in db_getlocal + db_setlocal after the luaL_checkint(L, arg+1) call matching if (!lua_getstack(L1, n, &ar)).", "i32 (ptr L, i32 level, ptr ar)" },
    { 1135, kGV_1_5_1164953, 0x00CAEA88, "verified", "lua_getinfo", "lua_getinfo(L, what, ar) -> int. Call in db_traceback after lua_getstack with rdx=\"Snl\" matching lua_getinfo(L1, \"Snl\", &ar).", "i32 (ptr L, cstr what, ptr ar)" },
    { 1136, kGV_1_5_1164953, 0x01250AC4, "verified", "luaL_checknumber", "luaL_checknumber(L, narg) -> lua_Number. First call in math_floor (and many other math_* wrappers) with (L, 1); each math_X = luaL_checknumber + CRT math fn + inlined lua_pushnumber; cross-confirmed in math_pow which calls it 2x.", "f32 (ptr L, i32 narg)" },
    { 1137, kGV_1_5_1164953, 0x0144965C, "verified", "lua_call", "lua_call(L, nargs, nresults). Inlined luaL_callmeta in luaB_tostring: after luaL_getmetafield returns true, code does lua_pushvalue(L, obj); lua_call(L, 1, 1) — call here is lua_call.", "void (ptr L, i32 nargs, i32 nresults)" },
    { 1138, kGV_1_5_1164953, 0x016546F8, "verified", "luaL_getmetafield", "luaL_getmetafield(L, obj, e) -> int. Third call in luaB_getmetatable matching luaL_getmetafield(L, 1, \"__metatable\"); cross-confirmed via inlined luaL_callmeta in luaB_tostring.", "i32 (ptr L, i32 obj, cstr e)" },
    { 1139, kGV_1_5_1164953, 0x013236D0, "verified", "lua_concat", "lua_concat(L, n). Sixth call in luaB_error matching lua_concat(L, 2).", "void (ptr L, i32 n)" },
    { 1140, kGV_1_5_1164953, 0x01323664, "verified", "luaL_pushresult", "luaL_pushresult(B). Call site in str_dump immediately after luaL_error and before security_check_cookie; source ends str_dump with luaL_pushresult(&b) followed by return 1.", "void (ptr B)" },
    { 1141, kGV_1_5_1164953, 0x013E14D8, "verified", "lua_load", "lua_load(L, reader, dt, chunkname) -> int. Call in luaB_loadstring after luaL_checklstring + luaL_optstring with the LoadS struct in stack; function calls luaD_pcall (via luaD_protectedparser).", "i32 (ptr L, ptr reader, ptr dt, cstr chunkname)" },
    { 1142, kGV_1_5_1164953, 0x00B9C9BC, "verified", "luaL_checklstring", "luaL_checklstring(L, narg, len) -> const char*. Call in db_sethook matching luaL_checkstring macro = luaL_checklstring(L, n, NULL); cross-confirmed in str_rep + str_len.", "cstr (ptr L, i32 narg, ptr len)" },
    { 1143, kGV_1_5_1164953, 0x00B9CA08, "verified", "luaL_optinteger", "luaL_optinteger(L, narg, default) -> lua_Integer. First call in luaB_error matching luaL_optint macro = (int)luaL_optinteger(L, 2, 1); cross-confirmed in luaB_tonumber + luaB_collectgarbage + tremove + db_sethook.", "i64 (ptr L, i32 narg, i64 def)" },
    { 1144, kGV_1_5_1164953, 0x00B9CAB4, "verified", "luaL_checkinteger", "luaL_checkinteger(L, narg) -> lua_Integer. Reached via luaL_checkint macro in tconcat + tinsert + tremove + db_getlocal + db_setlocal.", "i64 (ptr L, i32 narg)" },
    { 1145, kGV_1_5_1164953, 0x00B9CC20, "verified", "luaL_checkstack", "luaL_checkstack(L, sz, msg) — different from lua_checkstack. Call in luaB_unpack matching luaL_checkstack(L, n, \"too many results to unpack\").", "void (ptr L, i32 sz, cstr msg)" },
    { 1146, kGV_1_5_1164953, 0x00B9CE94, "verified", "luaL_checkany", "luaL_checkany(L, narg). Cross-confirmed across luaB_rawget + luaB_rawset + luaB_rawequal + luaB_type + luaB_getmetatable + luaB_pcall + luaB_xpcall + luaB_tostring + luaB_tonumber + luaB_unpack + db_setlocal.", "void (ptr L, i32 narg)" },
    { 1147, kGV_1_5_1164953, 0x00B9D8E4, "verified", "luaL_addlstring", "luaL_addlstring(B, s, l). Call inside str_rep's repeat loop after the buffer was initialized inline; matches luaL_addlstring(&b, s, l) (3 args).", "void (ptr B, cstr s, u64 l)" },
    { 1148, kGV_1_5_1164953, 0x039930A4, "verified", "lua_iscfunction", "lua_iscfunction(L, idx) -> int. Call in luaB_getfenv with (L, -1); cross-confirmed in luaB_setfenv.", "i32 (ptr L, i32 idx)" },
    { 1149, kGV_1_5_1164953, 0x039930CC, "verified", "lua_isstring", "lua_isstring(L, idx) -> int. Third call in luaB_error body matching the lua_isstring(L, 1) check.", "i32 (ptr L, i32 idx)" },
    { 1150, kGV_1_5_1164953, 0x039930E8, "verified", "lua_lessthan", "lua_lessthan(L, idx1, idx2) -> int. sort_comp's cold path (when comparator nil) tail-jumps to lua_lessthan via the cold-path call at 0x3999CE9 → 0x39930E8; the function calls index2adr 2x and tail-jmps to luaV_lessthan.", "i32 (ptr L, i32 idx1, i32 idx2)" },
    { 1151, kGV_1_5_1164953, 0x03993060, "verified", "lua_getupvalue", "lua_getupvalue(L, funcindex, n) -> const char*. Call in auxupvalue's get-path (test edi != 0 fall-through); source: name = get ? lua_getupvalue(L, 1, n) : lua_setupvalue(L, 1, n).", "cstr (ptr L, i32 funcindex, i32 n)" },
    { 1152, kGV_1_5_1164953, 0x03993134, "verified", "lua_pushfstring", "lua_pushfstring(L, fmt, ...) -> const char*. Called 5+ times in db_traceback with format-string args (variadic pattern); each call loads a format string (e.g. \"%s:\" \"%d:\" \" in function \" LUA_QS).", "" },
    { 1153, kGV_1_5_1164953, 0x03993178, "verified", "lua_rawequal", "lua_rawequal(L, idx1, idx2) -> int. Third call in luaB_rawequal with 3 args (L, 1, 2); lua_pushboolean inlined.", "i32 (ptr L, i32 idx1, i32 idx2)" },
    { 1154, kGV_1_5_1164953, 0x039931C0, "verified", "lua_setfenv", "lua_setfenv(L, idx) -> int. Third call in db_setfenv after luaL_checktype + lua_settop matching if (lua_setfenv(L, 1) == 0); cross-confirmed in luaB_setfenv.", "i32 (ptr L, i32 idx)" },
    { 1155, kGV_1_5_1164953, 0x03993244, "verified", "lua_setupvalue", "lua_setupvalue(L, funcindex, n) -> const char*. Call in auxupvalue's set-path (test edi != 0 taken-branch); ternary other branch from lua_getupvalue site.", "cstr (ptr L, i32 funcindex, i32 n)" },
    { 1156, kGV_1_5_1164953, 0x039932C0, "verified", "lua_tothread", "lua_tothread(L, idx) -> lua_State*. First call in luaB_costatus matching lua_State *co = lua_tothread(L, 1); cross-confirmed via luaB_coresume.", "ptr (ptr L, i32 idx)" },
    { 1157, kGV_1_5_1164953, 0x039932DC, "verified", "lua_xmove", "lua_xmove(from, to, n). auxresume calls lua_xmove 3x — site at 0x39932DC is called 3x from auxresume; cross-confirmed in db_getlocal + db_setlocal + db_sethook.", "void (ptr from, ptr to, i32 n)" },
    { 1158, kGV_1_5_1164953, 0x039934C8, "verified", "luaL_addvalue", "luaL_addvalue(B). Final call in str_gsub's add_value helper with rcx=&b matching luaL_addvalue(b); single luaL_Buffer* arg.", "void (ptr B)" },
    { 1159, kGV_1_5_1164953, 0x0399355C, "verified", "luaL_argerror", "luaL_argerror(L, narg, extramsg). Error path of db_getlocal + db_setlocal after !lua_getstack — matches return luaL_argerror(L, arg+1, \"level out of range\").", "i32 (ptr L, i32 narg, cstr extramsg)" },
    { 1160, kGV_1_5_1164953, 0x03993638, "verified", "luaL_checkoption", "luaL_checkoption(L, narg, def, lst[]) -> int. First call in luaB_collectgarbage matching luaL_checkoption(L, 1, \"collect\", opts).", "i32 (ptr L, i32 narg, cstr def, ptr lst)" },
    { 1161, kGV_1_5_1164953, 0x0399375C, "verified", "luaL_error", "luaL_error(L, fmt, ...) -> int. Cross-confirmed: str_dump (lstrlib.c) + auxresume (lbaselib.c) + tinsert all reach the same RVA.", "" },
    { 1162, kGV_1_5_1164953, 0x03993AA4, "verified", "luaL_optlstring", "luaL_optlstring(L, narg, def, len) -> const char*. First call in tconcat matching luaL_optstring expansion (L, 2, \"\", &lsep).", "cstr (ptr L, i32 narg, cstr def, ptr len)" },
    { 1163, kGV_1_5_1164953, 0x03993B00, "verified", "luaL_prepbuffer", "luaL_prepbuffer(B) -> char*. Call in str_gsub main loop with single arg (luaL_Buffer*) when buffer needs refilling.", "ptr (ptr B)" },
    { 1164, kGV_1_5_1164953, 0x03993B24, "verified", "luaL_typerror", "luaL_typerror(L, narg, tname) -> int. Located via string xref to \"%s expected, got %s\" format string in .rdata; body calls lua_type + lua_typename + lua_pushfstring then tail-jumps to luaL_argerror.", "i32 (ptr L, i32 narg, cstr tname)" },
    { 1165, kGV_1_5_1164953, 0x03993B70, "verified", "luaL_where", "luaL_where(L, level). Fourth call in luaB_error matching luaL_where(L, level).", "void (ptr L, i32 level)" },
    { 1166, kGV_1_5_1164953, 0x03996250, "verified", "lua_getlocal", "lua_getlocal(L, ar, n) -> const char*. Call in db_getlocal after the second luaL_checkint(L, arg+2); matches lua_getlocal(L1, &ar, n).", "cstr (ptr L, ptr ar, i32 n)" },
    { 1167, kGV_1_5_1164953, 0x039962B8, "verified", "lua_setlocal", "lua_setlocal(L, ar, n) -> const char*. Call in db_setlocal after lua_xmove(L, L1, 1); matches lua_setlocal(L1, &ar, luaL_checkint(L, arg+2)).", "cstr (ptr L, ptr ar, i32 n)" },
    { 1168, kGV_1_5_1164953, 0x0399605C, "verified", "lua_error", "lua_error(L) -> int. Seventh and final call in luaB_error matching return lua_error(L).", "i32 (ptr L)" },
    { 1169, kGV_1_5_1164953, 0x039966A4, "verified", "lua_resume", "lua_resume(L, narg) -> int. auxresume calls lua_resume once after lua_xmove#1; sequence-position match (only 1 call in that window).", "i32 (ptr L, i32 narg)" },
    { 1170, kGV_1_5_1164953, 0x03996EE4, "verified", "lua_dump", "lua_dump(L, writer, data) -> int. Call site in str_dump immediately before \"unable to dump given function\" string load; the call str_dump makes before checking for failure.", "i32 (ptr L, ptr writer, ptr data)" },
    { 1171, kGV_1_5_1164953, 0x03992FFC, "verified", "lua_getfenv", "lua_getfenv(L, idx). Else branch of luaB_getfenv with edx=-1; matches lua_getfenv(L, -1).", "void (ptr L, i32 idx)" },
    { 1172, kGV_1_5_1164953, 0x009299AC, "verified", "luaopen_math", "luaopen_math(L). lualibs[] entry 6 — \"math\" → luaopen_math; first call in this function inlines luaI_openlib and walks through luaL_findtable/lua_getfield/lua_type/lua_settop/lua_pushvalue/lua_setfield/lua_remove/lua_insert/lua_pushcclosure.", "i32 (ptr L)" },
    { 1173, kGV_1_5_1164953, 0x00D815A4, "verified", "luaopen_table", "luaopen_table(L). lualibs[] entry 2 — \"table\" → luaopen_table.", "i32 (ptr L)" },
    { 1174, kGV_1_5_1164953, 0x007A671C, "verified", "luaopen_debug", "luaopen_debug(L). lualibs[] entry 7 — \"debug\" → luaopen_debug.", "i32 (ptr L)" },
    { 1175, kGV_1_5_1164953, 0x012DA578, "verified", "luaopen_base", "luaopen_base(L). lualibs[] entry 0 — empty name maps to luaopen_base in linit.c. Calls base_open then luaL_register on co_funcs.", "i32 (ptr L)" },
    { 1176, kGV_1_5_1164953, 0x012DAC38, "verified", "luaopen_string", "luaopen_string(L). lualibs[] entry 5 — \"string\" → luaopen_string.", "i32 (ptr L)" },
    { 1177, kGV_1_5_1164953, 0x012DAF40, "verified", "luaopen_package", "luaopen_package(L). lualibs[] entry 1 — \"package\" → luaopen_package. Inlines luaL_newmetatable at entry.", "i32 (ptr L)" },
    { 1178, kGV_1_5_1164953, 0x019DFD0C, "verified", "luaopen_os", "luaopen_os(L). lualibs[] entry 4 — \"os\" → luaopen_os. CryEngine os library has only {time, clock}; rest stripped.", "i32 (ptr L)" },
    { 1179, kGV_1_5_1164953, 0x003B70F0, "verified", "luaopen_io", "luaopen_io(L). lualibs[] entry 3 — \"io\" → luaopen_io. STUBBED: this RVA is a 3-byte `ret 0` thunk; CryEngine disabled Lua's io library wholesale.", "i32 (ptr L)" },
    { 1180, kGV_1_5_1164953, 0x0071DD7C, "verified", "index2adr", "index2adr(L, idx) -> TValue*. Internal lapi.c helper (NOT a public LUA_API); target of lua_pcall's first call. Resolves a stack index (positive/negative/pseudo) to a TValue pointer. Useful for advanced kcdx-internal work where direct stack manipulation is needed.", "ptr (ptr L, i32 idx)" },
    { 1181, kGV_1_5_1164953, 0x0071A628, "verified", "luaD_pcall", "luaD_pcall(L, func, u, old_top, ef) -> int. Internal ldo.c helper; thin wrapper around luaD_rawrunprotected with the error-handling cold path (luaF_close + luaD_seterrorobj). Called from lua_pcall + lua_cpcall + lua_load.", "i32 (ptr L, ptr func, ptr u, i64 old_top, i64 ef)" },
    { 1182, kGV_1_5_1164953, 0x0071A6A8, "verified", "luaD_rawrunprotected", "luaD_rawrunprotected(L, f, ud) -> int. Internal ldo.c helper; sets up the LUAI_TRY (setjmp/longjmp) frame and runs f(L, ud) inside it. Calls Windows amd64 _setjmp at 0x1d938e3.", "i32 (ptr L, ptr f, ptr ud)" },
    { 1183, kGV_1_5_1164953, 0x039934B4, "verified", "luaL_addstring", "luaL_addstring(B, s). Identified by JMP-scan: only function in WHGame.dll that tail-jumps to luaL_addlstring (0xB9D8E4). Body: inline strlen (or r8,-1; inc r8; cmp byte [rdx+r8], 0; jne) then jmp luaL_addlstring. No-frame leaf function (absent from .pdata) — start RVA derived by walking backward from the E9 JMP through the CC pad.", "void (ptr B, cstr s)" },
    { 1184, kGV_1_5_1164953, 0x0399838C, "verified", "luaO_pushvfstring", "luaO_pushvfstring(L, fmt, va_list) -> const char*. Internal lobject.c helper (NOT LUA_API). Called from lua_pushfstring's body at site 0x399316B (after inlined luaC_checkGC + va_list setup). Plugin shim that wants lua_pushvfstring should call this directly and handle the GC check manually (lua_pushvfstring's only meaningful wrapper logic is the luaC_checkGC + this call).", "cstr (ptr L, cstr fmt, ptr argp)" },
    { 1185, kGV_1_5_1164953, 0x0071F1F8, "verified", "lua_topointer", "lua_topointer(L, idx) -> const void*. Body matches the source switch-on-ttype: index2adr; then cmp ttype against LIGHTUSERDATA (2) → tail-jmp to lua_touserdata; else cmp against TABLE/FUNCTION/THREAD (5/6/7) → return [rax] (the GC pointer). Default returns NULL. Function lives between lua_setmetatable and other small lapi.c functions.", "ptr (ptr L, i32 idx)" },
    { 1186, kGV_1_5_1164953, 0x0071E7C0, "verified", "lua_settable", "lua_settable(L, idx). Body matches source: index2adr to get t; compute L->top-2 and L->top-1; call luaV_settable (0x71CCF0) with (L, t, L->top-2, L->top-1); L->top -= 2. The 4-arg internal call after stack-pointer arithmetic is the lua_settable signature.", "void (ptr L, i32 idx)" },
    { 1187, kGV_1_5_1164953, 0x0399614C, "verified", "luaG_runerror", "luaG_runerror(L, fmt, ...). Internal ldebug.c error-message constructor (NOT LUA_API). Body matches source: saves variadic args; checks G(L)->storedebug flag at [g+0x22]; if set calls luaO_pushvfstring(L, fmt, argp); else calls luaO_pushfstring(L, \"[Error] Lua error...\") — the storedebug-dispatch is CryEngine-specific (vendor/lua/luaconf.h adds storedebug for memory-saving debug suppression).", "" },
    { 1188, kGV_1_5_1164953, 0x03998368, "verified", "luaO_pushfstring", "luaO_pushfstring(L, fmt, ...). Internal lobject.c varargs wrapper around luaO_pushvfstring. Body: save varargs to home space, lea r8=va_list_start, tail-call luaO_pushvfstring. Used by luaG_runerror's no-debug-info path.", "" },
    { 1189, kGV_1_5_1164953, 0x014492A8, "verified", "lua_newstate", "lua_newstate(allocf, ud) -> lua_State*. PGO-fused with luaL_newstate — accepts no allocator argument (callers pass xor ecx, edx, r8d) and directly calls l_alloc (0x71E2B0). Body matches lstate.c::lua_newstate exactly: allocates 0x268 bytes (= sizeof(LG)), initializes g->storedebug=1 at +0x22, gcpause/gcstepmul=200 at +0x90/+0x94, totalbytes=0x268 at +0x78, mainthread=L at +0xb0, L->l_G=g at +0x20; final call is luaD_rawrunprotected(L, f_luaopen, NULL) at 0x014493F0. Sole caller is CScriptSystem::Init (0x1448F38).", "" },
    { 1190, kGV_1_5_1164953, 0x01449600, "verified", "luaL_openlibs", "luaL_openlibs(L). Sole xref to the static lualibs[] table at .rdata RVA 0x3B8B200 (NOT 0x3B8B210 — table actually starts 16 bytes earlier with the {\"\", luaopen_base} entry). Body is the canonical 3-call loop: lua_pushcclosure (0xB9D11C) + lua_pushstring (0x71EF54) + lua_call (0x144965C) per entry. The lualibs reference is at instr RVA 0x144960A inside this function.", "void (ptr L)" },
    { 1191, kGV_1_5_1164953, 0x00F77CA4, "verified", "f_luaopen", "f_luaopen(L, ud). Static helper inside lua_newstate. Computed from lea rdx, [rip - 0x4d174c] at 0x014493E9 immediately before call luaD_rawrunprotected. Builds the initial state: stack_init, gt(L)=luaH_new(L,0,2), registry=luaH_new(L,0,2), luaS_resize(L, MINSTRTABSIZE), luaT_init, luaX_init, luaS_fix(luaS_newliteral(L, \"not enough memory\")). NOT LUA_API — internal.", "void (ptr L, ptr ud)" },
    { 1192, kGV_1_5_1164953, 0x0071E2B0, "verified", "l_alloc", "l_alloc(ud, ptr, osize, nsize) -> void*. Default CryEngine Lua allocator. Unique direct-callee of lua_newstate (0x14492A8). Body matches lua_Alloc signature: nsize==0 → free; otherwise heap-alloc via 0x71E1B0 (CryEngine heap function). NOT LUA_API — internal allocator hook.", "ptr (ptr ud, ptr block, u64 osize, u64 nsize)" },
    { 1193, kGV_1_5_1164953, 0x039936D0, "verified", "luaL_checkudata", "luaL_checkudata(L, ud, tname) -> void*. Body matches lauxlib.c:124 exactly: lua_touserdata(L,1) → null check → lua_getmetatable(L,1) → zero check → lua_getfield(L, LUA_REGISTRYINDEX=0xFFFFD8F0, tname) → lua_rawequal(L,-2,-1) → zero check → lua_settop(L,-3) → return p; error tail: luaL_typerror(L,1,tname). LUA_REGISTRYINDEX immediate (0xFFFFD8F0) at 0x3993707 confirms identification. One caller (0x39979a0).", "ptr (ptr L, i32 ud, cstr tname)" },
    { 1194, kGV_1_5_1164953, 0x03B8AF70, "verified", "CScriptSystem_vtable", "CScriptSystem vtable RVA in .rdata. 69 slots from 0x3B8AF70..0x3B8B198. Slot [6] is 0x4D46E4 (CScriptSystem::ExecuteBuffer — the CryEngine-side caller of lua_pcall). Slots [12]/[13] both call lua_createtable (0x71F098). Slot [5]=ExecuteFile, [6]=ExecuteBuffer, [13]=CreateTable per muyuanjin's IScriptSystem.h notes. 2 xrefs total: ctor 0x1448E60 and dtor 0x39AD63C.", "" },
    { 1195, kGV_1_5_1164953, 0x01448E60, "verified", "CScriptSystem_ctor", "CScriptSystem constructor. Writes vtable 0x3B8AF70 to [rcx], second vtable 0x3B8AF50 (4-slot base-class adjustor subobject) to [rcx+8]; zeroes member slots at +0x40/+0x48/+0x50/+0x60/+0x68; calls CryEngine helper at 0x3F820C; allocates internal buffers via 0x50B994 / 0x4FDA6C (CryMemoryManager wiring). Single xref site: 0x01448DFC.", "" },
    { 1196, kGV_1_5_1164953, 0x01448F38, "verified", "CScriptSystem_Init", "CScriptSystem::Init — the Lua-boot parent function. Sole caller of both lua_newstate (0x14492A8) and luaL_openlibs (0x1449600). Sequence: malloc(0x40)+ctor of helper struct → virtual call on parent system → lua_newstate → stores L on instance and globals → SETS storedebug=0 (CryEngine memory-save; overrides the default storedebug=1 from lua_newstate) → luaL_openlibs → 3 CryEngine extension lib registrars (0x1449698, 0x1449410, 0x1449584). The boot anchor for kcdx's post-Lua-init hook.", "" },
    { 1197, kGV_1_5_1164953, 0x039AD63C, "verified", "CScriptSystem_dtor", "CScriptSystem destructor. Writes vtable back to [rcx] and [rcx+8] then tears down owned subsystems. Calls lua_close (0x39989A4) at site 0x39AD6E2 when [rdi+0x10] (the stored lua_State*) is non-null.", "" },
    { 1198, kGV_1_5_1164953, 0x039989A4, "verified", "lua_close", "lua_close(L). Body matches lstate.c source exactly: loads G(L)->mainthread via [L+0x20]+[g+0xB0] (the L = G(L)->mainthread reassignment); calls luaF_close (0x1565018) with rdx=L->stack; calls luaC_separateudata (0xFEABA8) with edx=1; resets L->ci=L->base_ci+L->base=L->top=L->ci->base+L->nCcalls=0; runs the do-while callallgcTM loop via luaD_rawrunprotected (0x71A6A8); tail-jumps to close_state (0x39987B4). Sole caller is CScriptSystem dtor at site 0x39AD6E2.", "void (ptr L)" },
    { 1199, kGV_1_5_1164953, 0x00B9CCB8, "verified", "lua_replace", "lua_replace(L, idx). Body-shape + cold-path string anchor verified. Hot path: cmp idx == LUA_ENVIRONINDEX (0xFFFFD8EF); call index2adr (0x71DD7C); cmp idx == LUA_ENVIRONINDEX again; setobj-from-L->top-1 to o; cmp idx < LUA_GLOBALSINDEX (upvalue check) + barrier call; L->top -= 16. Cold path: \"no calling environment\" string at .rdata RVA 0x4086D70 reached by cold-path lea+luaG_runerror tail-call (0x399614C). Barrier slow-path call to luaC_barrierf (0x3997070).", "void (ptr L, i32 idx)" },
    { 1200, kGV_1_5_1164953, 0x0071D118, "verified", "luaL_ref", "luaL_ref(L, t) -> int. Body matches lauxlib.c source: loads LUA_REGISTRYINDEX immediate (0xFFFFD8F0) into r15d; uses index2adr (0x71DD7C); uses internal helper 0x71DF0C for array indexing; reads freelist head from registry, copies stack-top value into freelist slot, returns the int ref. Discovered via CryEngine extension lib registrar at 0x14495C4 which calls it after lua_pushcclosure to store the C function in the registry.", "i32 (ptr L, i32 t)" },
    { 1201, kGV_1_5_1164953, 0x039987B4, "verified", "close_state", "close_state(L). Internal lstate.c helper called by lua_close at tail. Body: saves G(L) into rbp; calls luaF_close with L->stack; iterates string-table buckets freeing TString chains via 0x720338; frees buff/stack/base_ci via luaM_realloc_ (0x71E258); final tail-jmp via g->frealloc with rcx=g->ud, rdx=L, r8d=0x268 — CONFIRMS sizeof(LG)=0x268 in this build. NOT LUA_API.", "void (ptr L)" },
    { 1202, kGV_1_5_1164953, 0x03997070, "verified", "luaC_barrierf", "luaC_barrierf(L, o, v) — GC write barrier slow-path. Internal helper (NOT LUA_API). Reached from lua_replace's env-index barrier slow path at 0x228B6F7. Any shim stub that writes a GC pointer (lua_pushthread, lua_replace, lua_settable for GC values) must call this — without it incremental GC can free live objects.", "void (ptr L, ptr o, ptr v)" },
    { 1203, kGV_1_5_1164953, 0x01565018, "verified", "luaF_close", "luaF_close(L, level). Internal lfunc.c helper (NOT LUA_API). Called by lua_close to close all upvalues for the thread. Plugin stubs that need to close upvalues over a stack range (rare; mostly relevant if a plugin implements its own coroutine-like construct) can call this directly.", "void (ptr L, ptr level)" },
    { 1204, kGV_1_5_1164953, 0x00FEABA8, "verified", "luaC_separateudata", "luaC_separateudata(L, all). Internal lgc.c helper (NOT LUA_API). Called by lua_close with all=1 to separate userdata that have GC metamethods (so their __gc handlers can fire).", "u64 (ptr L, i32 all)" },
    { 1205, kGV_1_5_1164953, 0x0071E258, "verified", "luaM_realloc_", "luaM_realloc_(L, block, osize, nsize) -> void*. Internal lmem.c reallocator (NOT LUA_API). Routes all Lua memory management through g->frealloc. Used by close_state for array frees; also by lua_load (which we already have at 0x13E14D8) for ZIO buffer management.", "ptr (ptr L, ptr block, u64 osize, u64 nsize)" },
};

constexpr size_t kEntryCount = sizeof(kEntries) / sizeof(kEntries[0]);

uintptr_t WhgameBase() {
    static uintptr_t cached = 0;
    if (cached) return cached;
    HMODULE m = GetModuleHandleW(L"WHGame.dll");
    cached = reinterpret_cast<uintptr_t>(m);
    return cached;
}

}  // namespace

uintptr_t Resolve(uint64_t id) {
    uint32_t gv = kcdx::plugins::g_runtimeGameVersion;
    // Iterate the seed; ~130 entries today, linear is fine (sub-µs).
    // If the table grows past a few hundred entries we'd build a
    // hash, but the address library is bounded by how many things
    // a maintainer manually catalogues.
    for (size_t i = 0; i < kEntryCount; ++i) {
        const Entry& e = kEntries[i];
        if (e.id != id) continue;
        if (e.game_version != gv) continue;
        if (e.status == nullptr || e.status[0] == 'u' /* "unverified" */) {
            // Refuse to resolve unverified rows — callers must
            // explicitly opt in via a different API when we
            // eventually add one. For now, returning 0 is the safe
            // default: plugin authors get the same "this id is not
            // useable" signal whether the row is missing or
            // present-but-unverified.
            return 0;
        }
        if (e.rva == 0) return 0;
        uintptr_t base = WhgameBase();
        if (!base) return 0;
        return base + e.rva;
    }
    return 0;
}

size_t EntryCount() {
    return kEntryCount;
}

size_t EntryCountForRunningVersion() {
    uint32_t gv = kcdx::plugins::g_runtimeGameVersion;
    size_t n = 0;
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].game_version == gv) ++n;
    }
    return n;
}

namespace {

bool StrEq(const char* a, const char* b) {
    if (!a || !b) return false;
    while (*a && *b) {
        if (*a != *b) return false;
        ++a; ++b;
    }
    return *a == *b;
}

// ---------------------------------------------------------------------------
// Shared-name resolution (naming-namespaces.md): self > engine > other, with
// a warn-once-per-session-per-colliding-bare-name diagnostic.
//
// LAUNCH-TIME ONLY. Every helper below is reached exclusively from
// ResolveByName / ResolveSignatureByName, which run during the launch-time
// registration/apply pass and NEVER from a hook-fire / per-frame path (the
// resolved address is cached in the binding). The g_warnedCollisions dedup
// set is likewise touched only at launch. Do NOT call any of these from a
// hooked function or runtime tick — that would defeat the resident-registry
// invariant documented at g_authorTargets.
// ---------------------------------------------------------------------------

// Forward declaration of the author-target registry — its definition (with
// the resident / never-read-at-runtime invariant comment) lives in the
// registry section lower in this TU. Both blocks are in the SAME unnamed
// namespace, so this declaration binds to that definition (internal linkage).
extern std::vector<AuthorTarget> g_authorTargets;

// The engine seed scan, factored out of ResolveByName. Returns the resolved
// VA for a verified, game-version-matching seed row with this name, or 0.
uintptr_t SeedResolveAddr(const char* name) {
    uint32_t gv = kcdx::plugins::g_runtimeGameVersion;
    // Linear scan — same rationale as Resolve(): ~110 rows, sub-µs.
    // First matching row by name wins (names are unique in the seed in
    // practice; if a duplicate ever ships, the first-defined row wins).
    for (size_t i = 0; i < kEntryCount; ++i) {
        const Entry& e = kEntries[i];
        if (!StrEq(e.name, name)) continue;
        if (e.game_version != gv) continue;
        if (e.status == nullptr || e.status[0] == 'u') return 0;
        if (e.rva == 0) return 0;
        uintptr_t base = WhgameBase();
        if (!base) return 0;
        return base + e.rva;
    }
    return 0;
}

// True iff the engine seed DECLARES this name at all — independent of
// game_version / status. Collision detection is about namespace OCCUPANCY
// (who claims the name), not whether the row currently resolves: a seed row
// that exists but is unverified still occupies the engine namespace and must
// count as a shadowed owner, so the author is taught to prefix.
bool SeedHasName(const char* name) {
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (StrEq(kEntries[i].name, name)) return true;
    }
    return false;
}

// Find the author target owned by `plugin` with this bare name, or nullptr.
const AuthorTarget* FindAuthorTarget(const std::string& plugin,
                                     const char* bareName) {
    for (const AuthorTarget& t : g_authorTargets) {
        if (t.pluginName == plugin && t.bareName == bareName) return &t;
    }
    return nullptr;
}

// Find the FIRST author target with this bare name owned by some plugin OTHER
// than `excludePlugin`, or nullptr. `excludePlugin` is the calling plugin (its
// own target is the "self" tier, resolved separately and never the "other").
const AuthorTarget* FindOtherAuthorTarget(const std::string& excludePlugin,
                                          const char* bareName) {
    for (const AuthorTarget& t : g_authorTargets) {
        if (t.pluginName == excludePlugin) continue;
        if (t.bareName == bareName) return &t;
    }
    return nullptr;
}

// Resolve the address an author target locates, for the kinds resolvable
// DIRECTLY in this leaf module: Rva (locatorNum is the rva) and AddressId
// (locatorNum is a seed id → Resolve(id)). Pattern / TargetSymbol are NOT
// resolved here — turning them into a VA requires the patch engine / symbol
// table, and this module must not depend on them (the dependency runs the
// other way; see address-library.h FindResolvedAuthorTarget). Returns 0 for
// those two kinds.
//
// How a by-name Pattern/TargetSymbol author target reaches a real address:
// hook_chain::ResolveLocator, on a 0 from ResolveByName for an addressName,
// calls FindResolvedAuthorTarget; if the winner is a Pattern/TargetSymbol
// author target it feeds that target's locatorStr (the pattern string / symbol
// name) into the SAME patch::Resolve / symbol pipeline it already runs for a
// directly-set pattern / target_symbol locator. The address comes from THAT
// pipeline, not from here. We never fabricate a VA (AP2).
uintptr_t ResolveAuthorTargetAddr(const AuthorTarget& t) {
    switch (t.kind) {
        case AuthorLocatorKind::Rva: {
            if (t.locatorNum == 0) return 0;
            uintptr_t base = WhgameBase();
            if (!base) return 0;
            return base + static_cast<uintptr_t>(t.locatorNum);
        }
        case AuthorLocatorKind::AddressId:
            return Resolve(t.locatorNum);
        case AuthorLocatorKind::Pattern:
        case AuthorLocatorKind::TargetSymbol:
        default:
            return 0;
    }
}

// Already-warned bare names this session (warn-once-per-session-per-name per
// naming-namespaces.md). Shared by ResolveByName + ResolveSignatureByName so a
// bare name that collides warns ONCE total, not once per function. Launch-time
// only (see the block comment above).
std::set<std::string> g_warnedCollisions;

// Emit the once-per-session collision warning for a bare name. `winnerTier` /
// `winnerOwner` describe who won by precedence; `shadowed` lists the other
// owners. The line teaches the fix: prefix the name you didn't declare.
// A PREFIXED reference never reaches here (callers only call this on a bare
// reference that occupied >1 of {self, engine, other}).
void WarnBareCollisionOnce(const char* bareName,
                           const char* winnerTier,
                           const std::string& winnerOwner,
                           const std::string& shadowed) {
    if (g_warnedCollisions.count(bareName)) return;
    g_warnedCollisions.insert(bareName);
    LOG_WARN_KV("NAMESPACE", "bare_name_collision",
        log::KV("name", bareName),
        log::KV("resolved_to", winnerTier),
        log::KV("winner", winnerOwner),
        log::KV("shadowed", shadowed),
        log::KV("fix",
            "a bare name that exists in more than one of {your plugin, the "
            "engine, another plugin} resolves self > engine > other; prefix "
            "the one you did not declare as \"<plugin>.<name>\" (or \"kcdx."
            "<name>\" for the engine seed) to pick it explicitly and silence "
            "this warning (naming-namespaces.md)."));
}

// Detect a bare-name collision (the name occupies >1 of {self, engine, other})
// and warn once if so. Called by both resolvers AFTER they pick a winner, so
// the winner tier is known. `selfHit` = the calling plugin owns it;
// `engineHit` = the seed declares it; `otherHit` = some other plugin owns it.
// Resolution proceeds by precedence regardless of the warn.
void MaybeWarnCollision(const char* bareName, const std::string& owningPlugin,
                        bool selfHit, bool engineHit, bool otherHit,
                        const AuthorTarget* otherTarget) {
    int occupants = (selfHit ? 1 : 0) + (engineHit ? 1 : 0) + (otherHit ? 1 : 0);
    if (occupants < 2) return;

    // Winner tier + owner, by precedence (self > engine > other).
    const char* winnerTier = selfHit ? "self" : (engineHit ? "engine" : "other");
    std::string winnerOwner =
        selfHit ? owningPlugin
                : (engineHit ? std::string("kcdx") : otherTarget->pluginName);

    // List the shadowed owners (everyone the winner displaced).
    std::string shadowed;
    auto append = [&shadowed](const std::string& s) {
        if (!shadowed.empty()) shadowed += ", ";
        shadowed += s;
    };
    if (selfHit) {  // self won — engine and/or other are shadowed
        if (engineHit) append("kcdx (engine seed)");
        if (otherHit)  append(otherTarget->pluginName);
    } else if (engineHit) {  // engine won — other is shadowed (self absent)
        if (otherHit) append(otherTarget->pluginName);
    }
    WarnBareCollisionOnce(bareName, winnerTier, winnerOwner, shadowed);
}

// Split a possibly-prefixed shared name on the FIRST dot. Returns true and
// fills prefix/rest when a dot is present (prefix may be "kcdx" = the reserved
// engine root); returns false for a bare name (no dot). The engine parses on
// the dot — it is semantic, not convention (naming-namespaces.md).
bool SplitPrefixed(const char* name, std::string& prefix, std::string& rest) {
    const char* dot = nullptr;
    for (const char* p = name; *p; ++p) {
        if (*p == '.') { dot = p; break; }
    }
    if (!dot) return false;
    prefix.assign(name, dot);
    rest.assign(dot + 1);
    return true;
}

// What a bare name resolved to, by self > engine > other precedence. Computed
// ONCE by ResolveBareWinner so ResolveByName and FindResolvedAuthorTarget share
// the SAME precedence decision AND the SAME once-per-session collision warn
// (the warn fires inside ResolveBareWinner, keyed by name, so a name that
// already warned from one caller does not double-warn from the other —
// naming-namespaces.md). `winner` names the tier; `authorTarget` is the winning
// author target when the winner is Self/Other, nullptr when Engine/None.
struct BareResolution {
    enum class Tier { None, Self, Engine, Other } winner = Tier::None;
    const AuthorTarget* authorTarget = nullptr;  // non-null iff Self/Other won
};

// Resolve a BARE name (no dot) by self > engine > other precedence and emit the
// once-per-session collision warn if the name occupies >1 tier. Shared by
// ResolveByName (which turns the winner into a VA) and FindResolvedAuthorTarget
// (which hands the winning author target to hook_chain for pattern/symbol
// routing). Launch-time only.
BareResolution ResolveBareWinner(const char* name, const std::string& owner) {
    const AuthorTarget* selfTarget =
        owner.empty() ? nullptr : FindAuthorTarget(owner, name);
    bool engineHit = SeedHasName(name);
    const AuthorTarget* otherTarget = FindOtherAuthorTarget(owner, name);

    bool selfHit  = (selfTarget != nullptr);
    bool otherHit = (otherTarget != nullptr);

    MaybeWarnCollision(name, owner, selfHit, engineHit, otherHit, otherTarget);

    BareResolution r;
    if (selfTarget) {
        r.winner = BareResolution::Tier::Self;
        r.authorTarget = selfTarget;
    } else if (engineHit) {
        r.winner = BareResolution::Tier::Engine;
    } else if (otherTarget) {
        r.winner = BareResolution::Tier::Other;
        r.authorTarget = otherTarget;
    }
    return r;
}

}  // namespace

uintptr_t ResolveByName(const char* name, const char* owningPlugin) {
    if (!name || !name[0]) return 0;
    const std::string owner = owningPlugin ? owningPlugin : "";

    // --- EXPLICIT prefixed reference: "<plugin>.<name>" — never warns. -----
    std::string prefix, rest;
    if (SplitPrefixed(name, prefix, rest)) {
        if (prefix == "kcdx") {
            // Reserved engine root → the engine seed, by the unprefixed
            // engine name (rest).
            return SeedResolveAddr(rest.c_str());
        }
        // Another plugin's (or this plugin's own) target by full path —
        // unambiguous, callable from anywhere.
        const AuthorTarget* t = FindAuthorTarget(prefix, rest.c_str());
        if (!t) return 0;
        return ResolveAuthorTargetAddr(*t);
    }

    // --- BARE reference: resolve self > engine > other (shared decision). --
    BareResolution res = ResolveBareWinner(name, owner);
    switch (res.winner) {
        case BareResolution::Tier::Self:
        case BareResolution::Tier::Other:
            // An author target won. Rva / AddressId become a VA here; Pattern /
            // TargetSymbol return 0 (the caller asks FindResolvedAuthorTarget
            // and routes them through the patch/symbol pipeline — see header).
            return ResolveAuthorTargetAddr(*res.authorTarget);
        case BareResolution::Tier::Engine:
            return SeedResolveAddr(name);
        case BareResolution::Tier::None:
            return 0;
    }
    return 0;
}

const AuthorTarget* FindResolvedAuthorTarget(const char* name,
                                             const char* owningPlugin) {
    if (!name || !name[0]) return nullptr;
    const std::string owner = owningPlugin ? owningPlugin : "";

    // --- EXPLICIT prefixed reference: "<plugin>.<name>" — never warns. -----
    // "kcdx.<rest>" is the engine seed (NOT an author target) → nullptr;
    // "<plugin>.<rest>" resolves directly to that plugin's author target.
    std::string prefix, rest;
    if (SplitPrefixed(name, prefix, rest)) {
        if (prefix == "kcdx") return nullptr;
        return FindAuthorTarget(prefix, rest.c_str());
    }

    // --- BARE reference: SAME precedence + SAME collision-warn dedup as
    // ResolveByName (ResolveBareWinner is the single shared decision point).
    // Return the winning author target when Self/Other won; nullptr when the
    // engine seed won (a seed row is not an author target) or nothing matched.
    BareResolution res = ResolveBareWinner(name, owner);
    return res.authorTarget;  // non-null iff Self/Other won
}

void ForEachResolvable(ForEachResolvableCallback cb, void* userdata) {
    if (!cb) return;
    uint32_t gv = kcdx::plugins::g_runtimeGameVersion;
    uintptr_t base = WhgameBase();
    if (!base) return;
    for (size_t i = 0; i < kEntryCount; ++i) {
        const Entry& e = kEntries[i];
        if (e.game_version != gv) continue;
        if (e.status == nullptr || e.status[0] == 'u') continue;
        if (e.rva == 0) continue;
        if (!cb(e.id, e.name, e.description, base + e.rva, userdata)) return;
    }
}

const char* Describe(uint64_t id) {
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (kEntries[i].id == id) return kEntries[i].description;
    }
    return nullptr;
}

const char* DescribeByName(const char* name) {
    if (!name || !name[0]) return nullptr;
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (StrEq(kEntries[i].name, name)) return kEntries[i].description;
    }
    return nullptr;
}

namespace {

// The engine seed signature scan, factored out. Returns the row's structured
// signature ("" when the row carries none yet — AP2: never invented), or
// nullptr when the seed has no row by this name (so the caller can fall
// through to the next precedence tier). Returned regardless of game_version /
// status, like Describe() — the binder gates the ADDRESS via ResolveByName.
const char* SeedSignature(const char* name) {
    for (size_t i = 0; i < kEntryCount; ++i) {
        if (!StrEq(kEntries[i].name, name)) continue;
        return kEntries[i].signature ? kEntries[i].signature : "";
    }
    return nullptr;
}

}  // namespace

const char* ResolveSignatureByName(const char* name, const char* owningPlugin) {
    if (!name || !name[0]) return "";
    const std::string owner = owningPlugin ? owningPlugin : "";

    // --- EXPLICIT prefixed reference: "<plugin>.<name>" — never warns. -----
    std::string prefix, rest;
    if (SplitPrefixed(name, prefix, rest)) {
        if (prefix == "kcdx") {
            const char* s = SeedSignature(rest.c_str());
            return s ? s : "";
        }
        const AuthorTarget* t = FindAuthorTarget(prefix, rest.c_str());
        return t ? t->signature.c_str() : "";
    }

    // --- BARE reference: SAME order as ResolveByName (self > engine > other).
    // The signature must come from the SAME row the address came from, so the
    // ABI matches the resolved function. Share the collision dedup with
    // ResolveByName: a bare name that already warned there does not double-warn
    // here (first warn this session wins, keyed by the name).
    const AuthorTarget* selfTarget =
        owner.empty() ? nullptr : FindAuthorTarget(owner, name);
    const char* engineSig = SeedSignature(name);
    bool engineHit = (engineSig != nullptr);
    const AuthorTarget* otherTarget = FindOtherAuthorTarget(owner, name);

    bool selfHit  = (selfTarget != nullptr);
    bool otherHit = (otherTarget != nullptr);

    MaybeWarnCollision(name, owner, selfHit, engineHit, otherHit, otherTarget);

    // (1) self, (2) engine, (3) other — the signature from the winning row.
    if (selfTarget) return selfTarget->signature.c_str();
    if (engineHit)  return engineSig;
    if (otherTarget) return otherTarget->signature.c_str();
    return "";
}

// ===========================================================================
// Author-declared targets — runtime registry (storage + validation).
// ===========================================================================

namespace {

// The runtime registry of author-declared targets.
//
// INVARIANT — launch-time populate; resident; never read at runtime.
// This vector is POPULATED ONCE at launch, during plugin discovery, via
// RegisterAuthorTarget(). After discovery it is RESIDENT and READ-ONLY for
// the rest of the process lifetime. It must NEVER be consulted on a
// hook-fire / runtime-hot path: resolution happens exactly ONCE during the
// apply pass (a LATER step wires that read), the resolved address is cached
// in the binding, and the registry is never touched again while the game
// runs. Treat any read of this from a hooked function or per-frame tick as a
// bug.
std::vector<AuthorTarget> g_authorTargets;

// True iff `c` is a legal char for a shared-name component: [a-z0-9_].
// Uppercase, '.', '-', and everything else are rejected (the dot is the
// reserved canonical separator the engine parses on — naming-namespaces.md).
bool IsNameChar(char c) {
    return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_';
}

// Validate one shared-name COMPONENT (a plugin name OR a bare target name):
// non-empty, length 2..32, every char in [a-z0-9_]. `what` names the field
// in the teaching error ("plugin name" / "target name"). Does NOT apply the
// reserved-"kcdx" rule — that's plugin-name-specific, layered on by the
// caller. Returns false + fills outError on any violation.
bool ValidateNameComponent(const char* name, const char* what,
                           std::string& outError) {
    if (!name || name[0] == '\0') {
        outError = std::string(what) +
                   " is empty — must be 2-32 chars of [a-z0-9_] "
                   "(naming-namespaces.md).";
        return false;
    }
    size_t len = 0;
    for (const char* p = name; *p; ++p) {
        ++len;
        if (!IsNameChar(*p)) {
            outError = std::string(what) + " \"" + name +
                       "\" has an illegal character — only lowercase "
                       "[a-z0-9_] is allowed (no uppercase, '.', '-', or "
                       "spaces). The dot is the reserved namespace separator "
                       "(naming-namespaces.md).";
            return false;
        }
    }
    if (len < 2) {
        outError = std::string(what) + " \"" + name +
                   "\" is too short — must be 2-32 chars "
                   "(naming-namespaces.md).";
        return false;
    }
    if (len > 32) {
        outError = std::string(what) + " \"" + name +
                   "\" is too long — must be 2-32 chars "
                   "(naming-namespaces.md).";
        return false;
    }
    return true;
}

}  // namespace

bool ValidatePluginName(const char* name, std::string& outError) {
    // Charset + length first (also catches empty / over-long).
    if (!ValidateNameComponent(name, "[plugin].name", outError)) {
        return false;
    }
    // Reserved engine root: the exact value "kcdx" is the engine namespace;
    // any name starting "kcdx." would squat under the reserved root. Both are
    // a hard rejection (naming-namespaces.md: "kcdx.* is reserved for the
    // engine; [plugin].name = \"kcdx\" is rejected").
    //
    // Note: a literal "kcdx." can't actually reach here as a single component
    // because '.' fails the charset check above; we still guard the prefix
    // explicitly so the intent — and the teaching message — is unambiguous.
    if (StrEq(name, "kcdx") ||
        (name[0] == 'k' && name[1] == 'c' && name[2] == 'd' &&
         name[3] == 'x' && name[4] == '.')) {
        outError =
            "[plugin].name \"" + std::string(name) +
            "\" is reserved — the \"kcdx\" namespace (and any \"kcdx.\" "
            "prefix) belongs to the engine. Pick your own short lowercase "
            "id (naming-namespaces.md).";
        return false;
    }
    return true;
}

bool RegisterAuthorTarget(const char*       pluginName,
                          const char*       bareName,
                          AuthorLocatorKind kind,
                          const char*       locatorStr,
                          uint64_t          locatorNum,
                          const char*       signature,
                          std::string&      outError) {
    // Validate the owning plugin name as a namespace prefix (charset, length,
    // reserved-root) — a bad prefix corrupts every shared name this plugin
    // exports (naming-namespaces.md: hard manifest rejection).
    if (!ValidatePluginName(pluginName, outError)) {
        return false;
    }
    // Validate the bare name as the second half of `<plugin>.<name>` — same
    // [a-z0-9_], 2-32 component rule (the reserved-"kcdx" check is
    // prefix-only, so it does NOT apply to the bare name).
    if (!ValidateNameComponent(bareName, "target name", outError)) {
        return false;
    }

    // Validated — append. (Storage layer only: precedence resolution is a
    // later step. We do not de-dup here; collision handling is the resolver's
    // job per naming-namespaces.md, not the registry's.)
    AuthorTarget t;
    t.pluginName = pluginName;
    t.bareName   = bareName;
    t.kind       = kind;
    t.locatorStr = locatorStr ? locatorStr : "";
    t.locatorNum = locatorNum;
    t.signature  = signature ? signature : "";
    g_authorTargets.push_back(std::move(t));
    return true;
}

size_t AuthorTargetCount() {
    return g_authorTargets.size();
}

}  // namespace kcdx::address_library
