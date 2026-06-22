// === DIAGNOSTIC (PROBE R) — KI-0028 shader-cache VALIDATION gate ===============
// See dispatch_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "dispatch_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"
#include "../pe_helpers.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "DISPATCH_PROBE";

// Scratch probe RVAs (NOT production resolution — the no-hardcoded-addresses.md
// probe/diagnostic exception). SOURCE: _research/ki0028-cshaderman-pso-consumer-
// recon/FINDINGS.md, body-read against WHGame.dll release_1_5_1164953_841, image
// base 0x180000000.
//   0xb04984 = FUN_180b04984 = the lookupdata.bin LOADER; its return IS the
//              disable-cache gate bool (0 = reject => cache disabled).
//   0xb04478 = FUN_180b04478 = the cache validate DRIVER; calls the loader,
//              fires "Disabling read-only shader cache!" on a 0 return.
constexpr uint32_t kRvaLoader = 0xb04984;
constexpr uint32_t kRvaDriver = 0xb04478;

std::atomic<bool> g_armed{false};

// Per-call tallies (relaxed — diagnostic counters, no happens-before edge).
std::atomic<uint64_t> g_loaderCalls{0};
std::atomic<uint64_t> g_loaderReject{0};   // return 0 (cache disabled)
std::atomic<uint64_t> g_loaderAccept{0};   // return nonzero (cache used)
std::atomic<uint64_t> g_driverCalls{0};

// --- FUN_180b04984 lookupdata.bin loader after-hook ---
// ABI (body-read): undefined8 (longlong p1, undefined8 p2, undefined8 p3,
// char p4). 4-arg fastcall; the return is the gate bool. Mirror it exactly so the
// trampoline forwards every arg untouched.
using LoaderFn_t = uint64_t(__fastcall*)(void* p1, void* p2, void* p3, char p4);
LoaderFn_t g_origLoader = nullptr;

uint64_t __fastcall HookedLoader(void* p1, void* p2, void* p3, char p4) {
    const uint64_t n = g_loaderCalls.fetch_add(1) + 1;
    const uint64_t ret = g_origLoader(p1, p2, p3, p4);
    if (ret == 0) ++g_loaderReject; else ++g_loaderAccept;
    // The decisive line: ret==0 => the cache was REJECTED this call (=> disabled).
    // p4 is the per-call "is this the %USER% copy" flag the body ANDs into the
    // open flags (~-(p4!=0) & 0x10010004) — log it so the ENGINE vs USER arm is
    // distinguishable. Every call logged (this is NOT a hot path — the cache is
    // validated a handful of times at shader-system init).
    LOG_DEBUG_KV(kCat, "lookupdata_loader",
        KV("call_n",   n),
        KV("ret",      ret),
        KV("rejected", ret == 0 ? 1 : 0),
        KV("user_flag", (uint64_t)(uint8_t)p4));
    return ret;
}

// --- FUN_180b04478 validate driver after-hook ---
// ABI (body-read context FUN_180b033a0 calls it `FUN_180b04478(base + 0xf00)`):
// a single-pointer member-ish call. Return is not the gate (the loader's is); we
// only need REACH — was validation driven at all under the swap. Mirror as a
// 1-arg fastcall returning void (the caller discards any return).
using DriverFn_t = void(__fastcall*)(void* p1);
DriverFn_t g_origDriver = nullptr;

void __fastcall HookedDriver(void* p1) {
    const uint64_t n = g_driverCalls.fetch_add(1) + 1;
    // Log BEFORE forwarding — the driver runs the loader internally, so the
    // loader lines for THIS validation appear after this "driver_enter".
    LOG_DEBUG_KV(kCat, "driver_enter",
        KV("call_n", n),
        KV::BareStr("detail",
            "shader-cache validate driver reached; the lookupdata_loader line(s) "
            "that follow are this validation's gate verdict."));
    g_origDriver(p1);
}

bool HookRva(const kcdx::pe::ModuleView& whgame, uint32_t rva, void* detour,
             void** origOut, const char* label) {
    void* target = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(whgame.base) + rva);
    MH_STATUS s = MH_CreateHook(target, detour, origOut);
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "hook_create_failed",
            KV::BareStr("label", label),
            KV("rva", (uint64_t)rva),
            KV("mh_status", (uint64_t)s));
        return false;
    }
    if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "hook_enable_failed",
            KV::BareStr("label", label), KV("rva", (uint64_t)rva));
        return false;
    }
    LOG_INFO_KV(kCat, "hook_armed",
        KV::BareStr("label", label),
        KV("target", reinterpret_cast<uintptr_t>(target)));
    return true;
}

// Bounded summary watcher — same sanctioned diagnostic-poll shape as PROBE P/K
// (one dedicated thread, nothing suspended). Flushes the tallies so the A/B diff
// is readable without per-call flooding; stops after ~2 min.
constexpr DWORD kSummaryMs = 3000;
std::atomic<bool> g_watcherStarted{false};

DWORD WINAPI SummaryMain(LPVOID) {
    LOG_INFO_KV(kCat, "watcher_started",
        KV::BareStr("detail",
            "KI-0028 shader-cache-validation watcher armed. loader_reject>0 "
            "swap-ON + ==0 swap-OFF => cache disabled by the swap (the wedge). "
            "loader_accept>0 swap-ON => cache VALIDATES, gate is past validation. "
            "loader_calls==0 swap-ON => validation never reached, widen up."));
    for (int reads = 0; reads < 40; ++reads) {  // ~2 min
        Sleep(kSummaryMs);
        LOG_INFO_KV(kCat, "summary",
            KV("loader_calls",  g_loaderCalls.load()),
            KV("loader_reject", g_loaderReject.load()),
            KV("loader_accept", g_loaderAccept.load()),
            KV("driver_calls",  g_driverCalls.load()));
    }
    LOG_INFO_KV(kCat, "watcher_done",
        KV::BareStr("detail", "40 summary reads taken; stopping the watcher."));
    return 0;
}

void StartWatcherOnce() {
    bool expected = false;
    if (!g_watcherStarted.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;
    }
    HANDLE h = CreateThread(nullptr, 0, SummaryMain, nullptr, 0, nullptr);
    if (h) CloseHandle(h);
    else g_watcherStarted.store(false, std::memory_order_release);
}

}  // namespace

void DispatchProbeStart() {
    bool expected = false;
    if (!g_armed.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already armed
    }
    MH_STATUS mi = MH_Initialize();
    if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "mh_init_failed", KV("mh_status", (uint64_t)mi));
        g_armed.store(false, std::memory_order_release);
        return;
    }

    kcdx::pe::ModuleView whgame{};
    if (!kcdx::pe::OpenModule(L"WHGame.dll", whgame) || !whgame.base) {
        LOG_ERROR_KV(kCat, "whgame_not_loaded", KV::BareStr("detail",
            "WHGame.dll not resolvable — cannot arm the dispatch probe (the "
            "validation-gate RVAs cannot be located this boot)."));
        return;
    }

    bool any = false;
    any |= HookRva(whgame, kRvaLoader,
                   reinterpret_cast<void*>(&HookedLoader),
                   reinterpret_cast<void**>(&g_origLoader),
                   "lookupdata_loader_FUN_180b04984");
    any |= HookRva(whgame, kRvaDriver,
                   reinterpret_cast<void*>(&HookedDriver),
                   reinterpret_cast<void**>(&g_origDriver),
                   "validate_driver_FUN_180b04478");

    LOG_INFO_KV(kCat, "dispatch_hooks_armed", KV("ok", any ? 1 : 0),
        KV::BareStr("detail",
            "after-hooked the shader-cache validation gate (lookupdata.bin loader "
            "+ validate driver). The loader's return per call is the gate verdict; "
            "armed before the swap decision so it fires swap-ON and swap-OFF."));
    StartWatcherOnce();
}

}  // namespace kcdx::fs_takeover
