// === DIAGNOSTIC (PROBE U) — KI-0028 post-seat CCryPak vtable-reswap watcher ===
// See reswap_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "reswap_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "RESWAP_PROBE";

// Seat ground truth, captured once at arm time.
std::atomic<void*>       g_swappedObj{nullptr};   // the object kcdx swapped
std::atomic<void*>       g_kcdxVtable{nullptr};    // what kcdx wrote into [obj+0]
const void* volatile     g_gEnvSlotVa = nullptr;   // VA of the global pCryPak slot
std::atomic<void*>       g_seatGlobal{nullptr};     // the global object value at seat

std::atomic<bool> g_started{false};

// Bounded diagnostic poll (same sanctioned shape as PROBE K/W — an interval read
// that stops after a bounded count, NOT a production loop).
constexpr DWORD kSampleMs   = 1000;   // 1s cadence
constexpr int   kMaxSamples = 180;    // ~3 min — covers the freeze window

// Read a pointer-width value from an address (read-only; the engine owns it).
void* ReadPtr(const void* at) {
    if (!at) return nullptr;
    void* v = nullptr;
    std::memcpy(&v, at, sizeof(v));
    return v;
}

DWORD WINAPI WatcherMain(LPVOID) {
    void* const obj       = g_swappedObj.load(std::memory_order_acquire);
    void* const kcdxVt    = g_kcdxVtable.load(std::memory_order_acquire);
    const void* const slot = g_gEnvSlotVa;
    void* const seatGlobal = g_seatGlobal.load(std::memory_order_acquire);

    LOG_INFO_KV(kCat, "watcher_started",
        KV("swapped_obj",  reinterpret_cast<uintptr_t>(obj)),
        KV("kcdx_vtable",  reinterpret_cast<uintptr_t>(kcdxVt)),
        KV("gEnv_slot_va", reinterpret_cast<uintptr_t>(slot)),
        KV("seat_global",  reinterpret_cast<uintptr_t>(seatGlobal)),
        KV::BareStr("detail",
            "PROBE U armed. Sampling [obj+0x00] (the swapped object's vtable ptr) "
            "and *(gEnv pCryPak slot) every 1s. vtable!=kcdx => engine re-swapped "
            "the object post-seat. global!=seat object => a separate/replacement "
            "CCryPak is now the live global. neither changes => kcdx owns the one "
            "CCryPak end-to-end (reswap theory falsified)."));

    bool vtableDivergedLogged = false;
    bool globalDivergedLogged = false;

    for (int i = 0; i < kMaxSamples; ++i) {
        Sleep(kSampleMs);

        // 1) The swapped object's CURRENT vtable pointer.
        void* curVt = ReadPtr(obj);  // [obj + 0x00]
        if (curVt != kcdxVt && !vtableDivergedLogged) {
            vtableDivergedLogged = true;
            LOG_WARN_KV(kCat, "vtable_diverged",
                KV("sample",      static_cast<uint64_t>(i)),
                KV("swapped_obj", reinterpret_cast<uintptr_t>(obj)),
                KV("kcdx_vtable", reinterpret_cast<uintptr_t>(kcdxVt)),
                KV("now_vtable",  reinterpret_cast<uintptr_t>(curVt)),
                KV::BareStr("detail",
                    "[obj+0x00] is NO LONGER kcdx's vtable — the engine RE-POINTED "
                    "the CCryPak object's vtable AFTER kcdx's one-shot seat. kcdx "
                    "ownership was silently lost mid-boot. THIS IS THE MECHANISM "
                    "CLASS: the geometry path dispatches through now_vtable, not "
                    "kcdx. Next: identify what wrote now_vtable + when."));
        }

        // 2) The global pCryPak slot — is a DIFFERENT object the live global now?
        void* curGlobal = ReadPtr(slot);
        if (curGlobal != seatGlobal && !globalDivergedLogged) {
            globalDivergedLogged = true;
            LOG_WARN_KV(kCat, "global_diverged",
                KV("sample",       static_cast<uint64_t>(i)),
                KV("seat_global",  reinterpret_cast<uintptr_t>(seatGlobal)),
                KV("now_global",   reinterpret_cast<uintptr_t>(curGlobal)),
                KV("now_global_vtable",
                   reinterpret_cast<uintptr_t>(ReadPtr(curGlobal))),
                KV::BareStr("detail",
                    "the global pCryPak slot points at a DIFFERENT object than the "
                    "one kcdx swapped — a separate/replacement CCryPak is now the "
                    "live global. The geometry path uses an object kcdx never "
                    "owned. now_global_vtable names whose vtable it carries. Next: "
                    "where that object is constructed + why kcdx didn't swap it."));
        }

        // Periodic liveness line (every 30s) so a clean run is positively recorded.
        if ((i % 30) == 0) {
            LOG_INFO_KV(kCat, "sample",
                KV("i",          static_cast<uint64_t>(i)),
                KV("vtable_ok",  curVt == kcdxVt ? 1 : 0),
                KV("global_ok",  curGlobal == seatGlobal ? 1 : 0));
        }
    }

    LOG_INFO_KV(kCat, "watcher_done",
        KV("vtable_ever_diverged", vtableDivergedLogged ? 1 : 0),
        KV("global_ever_diverged", globalDivergedLogged ? 1 : 0),
        KV::BareStr("detail",
            "PROBE U bounded sampling complete. If BOTH ever_diverged=0, kcdx "
            "owned the one CCryPak object (vtable + global) end-to-end for the "
            "whole window — the reswap/second-object theory is FALSIFIED and the "
            "divergence is a non-CCryPak state the swap perturbs."));
    return 0;
}

}  // namespace

void ReswapProbeStartAtSeat(void* swappedObj, void* kcdxVtable,
                            const void* gEnvSlotVa) {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;
    }
    if (!swappedObj || !kcdxVtable || !gEnvSlotVa) {
        LOG_WARN_KV(kCat, "probe_disabled",
            KV("swapped_obj", reinterpret_cast<uintptr_t>(swappedObj)),
            KV("kcdx_vtable", reinterpret_cast<uintptr_t>(kcdxVtable)),
            KV("gEnv_slot_va", reinterpret_cast<uintptr_t>(gEnvSlotVa)),
            KV::BareStr("detail",
                "PROBE U not armed — a null seat input (object, kcdx vtable, or "
                "gEnv slot VA). The reswap watcher cannot run this boot."));
        g_started.store(false, std::memory_order_release);
        return;
    }
    g_swappedObj.store(swappedObj, std::memory_order_release);
    g_kcdxVtable.store(kcdxVtable, std::memory_order_release);
    g_gEnvSlotVa = gEnvSlotVa;
    g_seatGlobal.store(ReadPtr(gEnvSlotVa), std::memory_order_release);

    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);
    } else {
        LOG_ERROR_KV(kCat, "watcher_thread_failed",
            KV::BareStr("detail",
                "CreateThread for the PROBE U reswap watcher failed; the post-seat "
                "vtable/global watch does not run this boot."));
        g_started.store(false, std::memory_order_release);
    }
}

}  // namespace kcdx::fs_takeover
