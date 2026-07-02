// === DIAGNOSTIC (PROBE Y) — KI-0028 stall-no-geometry main-thread stack trigger =
// See stall_stack_probe.h for WHY + the trigger design. NO-RESIDUE on retire.

#include "stall_stack_probe.h"

#include <windows.h>

#include <atomic>

#include "../log.h"
#include "boot_watch.h"      // BootWatchDumpAllThreads + BootWatchTickCount
#include "drawcall_probe.h"  // DrawcallProbeIndexedCount
#include "present_probe.h"   // PresentProbeLastCount / PresentProbeSwapchainCaptured

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "STALL_STACK";

std::atomic<bool> g_started{false};

// Trigger thresholds.
constexpr uint64_t kPresentArm     = 30;      // present-count climb to call it "advancing"
constexpr uint64_t kHeartbeatFloor = 200;     // ticks before arming (transient guard)
constexpr int      kConfirmPolls   = 40;      // draw==0 across 40 wakes (~10s) => fire
constexpr uint64_t kGiveUpMs       = 300'000; // 5min without arming => NEVER_ARMED
constexpr DWORD    kPollMs         = 250;     // wake cadence (the sanctioned diagnostic poll)

// The dedicated diagnostic watcher — one Sleep-cadence thread (PROBE K/S shape),
// drives the arm->fire state machine, then self-exits. A boot that advances
// (draw_indexed>0) or never reaches the transition produces no stack dump.
DWORD WINAPI WatcherMain(LPVOID) {
    enum State { kWaitPresent, kArmed };
    State    state        = kWaitPresent;
    uint64_t firstPresent = 0;
    bool     havePresent  = false;
    int      drawZeroPolls = 0;
    const uint64_t startMs = GetTickCount64();

    LOG_INFO_KV(kCat, "STALL_STACK_watcher_started",
        KV::BareStr("detail",
            "KI-0028 PROBE Y armed — waits for present to advance (streaming/UI "
            "phase), then dumps all thread stacks if draw_indexed STAYS 0 "
            "(geometry never requested). Reads live accessors, not bounded "
            "watcher caches. swap-ON vs swap-OFF stack diff names the sequencer "
            "gate. A run that advances past the transition produces no dump."));

    for (;;) {
        Sleep(kPollMs);
        const uint64_t nowMs   = GetTickCount64();
        const uint64_t present = PresentProbeLastCount();
        const uint64_t draws   = DrawcallProbeIndexedCount();
        const uint64_t ticks   = BootWatchTickCount();

        if (state == kWaitPresent) {
            if (present > 0 && !havePresent) {
                firstPresent = present;
                havePresent  = true;
            }
            const bool presentAdvancing =
                havePresent && present >= firstPresent + kPresentArm;
            if (presentAdvancing && ticks >= kHeartbeatFloor) {
                state         = kArmed;
                drawZeroPolls = 0;
                LOG_WARN_KV(kCat, "STALL_STACK_ARMED",
                    KV("present",      present),
                    KV("draw_indexed", draws),
                    KV("ticks",        ticks),
                    KV::BareStr("detail",
                        "present advancing + heartbeat floor passed — armed. "
                        "Watching for draw_indexed to STAY 0; if it does, dump "
                        "all thread stacks (the KI-0028 stall signature)."));
                continue;
            }
            // Distinguishable never-fire: present never climbed in the window.
            if (nowMs - startMs >= kGiveUpMs) {
                const bool scCaptured = PresentProbeSwapchainCaptured();
                LOG_WARN_KV(kCat, "STALL_STACK_NEVER_ARMED",
                    KV("swapchain_captured", (uint64_t)(scCaptured ? 1 : 0)),
                    KV("present",            present),
                    KV("ticks",              ticks),
                    KV::BareStr("reason", scCaptured
                        ? "swapchain captured but present never climbed to the "
                          "arm threshold — the boot did not reach the streaming/"
                          "UI-compositing phase (NOT the KI-0028 signature; find "
                          "why present never advanced)."
                        : "swapchain NEVER captured (the engine created it by a "
                          "path the DXGI factory hook missed) — PROBE Y could "
                          "not read present progress; widen the capture point."));
                return 0;
            }
            continue;
        }

        // state == kArmed.
        if (draws > 0) {
            // Geometry WAS requested — this run advanced past the transition (the
            // swap-OFF / working outcome). Terminal, no dump.
            LOG_WARN_KV(kCat, "STALL_STACK_ADVANCED",
                KV("present",      present),
                KV("draw_indexed", draws),
                KV::BareStr("detail",
                    "draw_indexed became > 0 after arming — indexed geometry WAS "
                    "requested, so this run advanced (the working / swap-OFF "
                    "outcome). No stall dump. Compare against the swap-ON run "
                    "where draw_indexed stays 0 and this fires STALL_STACK_FIRED."));
            return 0;
        }
        if (++drawZeroPolls >= kConfirmPolls) {
            LOG_WARN_KV(kCat, "STALL_STACK_FIRED",
                KV("present",       present),
                KV("draw_indexed",  draws),
                KV("confirm_polls", drawZeroPolls),
                KV::BareStr("detail",
                    "present advancing but draw_indexed stayed 0 across the "
                    "confirm window — the KI-0028 stall signature (reached the "
                    "streaming/UI phase, never requested geometry). Dumping all "
                    "thread stacks; the swap-ON vs swap-OFF main-thread stack "
                    "diff names the boot-phase sequencer gate."));
            BootWatchDumpAllThreads("stall_no_geometry");
            return 0;
        }
    }
}

}  // namespace

void StallStackProbeStart() {
    bool expected = false;
    if (!g_started.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // already started
    }
    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);  // detached; self-exits after its terminal line
    } else {
        g_started.store(false, std::memory_order_release);
        LOG_ERROR_KV(kCat, "STALL_STACK_start_failed",
            KV("win32_err", (uint64_t)GetLastError()));
    }
}

}  // namespace kcdx::fs_takeover
