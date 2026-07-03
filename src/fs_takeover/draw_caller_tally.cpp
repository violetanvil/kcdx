// === DIAGNOSTIC (PROBE S / KI-0028 HOP 3) — Draw* caller attribution ===
// See draw_caller_tally.h for WHY. NO-RESIDUE on retire (with drawcall_probe).

#include "draw_caller_tally.h"

#include <windows.h>

#include <cstring>

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {
using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DRAW_PROBE";
uintptr_t g_whBase = 0;

// Resolve the module owning `addr` — basename (utf8, truncated) + module-relative
// offset. Cold path: runs once per unique caller (<= kMaxDrawCallers per table).
void ResolveModule(const void* addr, char* nameOut, int nameCap, uint64_t* offOut) {
    nameOut[0] = '\0';
    *offOut = 0;
    HMODULE mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(addr), &mod) || !mod) {
        std::strncpy(nameOut, "<unknown>", nameCap - 1);
        nameOut[nameCap - 1] = '\0';
        return;
    }
    *offOut = reinterpret_cast<uintptr_t>(addr) - reinterpret_cast<uintptr_t>(mod);
    wchar_t wpath[MAX_PATH] = {};
    if (GetModuleFileNameW(mod, wpath, MAX_PATH)) {
        const wchar_t* base = wcsrchr(wpath, L'\\');
        base = base ? base + 1 : wpath;
        int i = 0;
        for (; base[i] && i < nameCap - 1; ++i) {
            nameOut[i] = (base[i] < 128) ? static_cast<char>(base[i]) : '?';
        }
        nameOut[i] = '\0';
    } else {
        std::strncpy(nameOut, "<noname>", nameCap - 1);
        nameOut[nameCap - 1] = '\0';
    }
}
}  // namespace

void DrawCallerTallySetBase(uintptr_t whgameBase) { g_whBase = whgameBase; }

void TallyDrawCaller(DrawCallerTable& t, const char* which, void* retAddr) {
    const uintptr_t ra = reinterpret_cast<uintptr_t>(retAddr);
    const uint64_t rva = (g_whBase && ra >= g_whBase)
        ? static_cast<uint64_t>(ra - g_whBase) : static_cast<uint64_t>(ra);
    for (int i = 0; i < kMaxDrawCallers; ++i) {
        uint64_t cur = t.rva[i].load(std::memory_order_relaxed);
        bool claimed = false;
        if (cur == rva) {
            t.cnt[i].fetch_add(1, std::memory_order_relaxed);
            return;
        }
        if (cur == 0) {
            uint64_t expected = 0;
            if (t.rva[i].compare_exchange_strong(expected, rva,
                                                 std::memory_order_relaxed)) {
                claimed = true;
            } else if (expected == rva) {  // lost the claim race to the SAME rva
                t.cnt[i].fetch_add(1, std::memory_order_relaxed);
                return;
            }
        }
        if (claimed) {
            t.cnt[i].fetch_add(1, std::memory_order_relaxed);
            // Cold: resolve + publish the module fields, then log the sighting.
            ResolveModule(retAddr, t.modName[i], kDrawCallerNameLen, &t.modOff[i]);
            t.named[i].store(true, std::memory_order_release);
            LOG_WARN_KV(kCat, "draw_caller_first_seen",
                KV::BareStr("draw", which),
                KV::BareStr("module", t.modName[i]),
                KV("mod_off", t.modOff[i]),
                KV("whgame_rva", rva),
                KV::BareStr("note",
                    "new distinct Draw* caller. module+mod_off = the owning module "
                    "and module-relative offset (ASLR-stable across runs); "
                    "whgame_rva = retaddr minus WHGame base (raw when below it)."));
            return;
        }
    }
    t.overflow.fetch_add(1, std::memory_order_relaxed);
}

void DumpDrawCallers(const char* which, DrawCallerTable& t) {
    for (int i = 0; i < kMaxDrawCallers; ++i) {
        const uint64_t rva = t.rva[i].load(std::memory_order_relaxed);
        if (!rva) break;
        const bool named = t.named[i].load(std::memory_order_acquire);
        LOG_INFO_KV(kCat, "draw_caller",
            KV::BareStr("draw", which),
            KV::BareStr("module", named ? t.modName[i] : "<pending>"),
            KV("mod_off", named ? t.modOff[i] : 0),
            KV("whgame_rva", rva),
            KV("count", t.cnt[i].load(std::memory_order_relaxed)));
    }
    const uint64_t ovf = t.overflow.load(std::memory_order_relaxed);
    if (ovf) LOG_WARN_KV(kCat, "draw_caller_overflow",
                         KV::BareStr("draw", which), KV("count", ovf));
}

}  // namespace kcdx::fs_takeover
