// === DIAGNOSTIC (PROBE S / KI-0028 HOP 3) — Draw* caller attribution ===
// See draw_caller_tally.h for WHY. NO-RESIDUE on retire (with drawcall_probe).

#include "draw_caller_tally.h"

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {
using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DRAW_PROBE";
uintptr_t g_whBase = 0;
}  // namespace

void DrawCallerTallySetBase(uintptr_t whgameBase) { g_whBase = whgameBase; }

uint64_t TallyDrawCaller(DrawCallerTable& t, void* retAddr) {
    const uintptr_t ra = reinterpret_cast<uintptr_t>(retAddr);
    const uint64_t rva = (g_whBase && ra >= g_whBase)
        ? static_cast<uint64_t>(ra - g_whBase) : static_cast<uint64_t>(ra);
    for (int i = 0; i < kMaxDrawCallers; ++i) {
        uint64_t cur = t.rva[i].load(std::memory_order_relaxed);
        if (cur == rva) { t.cnt[i].fetch_add(1, std::memory_order_relaxed); return 0; }
        if (cur == 0) {
            uint64_t expected = 0;
            if (t.rva[i].compare_exchange_strong(expected, rva,
                                                 std::memory_order_relaxed)) {
                t.cnt[i].fetch_add(1, std::memory_order_relaxed);
                return rva;  // new caller claimed — the caller logs it once
            }
            if (expected == rva) {  // lost the claim race to the SAME rva
                t.cnt[i].fetch_add(1, std::memory_order_relaxed);
                return 0;
            }
        }
    }
    t.overflow.fetch_add(1, std::memory_order_relaxed);
    return 0;
}

void DumpDrawCallers(const char* which, DrawCallerTable& t) {
    for (int i = 0; i < kMaxDrawCallers; ++i) {
        const uint64_t rva = t.rva[i].load(std::memory_order_relaxed);
        if (!rva) break;
        LOG_INFO_KV(kCat, "draw_caller",
            KV::BareStr("draw", which),
            KV("caller_rva", rva),
            KV("count", t.cnt[i].load(std::memory_order_relaxed)));
    }
    const uint64_t ovf = t.overflow.load(std::memory_order_relaxed);
    if (ovf) LOG_WARN_KV(kCat, "draw_caller_overflow",
                         KV::BareStr("draw", which), KV("count", ovf));
}

}  // namespace kcdx::fs_takeover
