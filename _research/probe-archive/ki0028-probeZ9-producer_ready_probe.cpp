// === DIAGNOSTIC (PROBE Z9) — KI-0028 shader-system ready-flag ground truth ======
// See producer_ready_probe.h for WHY + the outcome->meaning map. NO-RESIDUE on retire.

#include "producer_ready_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"
#include "../log.h"
#include "../pe_helpers.h"  // kcdx::pe::OpenModule / ModuleView

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "READY_PROBE";

// SOURCE: static disasm (_research/ki0028-mainthread-condvar-wait-recon). These are
// diagnostic scratch RVAs/offsets for THIS boot's observation, not resolved-by-name
// address targets — a probe reads raw addresses to observe ground truth (the same
// scratch-RVA shape PROBE Y/Z7/S use); not an AP1 hardcoded resolution.
constexpr uint32_t kRvaReadyWaitLoop = 0x9ace14;  // CShaderMan ready-wait loop entry, f(rcx=wait_obj)
constexpr uint32_t kRvaProducer      = 0x492b8c8; // gEnv-table producer singleton (kicked via vtable +0x720)
constexpr uint32_t kRvaShaderMan     = 0x547bb50; // gEnv-table CShaderMan singleton (polled +0x108)
constexpr uintptr_t kReadyFlagOff    = 0x50;      // pred = *(bool*)(wait_obj+0x50)

constexpr DWORD kPollMs    = 250;      // sanctioned diagnostic-poll cadence (PROBE K/S/Y shape)
constexpr uint64_t kRunMs  = 120'000;  // observe ~2 min then report a terminal verdict

std::atomic<bool>     g_armed{false};
std::atomic<uint64_t> g_waitObj{0};        // captured wait object (rcx) on first entry
std::atomic<uint64_t> g_waitEnters{0};     // how many times the wait loop was entered
uintptr_t             g_producerAddr = 0;  // &[0x492b8c8] resolved at arm
uintptr_t             g_shaderManAddr = 0; // &[0x547bb50] resolved at arm

// Single-arg __fastcall — verified 0x9ace14 reads ONLY rcx as an argument
// (rdx/r8/r9 are set locally before use), so a single-pointer forward is ABI-safe.
using ReadyWaitFn_t = void*(__fastcall*)(void* thisObj);
ReadyWaitFn_t g_origReadyWait = nullptr;

void* __fastcall HookedReadyWait(void* thisObj) {
    // Capture the wait object once — its +0x50 is the ready-flag predicate.
    uint64_t expected = 0;
    if (g_waitObj.compare_exchange_strong(
            expected, reinterpret_cast<uint64_t>(thisObj),
            std::memory_order_acq_rel)) {
        LOG_WARN_KV(kCat, "READY_WAIT_ENTERED",
            KV("wait_obj",     reinterpret_cast<uint64_t>(thisObj)),
            KV("ready_flag_at", reinterpret_cast<uint64_t>(thisObj) + kReadyFlagOff),
            KV::BareStr("detail",
                "CShaderMan ready-wait loop (0x9ace14) ENTERED — Main is about to "
                "block on the shader-system-ready flag. The watcher now polls "
                "[wait_obj+0x50] (the predicate) + the producer singleton."));
    }
    g_waitEnters.fetch_add(1, std::memory_order_relaxed);
    return g_origReadyWait(thisObj);
}

uint64_t ReadPtr(uintptr_t addr) {
    uint64_t v = 0;
    if (addr) __try {
        v = *reinterpret_cast<volatile uint64_t*>(addr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = 0;
    }
    return v;
}

uint8_t ReadFlag(uint64_t objAddr) {
    uint8_t v = 0;
    if (objAddr) __try {
        v = *reinterpret_cast<volatile uint8_t*>(objAddr + kReadyFlagOff);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        v = 0;
    }
    return v;
}

// Find this process's main visible top-level window (the game window).
struct FindWinCtx { DWORD pid; HWND hwnd; };
BOOL CALLBACK FindMainWindowCb(HWND hwnd, LPARAM lp) {
    auto* ctx = reinterpret_cast<FindWinCtx*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid == ctx->pid && IsWindowVisible(hwnd) && GetWindow(hwnd, GW_OWNER) == nullptr) {
        ctx->hwnd = hwnd;
        return FALSE;  // stop
    }
    return TRUE;
}

// Terminal completion signal — so the operator sees "done" without watching a clock:
// retitle the game window + flash its taskbar button. Diagnostic UX only.
void SignalOperatorProbeDone() {
    FindWinCtx ctx{ GetCurrentProcessId(), nullptr };
    EnumWindows(FindMainWindowCb, reinterpret_cast<LPARAM>(&ctx));
    if (!ctx.hwnd) {
        LOG_WARN_KV(kCat, "done_signal_no_window", KV::BareStr("detail",
            "verdict written but the game window was not found to signal — read the "
            "READY_PROBE_VERDICT line; the run is safe to close."));
        return;
    }
    SetWindowTextW(ctx.hwnd, L"PROBE Z9 DONE - safe to close");
    FLASHWINFO fi{};
    fi.cbSize    = sizeof(fi);
    fi.hwnd      = ctx.hwnd;
    fi.dwFlags   = FLASHW_ALL | FLASHW_TIMERNOFG;  // flash caption + taskbar until foregrounded
    fi.uCount    = 0;
    fi.dwTimeout = 0;
    FlashWindowEx(&fi);
    LOG_INFO_KV(kCat, "done_signal_sent", KV::BareStr("detail",
        "game window retitled 'PROBE Z9 DONE - safe to close' + taskbar flashing "
        "(Alt-Tab / taskbar shows it even on a black screen)."));
}

DWORD WINAPI WatcherMain(LPVOID) {
    const uint64_t startMs = GetTickCount64();
    bool sawEnter    = false;
    bool sawFlagSet  = false;
    bool sawProducer = false;

    LOG_INFO_KV(kCat, "READY_PROBE_watcher_started",
        KV::BareStr("detail",
            "KI-0028 PROBE Z9 armed — observes whether the shader-system ready-flag "
            "[wait_obj+0x50] ever flips, and whether the producer singleton "
            "[0x492b8c8] is installed. swap-ON (black) vs swap-OFF (menu) A/B names "
            "whether the swap derails the producer's COMPLETION or its INSTALLATION."));

    for (;;) {
        Sleep(kPollMs);
        const uint64_t nowMs   = GetTickCount64();
        const uint64_t obj     = g_waitObj.load(std::memory_order_acquire);
        const uint64_t producer = ReadPtr(g_producerAddr);
        const uint8_t  flag     = obj ? ReadFlag(obj) : 0;

        if (obj && !sawEnter) {
            sawEnter = true;
        }
        if (producer && !sawProducer) {
            sawProducer = true;
            LOG_WARN_KV(kCat, "PRODUCER_INSTALLED",
                KV("producer", producer),
                KV::BareStr("detail",
                    "[0x492b8c8] became non-null — the producer subsystem IS "
                    "installed on this arm. If the ready-flag still never flips, the "
                    "swap derails its COMPLETION, not its installation."));
        }
        if (obj && flag && !sawFlagSet) {
            sawFlagSet = true;
            LOG_WARN_KV(kCat, "READY_FLAG_SET",
                KV("wait_obj", obj),
                KV("wall_ms",  nowMs - startMs),
                KV::BareStr("detail",
                    "the shader-ready flag [wait_obj+0x50] FLIPPED to non-zero — the "
                    "shader system became ready on this arm. If this fires swap-OFF "
                    "but NOT swap-ON, the producer's ready-signal is the differentiator."));
        }

        if (nowMs - startMs >= kRunMs) {
            // Terminal verdict — a never-fire is an OBSERVED outcome, never silence.
            LOG_WARN_KV(kCat, "READY_PROBE_VERDICT",
                KV("wait_entered",  (uint64_t)(sawEnter ? 1 : 0)),
                KV("wait_enters",   g_waitEnters.load()),
                KV("flag_ever_set", (uint64_t)(sawFlagSet ? 1 : 0)),
                KV("producer_installed", (uint64_t)(sawProducer ? 1 : 0)),
                KV("producer_now",  ReadPtr(g_producerAddr)),
                KV("shaderman_now", ReadPtr(g_shaderManAddr)),
                KV::BareStr("map",
                    "never-entered => Main's stuck wait is a DIFFERENT instance "
                    "(re-attribute). entered+flag_set => shader-ready DOES fire "
                    "(stall is downstream). entered+flag never set+producer non-null "
                    "=> CONFIRMED producer installed but never signals ready (probe "
                    "+0x720 kick / +0x50 writer next). entered+flag never set+producer "
                    "null => producer singleton never installed (walk its installer)."));
            SignalOperatorProbeDone();  // retitle + flash the window so the operator knows to close
            return 0;
        }
    }
}

}  // namespace

void ProducerReadyProbeStart() {
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
            "WHGame.dll not resolvable — cannot arm the ready probe."));
        g_armed.store(false, std::memory_order_release);
        return;
    }
    const uintptr_t base = reinterpret_cast<uintptr_t>(whgame.base);
    g_producerAddr  = base + kRvaProducer;
    g_shaderManAddr = base + kRvaShaderMan;

    void* target = reinterpret_cast<void*>(base + kRvaReadyWaitLoop);
    MH_STATUS s = MH_CreateHook(target, reinterpret_cast<void*>(&HookedReadyWait),
                                reinterpret_cast<void**>(&g_origReadyWait));
    if (s != MH_OK && s != MH_ERROR_ALREADY_CREATED) {
        LOG_ERROR_KV(kCat, "hook_create_failed",
            KV("rva", (uint64_t)kRvaReadyWaitLoop), KV("mh_status", (uint64_t)s));
    } else if (MH_EnableHook(target) != MH_OK) {
        LOG_ERROR_KV(kCat, "hook_enable_failed", KV("rva", (uint64_t)kRvaReadyWaitLoop));
    } else {
        LOG_INFO_KV(kCat, "ready_wait_hook_armed",
            KV("target", reinterpret_cast<uintptr_t>(target)),
            KV("producer_addr", g_producerAddr));
    }

    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);  // detached; self-exits after its terminal verdict
    } else {
        LOG_ERROR_KV(kCat, "watcher_start_failed",
            KV("win32_err", (uint64_t)GetLastError()));
    }
}

}  // namespace kcdx::fs_takeover
