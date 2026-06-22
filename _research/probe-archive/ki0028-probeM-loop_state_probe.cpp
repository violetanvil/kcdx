// === DIAGNOSTIC (PROBE M) — KI-0028 window/display-mode loop exit-condition read ===
// See loop_state_probe.h for WHY + the outcome→meaning map. NO-RESIDUE on retire.

#include "loop_state_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "LOOP_STATE";

std::atomic<bool> g_started{false};

constexpr DWORD kPollMs = 500;   // diagnostic read cadence (user-approved, probe-only)
constexpr int   kMaxReads = 360;  // ~3 min of reads, then exit (bounded)

// The exact .data RVAs the static read pinned (FINDING-real-rva-window-mode-loop.md).
// These are WHGame.dll RVAs; the watcher resolves them to live VAs as base + RVA.
// NOT game-address RESOLUTION for production (no name exists) — a one-off diagnostic
// read of named-by-the-investigation globals; the probe is removed on retire.
constexpr uintptr_t kRva_counterA   = 0x56628d8;  // dword — loop retry counter A
constexpr uintptr_t kRva_counterB   = 0x56628dc;  // dword — loop retry counter B
constexpr uintptr_t kRva_flagByte   = 0x556d080;  // byte  — window-mgr result flag
constexpr uintptr_t kRva_flagDword  = 0x556d084;  // dword — window-mgr result flag
constexpr uintptr_t kRva_singleton0 = 0x492b890;  // qword — window-mgr singleton ptr
constexpr uintptr_t kRva_singleton1 = 0x492b8c0;  // qword — window-mgr singleton ptr

// Read a value of size N at addr without faulting on an unmapped page. The .data
// globals are in a mapped section once WHGame is loaded, but the singletons can be
// null and we only DEREFERENCE the global slot itself (always mapped), never the
// pointer it holds — so a plain read is safe. Guarded anyway (a torn early read
// during section setup would be a benign one-sample artifact, not a fault).
template <typename T>
bool SafeRead(uintptr_t va, T* out) {
    __try {
        *out = *reinterpret_cast<volatile const T*>(va);
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// The watcher thread: resolve WHGame base, then every kPollMs log the raw values of
// all six globals under one tag. Ground-truth-first — it logs WHAT IS, never tests a
// theory. A dedicated diagnostic thread (the KI-0028 boot_watch shape); removed with
// the probe on retire.
DWORD WINAPI WatcherMain(LPVOID) {
    HMODULE wh = GetModuleHandleW(L"WHGame.dll");
    if (!wh) {
        // WHGame not yet mapped at arm time is possible on the very earliest seat;
        // spin a few short waits for it rather than give up (bounded, diagnostic).
        for (int i = 0; i < 40 && !wh; ++i) {  // up to ~10s
            Sleep(250);
            wh = GetModuleHandleW(L"WHGame.dll");
        }
    }
    if (!wh) {
        LOG_ERROR_KV(kCat, "whgame_not_resolved",
            KV::BareStr("detail",
                "WHGame.dll did not resolve — the loop-state probe cannot read its "
                "globals this boot. No readings emitted."));
        return 0;
    }

    const uintptr_t base = reinterpret_cast<uintptr_t>(wh);
    LOG_INFO_KV(kCat, "probe_armed",
        KV("whgame_base", reinterpret_cast<void*>(base)),
        KV::BareStr("detail",
            "KI-0028 PROBE M — reading the window/display-mode loop's exit-condition "
            "globals (counters 0x56628d8/dc, flags 0x556d080/084, singletons "
            "0x492b890/0x492b8c0) every 500ms. Compare swap-ON vs swap-OFF: a counter "
            "stuck != -1 swap-ON but reaching -1 swap-OFF names the uncompleted task."));

    for (int reads = 0; reads < kMaxReads; ++reads) {
        Sleep(kPollMs);

        uint32_t ca = 0, cb = 0, fd = 0;
        uint8_t  fb = 0;
        uint64_t s0 = 0, s1 = 0;
        const bool okA  = SafeRead(base + kRva_counterA,   &ca);
        const bool okB  = SafeRead(base + kRva_counterB,   &cb);
        const bool okFB = SafeRead(base + kRva_flagByte,   &fb);
        const bool okFD = SafeRead(base + kRva_flagDword,  &fd);
        const bool okS0 = SafeRead(base + kRva_singleton0, &s0);
        const bool okS1 = SafeRead(base + kRva_singleton1, &s1);

        // One line per interval — a state read (the approved diagnostic poll), not a
        // hot-path log. Raw values, signed-rendered for the counters so -1 reads as -1.
        LOG_INFO_KV(kCat, "loop_state",
            KV("read",       reads),
            KV("counterA",   (int64_t)(int32_t)ca),
            KV("counterB",   (int64_t)(int32_t)cb),
            KV("flagByte",   (uint64_t)fb),
            KV("flagDword",  (uint64_t)fd),
            KV("singleton0", reinterpret_cast<void*>((uintptr_t)s0)),
            KV("singleton1", reinterpret_cast<void*>((uintptr_t)s1)),
            KV("read_ok",
                (uint64_t)((okA<<0)|(okB<<1)|(okFB<<2)|(okFD<<3)|(okS0<<4)|(okS1<<5))));
    }

    LOG_INFO_KV(kCat, "probe_done",
        KV::BareStr("detail",
            "PROBE M reached its bounded read cap — stopping the loop-state watcher."));
    return 0;
}

}  // namespace

void LoopStateProbeStart() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // already armed
    }
    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);  // detached; the watcher self-exits after its bounded reads
    } else {
        g_started.store(false, std::memory_order_release);
        LOG_ERROR_KV(kCat, "watcher_start_failed",
            KV("win32_err", (uint64_t)GetLastError()));
    }
}

}  // namespace kcdx::fs_takeover
