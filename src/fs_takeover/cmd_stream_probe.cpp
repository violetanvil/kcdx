// === DIAGNOSTIC (PROBE Z10 / KI-0028 HOP 8) — render command-stream interpreter ===
// See cmd_stream_probe.h for WHY. NO-RESIDUE on retire (file + Z10 arm/tally calls + CMake).

#include "cmd_stream_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "RENDER_TRACE";

// FUN_18251bb1c(p1, p2): the command-stream interpreter. Stream fields on p2:
constexpr uint32_t  kRvaCmdInterp = 0x251bb1c;
constexpr uintptr_t kCmdLenOff    = 0x10;  // [p2+0x10] = stream length (bytes)
constexpr uintptr_t kCmdBaseOff   = 0x18;  // [p2+0x18] = stream base ptr
constexpr uint64_t  kMaxPerSite   = 6;     // log the first N invocations only

std::atomic<uint64_t> g_invokes{0};
std::atomic<uint64_t> g_lenZero{0};  // streams with length == 0 (empty)
std::atomic<uint64_t> g_lenMax{0};
std::atomic<uint64_t> g_lenLast{0};

// SEH-guarded engine read (a torn/freed stream must not fault the game).
uint64_t SafeReadU64(uintptr_t addr) {
    uint64_t v = 0;
    if (addr) __try {
        v = *reinterpret_cast<volatile uint64_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) { v = 0; }
    return v;
}

using CmdInterpFn_t = void(__fastcall*)(void* p1, void* p2);
CmdInterpFn_t g_origCmdInterp = nullptr;

void __fastcall HookedCmdInterp(void* p1, void* p2) {
    const uint64_t inv = g_invokes.fetch_add(1, std::memory_order_relaxed);
    const uintptr_t s = reinterpret_cast<uintptr_t>(p2);
    const uint64_t len  = SafeReadU64(s + kCmdLenOff);
    const uint64_t base = SafeReadU64(s + kCmdBaseOff);
    const uint64_t firstOp = base ? (SafeReadU64(base) & 0xffffffff) : 0xffffffffULL;
    g_lenLast.store(len, std::memory_order_relaxed);
    if (len == 0) g_lenZero.fetch_add(1, std::memory_order_relaxed);
    uint64_t prev = g_lenMax.load(std::memory_order_relaxed);  // track max (relaxed CAS)
    while (len > prev &&
           !g_lenMax.compare_exchange_weak(prev, len, std::memory_order_relaxed)) {}
    if (inv < kMaxPerSite) {
        LOG_WARN_KV(kCat, "cmd_stream",
            KV::BareStr("site", "cmd_interp_251bb1c"),
            KV("inv",      inv),
            KV("len",      len),
            KV("base",     base),
            KV("first_op", firstOp),
            KV::BareStr("note",
                "render command-stream at interp entry. len=0 => empty stream (no "
                "opcodes -> no pass submits, no item build) = the black-arm signature "
                "if it holds swap-ON; len>0 => stream present, cross-check the opcode-4 "
                "(pass submit) count against passa_dispatch invokes."));
    }
    g_origCmdInterp(p1, p2);
}

}  // namespace

int CmdStreamProbeArm(unsigned long long whgameBase) {
    void* target = reinterpret_cast<void*>(
        static_cast<uintptr_t>(whgameBase) + kRvaCmdInterp);
    MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void*>(&HookedCmdInterp),
                                reinterpret_cast<void**>(&g_origCmdInterp));
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "hook_create_failed",
            KV::BareStr("site", "cmd_interp"), KV("mh_status", (uint64_t)s));
        return 0;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "hook_enable_failed", KV::BareStr("site", "cmd_interp"));
        return 0;
    }
    return 1;
}

void CmdStreamProbeEmitTally() {
    LOG_INFO_KV(kCat, "cmd_stream_tally",
        KV("invokes",  g_invokes.load()),
        KV("len_zero", g_lenZero.load()),
        KV("len_max",  g_lenMax.load()),
        KV("len_last", g_lenLast.load()),
        KV::BareStr("note",
            "HOP-8: render command-stream interpreter. invokes>0 + len_zero=all (or "
            "len_max=0) => the stream is EMPTY every frame swap-ON => the frontier is "
            "the stream PRODUCER (scene->command-buffer build, FUN_18252a228 up); "
            "len_max>0 swap-ON => the stream is populated, cross-check the pass-submit "
            "(opcode-4) count = passa_dispatch invokes to see if the build opcodes are "
            "what's missing."));
}

}  // namespace kcdx::fs_takeover
