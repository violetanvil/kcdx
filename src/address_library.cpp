#include "address_library.h"

#include <windows.h>

#include <cstddef>
#include <cstdint>

#include "kcdx/Interfaces.h"
#include "log.h"
#include "plugin_loader.h"  // for g_runtimeGameVersion

namespace kcdx::address_library {

namespace {

// One row of the address library. Seeded from
// _research/phase7-recon/address-library-seed.csv. Status maps to
// the policy in _research/phase7-recon/id-assignment-policy.md —
// only "verified" rows resolve; "unverified" rows are tracked but
// return 0 from Resolve() because we don't promise their RVAs are
// correct yet.
//
// game_version uses the kcdxMakeGameVersion encoding: KCD2 build
// 1.5.1164953 → (1<<24) | (5<<16) | (1164953 & 0xFFFF) = 0x010579D9.
struct Entry {
    uint64_t    id;
    uint32_t    game_version;   // encoded via kcdxMakeGameVersion
    uintptr_t   rva;            // 0 = no RVA known for this row
    const char* status;         // "verified" or "unverified"
    const char* name;
};

// Game-version constant for the build the seed CSV targets:
// release_1_5_1164953_841 → 1.5.1164953.
constexpr uint32_t kGV_1_5_1164953 = kcdxMakeGameVersion(1, 5, 1164953);

// Seed table — every row from address-library-seed.csv, in the
// same order. When the CSV changes, regenerate this table. The
// canonical source is _research/phase7-recon/address-library-seed.csv;
// this in-source mirror is the compiled-in fallback.
constexpr Entry kEntries[] = {
    // ----- 1000–1003: kcdx engine + yobson1 function-entry RVAs -----
    { 1000, kGV_1_5_1164953, 0x0071A5A4, "verified", "lua_pcall" },
    { 1001, kGV_1_5_1164953, 0x00667B24, "verified", "CGame_Update" },
    { 1002, kGV_1_5_1164953, 0x03993898, "verified", "luaL_loadfile" },
    { 1003, kGV_1_5_1164953, 0x00865FB4, "verified", "CGame_per_frame_ui_pump" },

    // ----- 1004–1007: in-tree mid-function call sites -----
    { 1004, kGV_1_5_1164953, 0x0056174C, "verified", "outfit_swap_callsite_aob" },
    { 1005, kGV_1_5_1164953, 0x00561745, "verified", "outfit_swap_callsite_context" },
    { 1006, kGV_1_5_1164953, 0x005605BC, "verified", "IsInCombat_callsite_26b" },
    { 1007, kGV_1_5_1164953, 0x00566040, "verified", "IsInCombat_callsite_with_stack_frame" },

    // ----- 1008–1011: muyuanjin gEnv resolver chain -----
    { 1008, kGV_1_5_1164953, 0x0086AD99, "verified", "gEnv_pConsole_mov_instruction" },
    { 1009, kGV_1_5_1164953, 0x0492B8A8, "verified", "gEnv_pConsole" },
    { 1010, kGV_1_5_1164953, 0x0492B800, "verified", "gEnv" },
    { 1011, kGV_1_5_1164953, 0x04095E58, "verified", "string_exec_autoexec_cfg" },

    // ----- 2000–2005: IConsole vtable slots ----------------------------
    // 2000 was originally seeded at 0x0100A3D4 (slot 32) by the Phase 7
    // probe; corrected 2026-05-20 to 0x00B9A2B0 (slot 33) after the
    // DISPATCH-INVESTIGATION Ghidra session showed slot 32 is the
    // script-string overload, not the function-pointer overload. See
    // _research/phase7-recon/DISPATCH-INVESTIGATION.md for the
    // three-way evidence chain.
    { 2000, kGV_1_5_1164953, 0x00B9A2B0, "verified", "IConsole_AddCommand" },
    { 2001, kGV_1_5_1164953, 0x0100955C, "verified", "IConsole_RemoveCommand" },
    { 2002, kGV_1_5_1164953, 0x007A5818, "verified", "IConsole_ExecuteString" },
    { 2003, kGV_1_5_1164953, 0x009DF818, "verified", "IConsole_GetCVar" },
    // 2004 = engine-side static wrapper that handles pConsole resolution
    // + vtable[33] dispatch internally. Plugins may prefer this if they
    // want survival-across-vtable-shuffles guarantees.
    { 2004, kGV_1_5_1164953, 0x00B99098, "verified", "IConsole_AddCommand_static_wrapper" },
    // 2005 = the script-string overload (originally mis-attributed to
    // id 2000). Kept for completeness — most plugins don't want this.
    { 2005, kGV_1_5_1164953, 0x0100A3D4, "verified", "IConsole_AddCommand_script_overload" },

    // ----- 3000–3005: vtable-index CONSTANTS, not RVAs (status stays unverified
    //                  until kcdx ships [[vtable_hook]] in a later phase) -----
    { 3000, kGV_1_5_1164953, 4,  "unverified", "IGame_CompleteInit_vtable_idx" },
    { 3001, kGV_1_5_1164953, 13, "unverified", "IScriptSystem_CreateTable_vtable_idx" },
    { 3002, kGV_1_5_1164953, 7,  "unverified", "IScriptTable_SetValueAny_vtable_idx" },
    { 3003, kGV_1_5_1164953, 16, "unverified", "IGame_GetIGameFramework_vtable_idx" },
    { 3004, kGV_1_5_1164953, 12, "unverified", "IGame_GetLongName_vtable_idx" },
    { 3005, kGV_1_5_1164953, 13, "unverified", "IGame_GetName_vtable_idx" },

    // ----- 1100-1188: Lua 5.1 C API surface (Phase 8 FIX A harvest) -----
    // RVAs of WHGame.dll's compiled Lua 5.1 LUA_API/LUALIB_API + luaopen_*
    // functions. Harvested by walking call graphs from known anchors;
    // each row's evidence is documented in the canonical seed CSV at
    // _research/phase7-recon/address-library-seed.csv (look up by id)
    // and the per-function notes in _research/phase8-fix-a/notes/.
    //
    // These IDs let kcdx and plugins call WHGame's compiled Lua
    // directly via address_library::Resolve(id), bypassing the
    // statically-linked vendor/lua. Used by FIX A to eliminate the
    // dual-Lua sentinel hazard. lua_pcall + luaL_loadfile already
    // exist at ids 1000 + 1002 — those rows authoritative, NOT
    // duplicated here.
    { 1100, kGV_1_5_1164953, 0x0071A0D8, "verified", "luaL_checktype" },
    { 1101, kGV_1_5_1164953, 0x0071A49C, "verified", "lua_insert" },
    { 1102, kGV_1_5_1164953, 0x0071A4E4, "verified", "lua_remove" },
    { 1103, kGV_1_5_1164953, 0x0071CA90, "verified", "lua_type" },
    { 1104, kGV_1_5_1164953, 0x0071D06C, "verified", "lua_rawgeti" },
    { 1105, kGV_1_5_1164953, 0x0071E46C, "verified", "lua_pushlstring" },
    { 1106, kGV_1_5_1164953, 0x0071E4D4, "verified", "lua_getmetatable" },
    { 1107, kGV_1_5_1164953, 0x0071E77C, "verified", "lua_settop" },
    { 1108, kGV_1_5_1164953, 0x0071ECEC, "verified", "lua_touserdata" },
    { 1109, kGV_1_5_1164953, 0x0071EF54, "verified", "lua_pushstring" },
    { 1110, kGV_1_5_1164953, 0x0071F098, "verified", "lua_createtable" },
    { 1111, kGV_1_5_1164953, 0x0071F100, "verified", "lua_pushvalue" },
    { 1112, kGV_1_5_1164953, 0x0071F128, "verified", "lua_setmetatable" },
    { 1113, kGV_1_5_1164953, 0x0071F4BC, "verified", "lua_next" },
    { 1114, kGV_1_5_1164953, 0x0071F6A8, "verified", "lua_tolstring" },
    { 1115, kGV_1_5_1164953, 0x0071F6FC, "verified", "lua_gettable" },
    { 1116, kGV_1_5_1164953, 0x0071FCE8, "verified", "luaC_step" }, // internal GC stepper; previously mis-tagged as lua_newthread (which is actually inlined into luaB_cocreate)
    { 1117, kGV_1_5_1164953, 0x00718464, "verified", "lua_rawget" },
    { 1118, kGV_1_5_1164953, 0x00720738, "verified", "lua_rawset" },
    { 1119, kGV_1_5_1164953, 0x0041C200, "verified", "lua_tonumber" },
    { 1120, kGV_1_5_1164953, 0x0041C230, "verified", "lua_isnumber" },
    { 1121, kGV_1_5_1164953, 0x00B9CC48, "verified", "lua_checkstack" },
    { 1122, kGV_1_5_1164953, 0x00B9CE78, "verified", "lua_typename" },
    { 1123, kGV_1_5_1164953, 0x00B9D764, "verified", "lua_objlen" },
    { 1124, kGV_1_5_1164953, 0x00B9C1AC, "verified", "lua_toboolean" },
    { 1125, kGV_1_5_1164953, 0x00B9C2A4, "verified", "lua_rawseti" },
    { 1126, kGV_1_5_1164953, 0x00B9CAFC, "verified", "lua_tointeger" },
    { 1127, kGV_1_5_1164953, 0x00B9D0C0, "verified", "lua_newuserdata" },
    { 1128, kGV_1_5_1164953, 0x00B9D11C, "verified", "lua_pushcclosure" },
    { 1129, kGV_1_5_1164953, 0x00ADB058, "verified", "luaL_findtable" },
    { 1130, kGV_1_5_1164953, 0x00ADB44C, "verified", "lua_getfield" },
    { 1131, kGV_1_5_1164953, 0x00A2D840, "verified", "lua_setfield" },
    { 1132, kGV_1_5_1164953, 0x00A2D8B8, "verified", "lua_gc" },
    { 1133, kGV_1_5_1164953, 0x00A2DA68, "verified", "lua_sethook" },
    { 1134, kGV_1_5_1164953, 0x00CAE9A8, "verified", "lua_getstack" },
    { 1135, kGV_1_5_1164953, 0x00CAEA88, "verified", "lua_getinfo" },
    { 1136, kGV_1_5_1164953, 0x01250AC4, "verified", "luaL_checknumber" },
    { 1137, kGV_1_5_1164953, 0x0144965C, "verified", "lua_call" },
    { 1138, kGV_1_5_1164953, 0x016546F8, "verified", "luaL_getmetafield" },
    { 1139, kGV_1_5_1164953, 0x013236D0, "verified", "lua_concat" },
    { 1140, kGV_1_5_1164953, 0x01323664, "verified", "luaL_pushresult" },
    { 1141, kGV_1_5_1164953, 0x013E14D8, "verified", "lua_load" },
    { 1142, kGV_1_5_1164953, 0x00B9C9BC, "verified", "luaL_checklstring" },
    { 1143, kGV_1_5_1164953, 0x00B9CA08, "verified", "luaL_optinteger" },
    { 1144, kGV_1_5_1164953, 0x00B9CAB4, "verified", "luaL_checkinteger" },
    { 1145, kGV_1_5_1164953, 0x00B9CC20, "verified", "luaL_checkstack" },
    { 1146, kGV_1_5_1164953, 0x00B9CE94, "verified", "luaL_checkany" },
    { 1147, kGV_1_5_1164953, 0x00B9D8E4, "verified", "luaL_addlstring" },
    { 1148, kGV_1_5_1164953, 0x039930A4, "verified", "lua_iscfunction" },
    { 1149, kGV_1_5_1164953, 0x039930CC, "verified", "lua_isstring" },
    { 1150, kGV_1_5_1164953, 0x039930E8, "verified", "lua_lessthan" },
    { 1151, kGV_1_5_1164953, 0x03993060, "verified", "lua_getupvalue" },
    { 1152, kGV_1_5_1164953, 0x03993134, "verified", "lua_pushfstring" },
    { 1153, kGV_1_5_1164953, 0x03993178, "verified", "lua_rawequal" },
    { 1154, kGV_1_5_1164953, 0x039931C0, "verified", "lua_setfenv" },
    { 1155, kGV_1_5_1164953, 0x03993244, "verified", "lua_setupvalue" },
    { 1156, kGV_1_5_1164953, 0x039932C0, "verified", "lua_tothread" },
    { 1157, kGV_1_5_1164953, 0x039932DC, "verified", "lua_xmove" },
    { 1158, kGV_1_5_1164953, 0x039934C8, "verified", "luaL_addvalue" },
    { 1159, kGV_1_5_1164953, 0x0399355C, "verified", "luaL_argerror" },
    { 1160, kGV_1_5_1164953, 0x03993638, "verified", "luaL_checkoption" },
    { 1161, kGV_1_5_1164953, 0x0399375C, "verified", "luaL_error" },
    { 1162, kGV_1_5_1164953, 0x03993AA4, "verified", "luaL_optlstring" },
    { 1163, kGV_1_5_1164953, 0x03993B00, "verified", "luaL_prepbuffer" },
    { 1164, kGV_1_5_1164953, 0x03993B24, "verified", "luaL_typerror" },
    { 1165, kGV_1_5_1164953, 0x03993B70, "verified", "luaL_where" },
    { 1166, kGV_1_5_1164953, 0x03996250, "verified", "lua_getlocal" },
    { 1167, kGV_1_5_1164953, 0x039962B8, "verified", "lua_setlocal" },
    { 1168, kGV_1_5_1164953, 0x0399605C, "verified", "lua_error" },
    { 1169, kGV_1_5_1164953, 0x039966A4, "verified", "lua_resume" },
    { 1170, kGV_1_5_1164953, 0x03996EE4, "verified", "lua_dump" },
    { 1171, kGV_1_5_1164953, 0x03992FFC, "verified", "lua_getfenv" },
    { 1172, kGV_1_5_1164953, 0x009299AC, "verified", "luaopen_math" },
    { 1173, kGV_1_5_1164953, 0x00D815A4, "verified", "luaopen_table" },
    { 1174, kGV_1_5_1164953, 0x007A671C, "verified", "luaopen_debug" },
    { 1175, kGV_1_5_1164953, 0x012DA578, "verified", "luaopen_base" },
    { 1176, kGV_1_5_1164953, 0x012DAC38, "verified", "luaopen_string" },
    { 1177, kGV_1_5_1164953, 0x012DAF40, "verified", "luaopen_package" },
    { 1178, kGV_1_5_1164953, 0x019DFD0C, "verified", "luaopen_os" },
    { 1179, kGV_1_5_1164953, 0x003B70F0, "verified", "luaopen_io" },  // NOTE: stubbed (3-byte ret 0) by CryEngine
    { 1180, kGV_1_5_1164953, 0x0071DD7C, "verified", "index2adr" },        // internal helper, NOT a public LUA_API
    { 1181, kGV_1_5_1164953, 0x0071A628, "verified", "luaD_pcall" },           // internal helper, NOT a public LUA_API
    { 1182, kGV_1_5_1164953, 0x0071A6A8, "verified", "luaD_rawrunprotected" }, // internal helper, NOT a public LUA_API
    { 1183, kGV_1_5_1164953, 0x039934B4, "verified", "luaL_addstring" },
    { 1184, kGV_1_5_1164953, 0x0399838C, "verified", "luaO_pushvfstring" },    // internal helper used by lua_pushfstring (lua_pushvfstring inlined)
    { 1185, kGV_1_5_1164953, 0x0071F1F8, "verified", "lua_topointer" },
    { 1186, kGV_1_5_1164953, 0x0071E7C0, "verified", "lua_settable" },
    { 1187, kGV_1_5_1164953, 0x0399614C, "verified", "luaG_runerror" },     // internal ldebug.c helper
    { 1188, kGV_1_5_1164953, 0x03998368, "verified", "luaO_pushfstring" },  // internal lobject.c helper
    { 1189, kGV_1_5_1164953, 0x014492A8, "verified", "lua_newstate" },      // PGO-fused with luaL_newstate (hardcoded l_alloc); sole caller is CScriptSystem::Init
    { 1190, kGV_1_5_1164953, 0x01449600, "verified", "luaL_openlibs" },     // sole xref to lualibs[] @ .rdata 0x3B8B200
    { 1191, kGV_1_5_1164953, 0x00F77CA4, "verified", "f_luaopen" },     // internal: static helper inside lua_newstate (calls stack_init, luaH_new x2, luaS_resize, etc.)
    { 1192, kGV_1_5_1164953, 0x0071E2B0, "verified", "l_alloc" },       // internal: default CryEngine Lua allocator (the lua_Alloc passed to lua_newstate)
    { 1193, kGV_1_5_1164953, 0x039936D0, "verified", "luaL_checkudata" },
    { 1194, kGV_1_5_1164953, 0x03B8AF70, "verified", "CScriptSystem_vtable" },   // 69 slots; slot[6]=ExecuteBuffer (caller of lua_pcall), [13]=CreateTable
    { 1195, kGV_1_5_1164953, 0x01448E60, "verified", "CScriptSystem_ctor" },
    { 1196, kGV_1_5_1164953, 0x01448F38, "verified", "CScriptSystem_Init" },     // lua-boot anchor; sole caller of lua_newstate + luaL_openlibs
    { 1197, kGV_1_5_1164953, 0x039AD63C, "verified", "CScriptSystem_dtor" },
    { 1198, kGV_1_5_1164953, 0x039989A4, "verified", "lua_close" },              // sole caller is CScriptSystem dtor at 0x39AD6E2
    { 1199, kGV_1_5_1164953, 0x00B9CCB8, "verified", "lua_replace" },            // verified via "no calling environment" string anchor
    { 1200, kGV_1_5_1164953, 0x0071D118, "verified", "luaL_ref" },               // discovered via CryEngine extension lib registrar 0x14495C4
    { 1201, kGV_1_5_1164953, 0x039987B4, "verified", "close_state" },        // internal lstate.c helper; tail-called from lua_close
    { 1202, kGV_1_5_1164953, 0x03997070, "verified", "luaC_barrierf" },          // GC barrier slow-path; needed by shim stubs that write GC pointers
    { 1203, kGV_1_5_1164953, 0x01565018, "verified", "luaF_close" },             // internal lfunc.c helper
    { 1204, kGV_1_5_1164953, 0x00FEABA8, "verified", "luaC_separateudata" },     // internal lgc.c helper
    { 1205, kGV_1_5_1164953, 0x0071E258, "verified", "luaM_realloc_" },          // internal lmem.c reallocator (routes all Lua memory through g->frealloc)
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
    // Iterate the seed; small N (~22 today), so linear is fine.
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

}  // namespace

uintptr_t ResolveByName(const char* name) {
    if (!name || !name[0]) return 0;
    uint32_t gv = kcdx::plugins::g_runtimeGameVersion;
    // Linear scan — same rationale as Resolve(): ~110 rows, sub-µs.
    // First matching row by name wins (names should be unique in
    // practice; if a duplicate ever ships, the first-defined row
    // takes precedence and a maintainer warning lands in the diff).
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
        if (!cb(e.id, e.name, base + e.rva, userdata)) return;
    }
}

}  // namespace kcdx::address_library
