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
