// === DIAGNOSTIC (PROBE H) — KI-0028 boot-progress watcher + auto-stackdump ===
// See boot_watch.h for WHY + the Gate A binding discipline. NO-RESIDUE on retire.

#include "boot_watch.h"

#include <windows.h>
#include <tlhelp32.h>  // CreateToolhelp32Snapshot / Thread32First/Next

#include <atomic>

#include "../log.h"

namespace kcdx::fs_takeover {

namespace {

using KV = ::kcdx::log::KV;
constexpr const char* kCat = "BOOT_WATCH";

// ---- H1 heartbeat state (written by the main-thread tick, read by the watcher).
// tick: monotonic count. lastMs: GetTickCount64() at the last tick — the watcher
// reads THIS to detect a stall (the main thread stopped advancing it). Both
// relaxed: the only consumer is the watcher's stall test, which tolerates a
// one-sample lag (a real wedge holds lastMs frozen for seconds, far beyond any
// memory-visibility skew). No happens-before edge is published through them.
std::atomic<uint64_t> g_tick{0};
std::atomic<uint64_t> g_lastMs{0};
std::atomic<uint64_t> g_lastEmitSec{0};  // last integer-second we emitted on

std::atomic<bool> g_watcherStarted{false};

// The wedge thresholds (architect F5/F6; N user-set 2026-06-20).
constexpr uint64_t kStallMs      = 10'000;  // no tick advance for 10s => onset
constexpr uint64_t kSecondDumpMs = 30'000;  // +30s after onset => second dump
constexpr DWORD    kPollMs       = 250;     // watcher wake cadence (diagnostic)

// One frame captured WITHOUT logging (safe inside a suspended window). Fixed
// size, no allocation. module_rva + pc are the offline-symbolizable payload
// (module name resolved at LOG time, after resume — GetModuleHandleEx is not
// suspended-window-safe to call while holding other threads, so we keep only
// the pc here and resolve the module after every thread is resumed).
struct RawFrame {
    uint64_t pc;
};

constexpr int kMaxThreads = 256;
constexpr int kMaxFrames  = 64;

struct ThreadDump {
    DWORD    tid;
    int      frameCount;
    RawFrame frames[kMaxFrames];
    const char* endReason;  // fixed literal
};

// Walk one suspended thread's stack into `out` — alloc-free, lock-free, NO
// logging (architect F1). Same x64-native unwind as crash_guard's LogFullStack,
// but it WRITES TO A BUFFER instead of emitting. Caller MUST have the thread
// suspended (architect F2 — the ReadProcessMemory of its stack is valid only
// while suspended). Returns the frame count.
int WalkToBuffer(const CONTEXT* ctx, RawFrame* out, int maxFrames,
                 const char** endReason) {
    CONTEXT local = *ctx;  // RtlVirtualUnwind mutates it — copy.
    int n = 0;
    *endReason = "complete";
    for (int i = 0; i < maxFrames; ++i) {
        const uint64_t pc = local.Rip;
        if (pc == 0) { *endReason = "complete"; break; }
        out[n++].pc = pc;

        DWORD64           imageBase = 0;
        PRUNTIME_FUNCTION funcEntry =
            RtlLookupFunctionEntry(pc, &imageBase, nullptr);
        if (funcEntry) {
            PVOID   handlerData      = nullptr;
            DWORD64 establisherFrame = 0;
            RtlVirtualUnwind(UNW_FLAG_NHANDLER, imageBase, pc, funcEntry,
                             &local, &handlerData, &establisherFrame, nullptr);
        } else {
            if (local.Rsp == 0) { *endReason = "stack_unreadable"; break; }
            uint64_t retAddr = 0;
            if (::ReadProcessMemory(GetCurrentProcess(),
                                    reinterpret_cast<LPCVOID>(local.Rsp),
                                    &retAddr, sizeof(retAddr), nullptr) == 0) {
                *endReason = "stack_unreadable"; break;
            }
            local.Rip  = retAddr;
            local.Rsp += 8;
        }
        if (local.Rip == pc) { *endReason = "no_progress"; break; }
    }
    return n;
}

// Resolve a pc to "module_leaf" + rva for the LOG pass (after resume — this
// calls GetModuleHandleEx, which is fine once no thread is suspended).
void ModuleForPc(uint64_t pc, char* mod, size_t modLen, uint64_t* rva) {
    mod[0] = '?'; mod[1] = '\0'; *rva = 0;
    HMODULE h = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(pc), &h) && h) {
        wchar_t wpath[MAX_PATH] = {0};
        if (GetModuleFileNameW(h, wpath, MAX_PATH)) {
            const wchar_t* leaf = wpath;
            for (const wchar_t* p = wpath; *p; ++p)
                if (*p == L'\\' || *p == L'/') leaf = p + 1;
            size_t i = 0;
            for (; leaf[i] && i + 1 < modLen; ++i) mod[i] = (char)leaf[i];
            mod[i] = '\0';
        }
        *rva = pc - reinterpret_cast<uint64_t>(h);
    }
}

// The full dump: per thread (except this watcher thread) suspend -> GetThreadContext
// -> WalkToBuffer -> ResumeThread (architect F1/F2: one thread suspended at a
// time, NO logging inside the window). Then, with everything resumed, emit every
// buffered frame through the normal logger. `label` distinguishes onset vs +30s.
void DumpAllThreads(const char* label) {
    const DWORD selfTid = GetCurrentThreadId();
    const DWORD pid     = GetCurrentProcessId();

    // Now callable from TWO diagnostic watcher threads — boot_watch's cessation
    // dumper AND PROBE Y (stall_stack_probe.cpp). Their trigger signatures are
    // mutually exclusive (heartbeat stopped vs alive+draws==0) but not provably
    // so in TIME; a concurrent second entry would race the static `dumps[]`
    // buffer and garble the decisive capture. This latch admits exactly one dump
    // at a time (a dump is a terminal one-shot on a wedged boot, so a dropped
    // second dump is fine — the first already captured every thread). RAII so it
    // releases on every exit path (incl. the snapshot-failed early return).
    static std::atomic<bool> g_dumping{false};
    bool expected = false;
    if (!g_dumping.compare_exchange_strong(expected, true,
                                           std::memory_order_acq_rel)) {
        return;  // another watcher is mid-dump; its capture is authoritative
    }
    struct DumpLatch {
        std::atomic<bool>& f;
        ~DumpLatch() { f.store(false, std::memory_order_release); }
    } dumpLatch{g_dumping};

    static ThreadDump dumps[kMaxThreads];  // static: no stack blow, watcher-only
    int dumpCount = 0;

    HANDLE snap = CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
    if (snap == INVALID_HANDLE_VALUE) {
        LOG_ERROR_KV(kCat, "BOOT_DUMP_snapshot_failed",
            KV::BareStr("label", label),
            KV("win32_err", (uint64_t)GetLastError()));
        return;
    }
    THREADENTRY32 te{};
    te.dwSize = sizeof(te);
    if (Thread32First(snap, &te)) {
        do {
            if (te.th32OwnerProcessID != pid) continue;
            if (te.th32ThreadID == selfTid) continue;  // never suspend ourselves
            if (dumpCount >= kMaxThreads) break;

            HANDLE th = OpenThread(
                THREAD_SUSPEND_RESUME | THREAD_GET_CONTEXT | THREAD_QUERY_INFORMATION,
                FALSE, te.th32ThreadID);
            if (!th) continue;

            // --- SUSPENDED WINDOW (architect F1/F2): NO logging in here. ---
            ThreadDump& d = dumps[dumpCount];
            d.tid        = te.th32ThreadID;
            d.frameCount = 0;
            d.endReason  = "not_walked";
            if (SuspendThread(th) != (DWORD)-1) {
                CONTEXT ctx{};
                ctx.ContextFlags = CONTEXT_CONTROL | CONTEXT_INTEGER;
                if (GetThreadContext(th, &ctx)) {
                    d.frameCount = WalkToBuffer(&ctx, d.frames, kMaxFrames,
                                                &d.endReason);
                }
                ResumeThread(th);  // resume BEFORE we ever touch the logger
            }
            // --- END suspended window. ---
            CloseHandle(th);
            ++dumpCount;
        } while (Thread32Next(snap, &te));
    }
    CloseHandle(snap);

    // --- LOG PASS: every thread resumed; the logger mutex is safe now. ---
    LOG_ERROR_KV(kCat, "BOOT_DUMP_BEGIN",
        KV::BareStr("label", label),
        KV("threads", dumpCount),
        KV::BareStr("detail",
            "KI-0028 wedge stack dump — heartbeat stalled. Frames are pc + "
            "module_rva (offline-symbolizable vs WHGame.dll). A render/main "
            "thread parked in NVSDK_NGX_UpdateFeature / C_Game::CreateInstance "
            "is the wedge; two dumps (onset, +30s) with IDENTICAL frozen frames "
            "and a never-resuming heartbeat => DEADLOCK; heartbeat resuming => "
            "LATENCY."));
    for (int i = 0; i < dumpCount; ++i) {
        const ThreadDump& d = dumps[i];
        for (int f = 0; f < d.frameCount; ++f) {
            char mod[128]; uint64_t rva = 0;
            ModuleForPc(d.frames[f].pc, mod, sizeof(mod), &rva);
            LOG_ERROR_KV(kCat, "BOOT_FRAME",
                KV::BareStr("label", label),
                KV("tid",        (uint64_t)d.tid),
                KV("frame",      f),
                KV::BareStr("module", mod),
                KV("module_rva", rva),
                KV("pc",         reinterpret_cast<void*>(d.frames[f].pc)));
        }
        LOG_ERROR_KV(kCat, "BOOT_FRAMES_END",
            KV::BareStr("label", label),
            KV("tid",     (uint64_t)d.tid),
            KV("frames",  d.frameCount),
            KV::BareStr("reason", d.endReason));
    }
    LOG_ERROR_KV(kCat, "BOOT_DUMP_END", KV::BareStr("label", label));
}

// === DIAGNOSTIC (PROBE W) — KI-0028 window-activation observer ==============
//
// WHY: the static exit-gate read (_research/ki0028-window-exit-gate-recon/)
// found the engine's per-frame focus poll (fn 0x865fb4) gates on
// `GetActiveWindow() == <engine-expected HWND>`. The whole boot proceeds past
// this only when the process window becomes the OS active/foreground window.
// All four FS-slot-output divergences are exonerated; kcdx calls ZERO window
// APIs (grep-verified) — so if the window never activates swap-on, it is an
// INDIRECT effect of the swap, observed here.
//
// This is Win32-ONLY — no engine offset, no hook, no ABI. From the existing
// watcher thread we sample the process's top-level window state and log on
// integer-second edges (event-debounced, NOT a new timer — the watcher already
// wakes every kPollMs; logging.md/polling.md).
//
// OUTCOME MAP (pre-committed, theory-independent — run swap-ON then swap-OFF):
//   - swap-OFF (reaches menu): a process top-level window becomes VISIBLE and
//     FOREGROUND/ACTIVE — records the HWND + the wall-second it converged.
//   - swap-ON (wedge): compare against swap-OFF:
//       (a) no process window ever becomes foreground/active (or never visible)
//           => CONFIRMS the window-activation mechanism (the swap prevents the
//           window reaching active state; the engine's GetActiveWindow gate
//           never passes).
//       (b) the window DOES become foreground/active swap-on too => the
//           active-window gate is NOT the wedge; Main passes it and wedges
//           later => widen the frame. (The falsifier that kills this theory.)
// Greppable tag: "WINDOW_PROBE".
//
// NO-RESIDUE: on retirement capture finding+wiring to _research/probe-archive/
// then REMOVE from live source (working-artifacts.md).

struct WinProbeEnum {
    DWORD  pid;
    HWND   foreground;
    HWND   active;          // GetActiveWindow() of the foreground thread, if ours
    int    topLevelCount;   // our process's top-level windows
    int    visibleCount;    // ...of which visible
    HWND   firstVisible;
    bool   foregroundIsOurs;
};

BOOL CALLBACK WinProbeEnumProc(HWND hwnd, LPARAM lp) {
    auto* e = reinterpret_cast<WinProbeEnum*>(lp);
    DWORD wpid = 0;
    GetWindowThreadProcessId(hwnd, &wpid);
    if (wpid != e->pid) return TRUE;  // not ours
    ++e->topLevelCount;
    if (IsWindowVisible(hwnd)) {
        ++e->visibleCount;
        if (!e->firstVisible) e->firstVisible = hwnd;
    }
    return TRUE;
}

// Sample the process window state once. Pure Win32 reads, alloc-free. Emits one
// WINDOW_PROBE line per wall-second the state is sampled (the caller gates on the
// integer-second edge), plus a one-shot CONVERGED line the first time a process
// window is BOTH visible AND the foreground window — the swap-OFF baseline's
// convergence point, and the signal whose ABSENCE swap-ON is the answer.
void WinProbeSample(uint64_t nowSec, bool* convergedLatch) {
    WinProbeEnum e{};
    e.pid        = GetCurrentProcessId();
    e.foreground = GetForegroundWindow();
    EnumWindows(WinProbeEnumProc, reinterpret_cast<LPARAM>(&e));

    DWORD fgPid = 0;
    if (e.foreground) GetWindowThreadProcessId(e.foreground, &fgPid);
    e.foregroundIsOurs = (fgPid == e.pid && e.foreground != nullptr);

    LOG_INFO_KV(kCat, "WINDOW_PROBE",
        KV("wall_s",          nowSec),
        KV("toplevel",        e.topLevelCount),
        KV("visible",         e.visibleCount),
        KV("fg_is_ours",      (uint64_t)(e.foregroundIsOurs ? 1 : 0)),
        KV("foreground_hwnd", e.foreground),
        KV("first_visible",   e.firstVisible));

    // The convergence signal: a visible process window that is ALSO foreground.
    // This is exactly the state the engine's GetActiveWindow gate (focus-poll
    // 0x866029) waits for. One-shot — its PRESENCE swap-OFF and ABSENCE swap-ON
    // is the KI-0028 answer.
    if (!*convergedLatch && e.foregroundIsOurs && e.visibleCount > 0) {
        *convergedLatch = true;
        LOG_WARN_KV(kCat, "WINDOW_PROBE_CONVERGED",
            KV("wall_s",          nowSec),
            KV("foreground_hwnd", e.foreground),
            KV::BareStr("detail",
                "a process top-level window is BOTH visible AND the OS foreground "
                "window — the state the engine's GetActiveWindow focus-gate waits "
                "for. PRESENCE here = the window activated; ABSENCE across a full "
                "wedge run = the swap prevents window activation (KI-0028 "
                "mechanism CONFIRMED). Compare swap-ON vs swap-OFF."));
    }
}
// === END PROBE W ===

// The watcher thread. Wakes every kPollMs, reads the heartbeat's last-advance
// ms, and on a kStallMs stall dumps (onset), then again at +kSecondDumpMs, then
// stops arming. A live boot (heartbeat advancing) produces no dump. This is a
// dedicated DIAGNOSTIC thread (architect's prescribed fix for the F1 deadlock),
// not a production state poll — it watches for a one-shot failure and is removed
// with the probe on retirement.
DWORD WINAPI WatcherMain(LPVOID) {
    bool onsetDumped  = false;
    bool secondDumped = false;
    uint64_t onsetMs  = 0;

    // PROBE W state: sample window activation every wall-second across the WHOLE
    // boot (independent of the stall logic), and latch the one-shot convergence.
    uint64_t winProbeLastSec = 0;
    bool     winConverged    = false;

    for (;;) {
        Sleep(kPollMs);
        const uint64_t now     = GetTickCount64();

        // === PROBE W — window-activation sample (runs the entire boot, even
        // before the first update tick; the window can activate before update).
        // Event-debounced on the integer-second edge — one line per wall-second,
        // NOT per kPollMs wake. ===
        {
            const uint64_t nowSec = now / 1000;
            if (nowSec != winProbeLastSec) {
                winProbeLastSec = nowSec;
                WinProbeSample(nowSec, &winConverged);
            }
        }
        // === END PROBE W sample ===

        const uint64_t lastMs  = g_lastMs.load(std::memory_order_relaxed);
        if (lastMs == 0) continue;  // no tick yet — boot hasn't reached update

        const uint64_t stalled = (now >= lastMs) ? (now - lastMs) : 0;

        if (!onsetDumped) {
            if (stalled >= kStallMs) {
                LOG_WARN_KV(kCat, "BOOT_WATCH_STALL",
                    KV("stalled_ms", stalled),
                    KV("last_tick",  g_tick.load(std::memory_order_relaxed)),
                    KV::BareStr("detail",
                        "main-thread heartbeat has not advanced for >= the stall "
                        "threshold — wedge onset. Dumping all thread stacks."));
                DumpAllThreads("onset");
                onsetDumped = true;
                onsetMs     = now;
            }
            continue;
        }

        // After onset: if the heartbeat RESUMES, that is the decisive LATENCY
        // verdict — log it loud and stop (no permanent wedge).
        if (stalled < kStallMs) {
            LOG_WARN_KV(kCat, "BOOT_WATCH_RESUMED",
                KV("tick", g_tick.load(std::memory_order_relaxed)),
                KV::BareStr("detail",
                    "heartbeat RESUMED after the stall — the wait was LATENT, "
                    "not a deadlock. NGX/FSR2 (or its dependency) eventually "
                    "completed. The fork resolves to LATENCY."));
            return 0;
        }

        // Still stalled: fire the +30s second dump once, then stop arming.
        if (!secondDumped && (now - onsetMs) >= kSecondDumpMs) {
            LOG_WARN_KV(kCat, "BOOT_WATCH_STILL_STALLED",
                KV("stalled_ms", stalled),
                KV::BareStr("detail",
                    "heartbeat STILL not advancing 30s after onset — second "
                    "dump. Identical frozen frames vs the onset dump + a "
                    "heartbeat that never resumes => DEADLOCK."));
            DumpAllThreads("plus30s");
            secondDumped = true;
        }
        if (secondDumped) return 0;  // two dumps taken; stop the watcher
    }
}

}  // namespace

uint64_t BootWatchTickCount() {
    return g_tick.load(std::memory_order_relaxed);
}

void BootWatchDumpAllThreads(const char* label) {
    // Thin public reuse of the anon-namespace dumper (F1/F2 discipline is inside
    // it). PROBE Y (stall_stack_probe.cpp) fires this on the stall-no-geometry
    // signature the cessation watcher cannot see.
    DumpAllThreads(label);
}

void BootWatchTick() {
    const uint64_t now = GetTickCount64();
    g_lastMs.store(now, std::memory_order_relaxed);
    const uint64_t t = g_tick.fetch_add(1, std::memory_order_relaxed) + 1;

    // H1 emit: integer-second transition edge only (event-debounce on the tick,
    // NOT a timer — logging.md / polling.md). One BOOT_WATCH line per wall-second
    // the tick is alive; their cessation marks the wedge onset.
    const uint64_t nowSec = now / 1000;
    uint64_t lastSec = g_lastEmitSec.load(std::memory_order_relaxed);
    if (nowSec != lastSec &&
        g_lastEmitSec.compare_exchange_strong(lastSec, nowSec,
                                               std::memory_order_relaxed)) {
        LOG_INFO_KV(kCat, "heartbeat",
            KV("tick",   t),
            KV("wall_s", nowSec));
    }
}

void BootWatchStart() {
    bool expected = false;
    if (!g_watcherStarted.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
        return;  // already started
    }
    HANDLE h = CreateThread(nullptr, 0, WatcherMain, nullptr, 0, nullptr);
    if (h) {
        CloseHandle(h);  // detached; the watcher self-exits after its two dumps
        LOG_INFO_KV(kCat, "watcher_started",
            KV("stall_ms",  kStallMs),
            KV("second_ms", kSecondDumpMs),
            KV::BareStr("detail",
                "KI-0028 boot-progress watcher armed — dumps all thread stacks "
                "if the main-thread heartbeat stalls 10s, again at +30s, then "
                "exits. A healthy boot produces no dump."));
    } else {
        g_watcherStarted.store(false, std::memory_order_release);
        LOG_ERROR_KV(kCat, "watcher_start_failed",
            KV("win32_err", (uint64_t)GetLastError()));
    }
}

}  // namespace kcdx::fs_takeover
