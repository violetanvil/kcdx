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
    { 1000, kGV_1_5_1164953, 0x0071A5A4, "verified", "lua-pcall" },
    { 1001, kGV_1_5_1164953, 0x00667B24, "verified", "cgame-update" },
    { 1002, kGV_1_5_1164953, 0x03993898, "verified", "luaL-loadfile" },
    { 1003, kGV_1_5_1164953, 0x00865FB4, "verified", "cgame-update-callee-ui-pump" },

    // ----- 1004–1007: in-tree mid-function call sites -----
    { 1004, kGV_1_5_1164953, 0x0056174C, "verified", "outfit-swap-aob" },
    { 1005, kGV_1_5_1164953, 0x00561745, "verified", "outfit-swap-context" },
    { 1006, kGV_1_5_1164953, 0x005605BC, "verified", "isincombat-callsite-26b" },
    { 1007, kGV_1_5_1164953, 0x00566040, "verified", "isincombat-call-w-stack-frame" },

    // ----- 1008–1011: muyuanjin gEnv resolver chain -----
    { 1008, kGV_1_5_1164953, 0x0086AD99, "verified", "genv-pconsole-mov" },
    { 1009, kGV_1_5_1164953, 0x0492B8A8, "verified", "genv-pconsole-ptr" },
    { 1010, kGV_1_5_1164953, 0x0492B800, "verified", "genv-base" },
    { 1011, kGV_1_5_1164953, 0x04095E58, "verified", "anchor-exec-autoexec-cfg" },

    // ----- 2000–2005: IConsole vtable slots ----------------------------
    // 2000 was originally seeded at 0x0100A3D4 (slot 32) by the Phase 7
    // probe; corrected 2026-05-20 to 0x00B9A2B0 (slot 33) after the
    // DISPATCH-INVESTIGATION Ghidra session showed slot 32 is the
    // script-string overload, not the function-pointer overload. See
    // _research/phase7-recon/DISPATCH-INVESTIGATION.md for the
    // three-way evidence chain.
    { 2000, kGV_1_5_1164953, 0x00B9A2B0, "verified", "iconsole-addcommand" },
    { 2001, kGV_1_5_1164953, 0x0100955C, "verified", "iconsole-removecommand" },
    { 2002, kGV_1_5_1164953, 0x007A5818, "verified", "iconsole-executestring" },
    { 2003, kGV_1_5_1164953, 0x009DF818, "verified", "iconsole-getcvar" },
    // 2004 = engine-side static wrapper that handles pConsole resolution
    // + vtable[33] dispatch internally. Plugins may prefer this if they
    // want survival-across-vtable-shuffles guarantees.
    { 2004, kGV_1_5_1164953, 0x00B99098, "verified", "iconsole-addcommand-static-wrapper" },
    // 2005 = the script-string overload (originally mis-attributed to
    // id 2000). Kept for completeness — most plugins don't want this.
    { 2005, kGV_1_5_1164953, 0x0100A3D4, "verified", "iconsole-addcommand-script-overload" },

    // ----- 3000–3005: vtable-index CONSTANTS, not RVAs (status stays unverified
    //                  until kcdx ships [[vtable_hook]] in a later phase) -----
    { 3000, kGV_1_5_1164953, 4,  "unverified", "igame-completeinit-vtable-idx" },
    { 3001, kGV_1_5_1164953, 13, "unverified", "iscriptsystem-createtable-vtable-idx" },
    { 3002, kGV_1_5_1164953, 7,  "unverified", "iscripttable-setvalueany-vtable-idx" },
    { 3003, kGV_1_5_1164953, 16, "unverified", "igame-getigameframework-vtable-idx" },
    { 3004, kGV_1_5_1164953, 12, "unverified", "igame-getlongname-vtable-idx" },
    { 3005, kGV_1_5_1164953, 13, "unverified", "igame-getname-vtable-idx" },

    // ----- 1100-1184: Lua 5.1 C API surface (Phase 8 FIX A harvest) -----
    // RVAs of WHGame.dll's compiled Lua 5.1 LUA_API/LUALIB_API + luaopen_*
    // functions. Harvested by walking call graphs from known anchors;
    // each row's evidence is documented in the canonical seed CSV at
    // _research/phase7-recon/address-library-seed.csv (look up by id)
    // and the per-function notes in _research/phase8-fix-a/notes/.
    //
    // These IDs let kcdx and plugins call WHGame's compiled Lua
    // directly via address_library::Resolve(id), bypassing the
    // statically-linked vendor/lua. Used by FIX A to eliminate the
    // dual-Lua sentinel hazard. lua-pcall + luaL-loadfile already
    // exist at ids 1000 + 1002 — those rows authoritative, NOT
    // duplicated here.
    //
    // ID range layout (alphabetized within sub-ranges):
    //   1100-1141  =  lua_* (low-RVA cluster: 0x0071XXXX..0x0071FXXX)
    //   1119-1120  =  lua_tonumber / lua_isnumber (0x0041CXXX)
    //   1121-1128  =  lua_*  / luaL_* (0x00B9CXXX..0x00B9DXXX)
    //   1129-1140  =  high-RVA scatter (0x00ADBxxx-0x01649xxx)
    //   1141       =  lua_load (0x013E14D8 — luaB_loadstring's lua_load call)
    //   1142-1148  =  luaL_* helpers (0x00B9CXXX-0x00B9DXXX cluster)
    //   1149-1173  =  high-RVA lua_*/luaL_* (0x039930XX range — most luaB internals)
    //   1174-1181  =  luaopen_* (lualibs[] static table at .rdata 0x3B8B210)
    //   1182-1184  =  internal helpers (index2adr, luaD_pcall, luaD_rawrunprotected)
    { 1100, kGV_1_5_1164953, 0x0071A0D8, "verified", "luaL-checktype" },
    { 1101, kGV_1_5_1164953, 0x0071A49C, "verified", "lua-insert" },
    { 1102, kGV_1_5_1164953, 0x0071A4E4, "verified", "lua-remove" },
    { 1103, kGV_1_5_1164953, 0x0071CA90, "verified", "lua-type" },
    { 1104, kGV_1_5_1164953, 0x0071D06C, "verified", "lua-rawgeti" },
    { 1105, kGV_1_5_1164953, 0x0071E46C, "verified", "lua-pushlstring" },
    { 1106, kGV_1_5_1164953, 0x0071E4D4, "verified", "lua-getmetatable" },
    { 1107, kGV_1_5_1164953, 0x0071E77C, "verified", "lua-settop" },
    { 1108, kGV_1_5_1164953, 0x0071ECEC, "verified", "lua-touserdata" },
    { 1109, kGV_1_5_1164953, 0x0071EF54, "verified", "lua-pushstring" },
    { 1110, kGV_1_5_1164953, 0x0071F098, "verified", "lua-createtable" },
    { 1111, kGV_1_5_1164953, 0x0071F100, "verified", "lua-pushvalue" },
    { 1112, kGV_1_5_1164953, 0x0071F128, "verified", "lua-setmetatable" },
    { 1113, kGV_1_5_1164953, 0x0071F4BC, "verified", "lua-next" },
    { 1114, kGV_1_5_1164953, 0x0071F6A8, "verified", "lua-tolstring" },
    { 1115, kGV_1_5_1164953, 0x0071F6FC, "verified", "lua-gettable" },
    { 1116, kGV_1_5_1164953, 0x0071FCE8, "verified", "luaC-step" }, // CORRECTED: was tagged "lua-newthread" but body analysis shows this is luaC_step (internal GC). lua_newthread is now UNIDENTIFIED (likely inlined).
    { 1117, kGV_1_5_1164953, 0x00718464, "verified", "lua-rawget" },
    { 1118, kGV_1_5_1164953, 0x00720738, "verified", "lua-rawset" },
    { 1119, kGV_1_5_1164953, 0x0041C200, "verified", "lua-tonumber" },
    { 1120, kGV_1_5_1164953, 0x0041C230, "verified", "lua-isnumber" },
    { 1121, kGV_1_5_1164953, 0x00B9CC48, "verified", "lua-checkstack" },
    { 1122, kGV_1_5_1164953, 0x00B9CE78, "verified", "lua-typename" },
    { 1123, kGV_1_5_1164953, 0x00B9D764, "verified", "lua-objlen" },
    { 1124, kGV_1_5_1164953, 0x00B9C1AC, "verified", "lua-toboolean" },
    { 1125, kGV_1_5_1164953, 0x00B9C2A4, "verified", "lua-rawseti" },
    { 1126, kGV_1_5_1164953, 0x00B9CAFC, "verified", "lua-tointeger" },
    { 1127, kGV_1_5_1164953, 0x00B9D0C0, "verified", "lua-newuserdata" },
    { 1128, kGV_1_5_1164953, 0x00B9D11C, "verified", "lua-pushcclosure" },
    { 1129, kGV_1_5_1164953, 0x00ADB058, "verified", "luaL-findtable" },
    { 1130, kGV_1_5_1164953, 0x00ADB44C, "verified", "lua-getfield" },
    { 1131, kGV_1_5_1164953, 0x00A2D840, "verified", "lua-setfield" },
    { 1132, kGV_1_5_1164953, 0x00A2D8B8, "verified", "lua-gc" },
    { 1133, kGV_1_5_1164953, 0x00A2DA68, "verified", "lua-sethook" },
    { 1134, kGV_1_5_1164953, 0x00CAE9A8, "verified", "lua-getstack" },
    { 1135, kGV_1_5_1164953, 0x00CAEA88, "verified", "lua-getinfo" },
    { 1136, kGV_1_5_1164953, 0x01250AC4, "verified", "luaL-checknumber" },
    { 1137, kGV_1_5_1164953, 0x0144965C, "verified", "lua-call" },
    { 1138, kGV_1_5_1164953, 0x016546F8, "verified", "luaL-getmetafield" },
    { 1139, kGV_1_5_1164953, 0x013236D0, "verified", "lua-concat" },
    { 1140, kGV_1_5_1164953, 0x01323664, "verified", "luaL-pushresult" },
    { 1141, kGV_1_5_1164953, 0x013E14D8, "verified", "lua-load" },
    // IDs 1143 and 1156 are intentionally skipped — they held
    // placeholder rows during seed development that were removed.
    // Per the policy in id-assignment-policy.md ("ids are append-only,
    // never renumber"), the gaps are permanent.
    { 1142, kGV_1_5_1164953, 0x00B9C9BC, "verified", "luaL-checklstring" },
    { 1144, kGV_1_5_1164953, 0x00B9CA08, "verified", "luaL-optinteger" },
    { 1145, kGV_1_5_1164953, 0x00B9CAB4, "verified", "luaL-checkinteger" },
    { 1146, kGV_1_5_1164953, 0x00B9CC20, "verified", "luaL-checkstack" },
    { 1147, kGV_1_5_1164953, 0x00B9CE94, "verified", "luaL-checkany" },
    { 1148, kGV_1_5_1164953, 0x00B9D8E4, "verified", "luaL-addlstring" },
    { 1149, kGV_1_5_1164953, 0x039930A4, "verified", "lua-iscfunction" },
    { 1150, kGV_1_5_1164953, 0x039930CC, "verified", "lua-isstring" },
    { 1151, kGV_1_5_1164953, 0x039930E8, "verified", "lua-lessthan" },
    { 1152, kGV_1_5_1164953, 0x03993060, "verified", "lua-getupvalue" },
    { 1153, kGV_1_5_1164953, 0x03993134, "verified", "lua-pushfstring" },
    { 1154, kGV_1_5_1164953, 0x03993178, "verified", "lua-rawequal" },
    { 1155, kGV_1_5_1164953, 0x039931C0, "verified", "lua-setfenv" },
    { 1157, kGV_1_5_1164953, 0x03993244, "verified", "lua-setupvalue" },
    { 1158, kGV_1_5_1164953, 0x039932C0, "verified", "lua-tothread" },
    { 1159, kGV_1_5_1164953, 0x039932DC, "verified", "lua-xmove" },
    { 1160, kGV_1_5_1164953, 0x039934C8, "verified", "luaL-addvalue" },
    { 1161, kGV_1_5_1164953, 0x0399355C, "verified", "luaL-argerror" },
    { 1162, kGV_1_5_1164953, 0x03993638, "verified", "luaL-checkoption" },
    { 1163, kGV_1_5_1164953, 0x0399375C, "verified", "luaL-error" },
    { 1164, kGV_1_5_1164953, 0x03993AA4, "verified", "luaL-optlstring" },
    { 1165, kGV_1_5_1164953, 0x03993B00, "verified", "luaL-prepbuffer" },
    { 1166, kGV_1_5_1164953, 0x03993B24, "verified", "luaL-typerror" },
    { 1167, kGV_1_5_1164953, 0x03993B70, "verified", "luaL-where" },
    { 1168, kGV_1_5_1164953, 0x03996250, "verified", "lua-getlocal" },
    { 1169, kGV_1_5_1164953, 0x039962B8, "verified", "lua-setlocal" },
    { 1170, kGV_1_5_1164953, 0x0399605C, "verified", "lua-error" },
    { 1171, kGV_1_5_1164953, 0x039966A4, "verified", "lua-resume" },
    { 1172, kGV_1_5_1164953, 0x03996EE4, "verified", "lua-dump" },
    { 1173, kGV_1_5_1164953, 0x03992FFC, "verified", "lua-getfenv" },
    { 1174, kGV_1_5_1164953, 0x009299AC, "verified", "luaopen-math" },
    { 1175, kGV_1_5_1164953, 0x00D815A4, "verified", "luaopen-table" },
    { 1176, kGV_1_5_1164953, 0x007A671C, "verified", "luaopen-debug" },
    { 1177, kGV_1_5_1164953, 0x012DA578, "verified", "luaopen-base" },
    { 1178, kGV_1_5_1164953, 0x012DAC38, "verified", "luaopen-string" },
    { 1179, kGV_1_5_1164953, 0x012DAF40, "verified", "luaopen-package" },
    { 1180, kGV_1_5_1164953, 0x019DFD0C, "verified", "luaopen-os" },
    { 1181, kGV_1_5_1164953, 0x003B70F0, "verified", "luaopen-io" },  // NOTE: stubbed (3-byte ret 0) by CryEngine
    { 1182, kGV_1_5_1164953, 0x0071DD7C, "verified", "lua-index2adr" },        // internal helper, NOT a public LUA_API
    { 1183, kGV_1_5_1164953, 0x0071A628, "verified", "luaD-pcall" },           // internal helper, NOT a public LUA_API
    { 1184, kGV_1_5_1164953, 0x0071A6A8, "verified", "luaD-rawrunprotected" }, // internal helper, NOT a public LUA_API
    { 1185, kGV_1_5_1164953, 0x039934B4, "verified", "luaL-addstring" },
    { 1186, kGV_1_5_1164953, 0x0399838C, "verified", "luaO-pushvfstring" },    // internal helper used by lua_pushfstring (lua_pushvfstring inlined)
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

}  // namespace kcdx::address_library
