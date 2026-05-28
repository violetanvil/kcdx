#include "select_detour.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <vector>

#include "enabled_list_builder.h"
#include "../log.h"

// Worker-side enabled-list machinery — see select_detour.h for the framing.
//
// This file owns the BUILD half of the mod-loader takeover: the kcdx-owned
// process-lifetime std::vector<void*> of synthesized I_Mod* pointers, the
// diagnostic parallel vector, the manual-reset readiness event, and the
// CreateReadyEvent + BuildEnabledListOnWorker worker-thread entry points.
//
// The FIRE half — the live mutation of the engine's C_ModManager state —
// is now ctor_bracket.cpp: kcdx fully replaces ModManager_ctor and reads
// the storage exposed below directly. There is no SELECT detour and no
// HookedSelect callback in this file anymore.

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// === kcdx-OWNED, PROCESS-LIFETIME enabled-list storage ======================
//
// The synthesized C_ModManager at +0x30/+0x38/+0x40 points at &g_enabledList[0]
// .. &g_enabledList[N]. This vector MUST outlive MOUNT + every downstream pass
// that walks the list, so it is module-static (process lifetime) — NEVER built
// on the stack. The I_Mod* elements point at records record_synth owns
// process-lifetime; this array owns only the POINTER storage. It is filled
// exactly once (BuildEnabledListOnWorker's one-shot latch) and never
// reallocated after the bracket has consumed it.
std::vector<void*> g_enabledList;

// Parallel diagnostic vector. Built by the worker (BuildEnabledListOnWorker)
// alongside g_enabledList; read for the per-record DEBUG breakdown + vanilla /
// plugin counts. Module-static (process lifetime) so it survives the worker ->
// game-thread handoff.
std::vector<EnabledListEntry> g_entries;

// Readiness event the ctor-bracket callback waits on. Manual-reset, initially
// unsignaled. CreateReadyEvent (called by the worker BEFORE InstallCtorBracket)
// creates the handle and stores it with release ordering; the bracket's
// HookedCtor loads it with acquire ordering before checking + waiting (the
// worker creates on one thread, the game-thread callback reads on another —
// std::atomic establishes the happens-before edge so the write is visible
// without relying on hardware memory-model accidents). SetEvent fires once at
// the end of BuildEnabledListOnWorker; the event stays signaled (manual-reset)
// for the rest of the session so any re-entry callback's wait returns
// immediately.
std::atomic<HANDLE> g_kcdxReadyEvent{nullptr};

// One-shot guard for CreateReadyEvent. Ensures a second call is a no-op; the
// event is created exactly once per session.
std::atomic<bool> g_eventCreatedOnce{false};

// One-shot worker-build guard. Ensures BuildEnabledListOnWorker is a no-op on a
// second call (defensive — the call site in dllmain is single, but the guard
// keeps the function honest if some future caller invokes it twice).
std::atomic<bool> g_workerBuiltOnce{false};

}  // namespace

void CreateReadyEvent() {
    // Idempotent guard. A second call is a no-op.
    bool expected = false;
    if (!g_eventCreatedOnce.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
        return;
    }

    // Manual-reset, initially unsignaled. Created HERE — on the worker thread,
    // BEFORE InstallCtorBracket goes live — so the wait gate in HookedCtor
    // observes a non-null handle the moment the bracket can fire. If creation
    // were deferred to BuildEnabledListOnWorker (which runs AFTER
    // InstallCtorBracket, after DiscoverAndLoad), a game-thread call arriving
    // in the install→build window would see g_kcdxReadyEvent == null, skip the
    // wait, and race the worker's build (empty enabled list, zero mods mounted).
    // Co-locating creation with the consumer + sequencing it before the install
    // closes that window.
    HANDLE h = CreateEventW(nullptr, /*manualReset=*/TRUE,
                            /*initialState=*/FALSE, nullptr);
    if (!h) {
        DWORD err = GetLastError();
        LOG_ERROR_KV(kCat, "enabled_list_signal_failed",
                     kcdx::log::KV::BareStr("stage",   "CreateEventW"),
                     kcdx::log::KV("win32_err", (uint64_t)err),
                     kcdx::log::KV::BareStr("detail",
                         "CreateEventW for the ctor-bracket readiness event "
                         "failed — HookedCtor will skip the wait and proceed "
                         "with whatever g_enabledList holds at that moment "
                         "(empty if BuildEnabledList has not run yet); the "
                         "game may mount no mods this boot"));
        // Leave g_kcdxReadyEvent at its initial null; the acquire-load in
        // HookedCtor will observe null and skip the wait (defensive path).
        return;
    }

    // Release-store so the acquire-load in HookedCtor on the game thread
    // observes the fully-initialized handle (cross-thread visibility).
    g_kcdxReadyEvent.store(h, std::memory_order_release);

    LOG_INFO_KV(kCat, "enabled_list_event_created",
                kcdx::log::KV("tid",
                    (unsigned long long)GetCurrentThreadId()),
                kcdx::log::KV::BareStr("detail",
                    "g_kcdxReadyEvent created (manual-reset, unsignaled) on "
                    "the worker thread BEFORE InstallCtorBracket — the wait "
                    "gate in HookedCtor will observe a non-null handle the "
                    "moment the bracket can fire"));
}

void BuildEnabledListOnWorker() {
    // Idempotent guard. A second call is a no-op — the event is already
    // signaled, and re-running BuildEnabledList would race the live storage
    // the bracket is about to read.
    bool expected = false;
    if (!g_workerBuiltOnce.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
        return;
    }

    LOG_INFO_KV(kCat, "enabled_list_build_start",
                kcdx::log::KV("tid",
                    (unsigned long long)GetCurrentThreadId()),
                kcdx::log::KV::BareStr("detail",
                    "kcdx worker thread is building the enabled list eagerly; "
                    "the ctor-bracket game-thread callback will wait on the "
                    "readiness event before reading it"));

    // The readiness event must have been created earlier on this same worker
    // thread by CreateReadyEvent (called BEFORE InstallCtorBracket). If the
    // handle is null here, either CreateReadyEvent was not called (programming
    // error — the WorkerThread sequence is broken) or CreateEventW itself
    // failed (already logged loud at creation time). Log the programming-error
    // case explicitly so the failure mode is named in the log trail; the build
    // still proceeds (and SetEvent below will no-op on the null handle, which
    // means HookedCtor's wait gate falls back to the defensive skip path).
    HANDLE readyEvent = g_kcdxReadyEvent.load(std::memory_order_acquire);
    if (!readyEvent) {
        LOG_ERROR_KV(kCat, "enabled_list_event_missing",
                     kcdx::log::KV::BareStr("detail",
                         "g_kcdxReadyEvent is null when BuildEnabledListOnWorker "
                         "ran — CreateReadyEvent was not called before this "
                         "point (or its CreateEventW failed and was already "
                         "logged). The ctor-bracket wait gate will fall back "
                         "to the defensive skip path; if the game thread races "
                         "ahead of the build, the engine will see an empty "
                         "enabled list"));
    }

    // Build the enabled list (resolved load order, disabled + failed-synth
    // records excluded). Populates the module-static stores directly so
    // HookedCtor reads them without further work.
    g_enabledList = BuildEnabledList(&g_entries);

    // Per-thread count breakdown for the build summary.
    size_t vanilla = 0, plugins = 0;
    for (const EnabledListEntry& e : g_entries) {
        if (e.isPlugin) ++plugins; else ++vanilla;
    }
    LOG_INFO_KV(kCat, "enabled_list_built",
                kcdx::log::KV("count",   (uint64_t)g_enabledList.size()),
                kcdx::log::KV("vanilla", (uint64_t)vanilla),
                kcdx::log::KV("plugins", (uint64_t)plugins),
                kcdx::log::KV::BareStr("detail",
                    "worker-thread build complete; g_enabledList + g_entries "
                    "are ready for the ctor bracket to read after the "
                    "readiness event is signaled"));

    // Per-record DEBUG breakdown (dev-log-routed). Migrated here from the
    // retired HookedSelect callback so the per-record trail is preserved.
    for (size_t i = 0; i < g_entries.size(); ++i) {
        LOG_DEBUG_KV(kCat, "takeover_record",
                     kcdx::log::KV("idx",  (uint64_t)i),
                     kcdx::log::KV("id",   g_entries[i].id),
                     kcdx::log::KV("path", g_entries[i].rootPathSlash),
                     kcdx::log::KV::BareStr("kind",
                         g_entries[i].isPlugin ? "plugin" : "pak_mod"));
    }

    // Signal readiness. Manual-reset: stays signaled for the rest of the
    // session, so any HookedCtor call that arrives after this point returns
    // from its wait immediately.
    if (readyEvent) {
        if (SetEvent(readyEvent)) {
            LOG_INFO_KV(kCat, "enabled_list_signal",
                        kcdx::log::KV::BareStr("detail",
                            "g_kcdxReadyEvent SetEvented (manual-reset); the "
                            "ctor-bracket game-thread callback can now read "
                            "g_enabledList without blocking"));
        } else {
            DWORD err = GetLastError();
            LOG_ERROR_KV(kCat, "enabled_list_signal_failed",
                         kcdx::log::KV::BareStr("stage",   "SetEvent"),
                         kcdx::log::KV("win32_err", (uint64_t)err),
                         kcdx::log::KV::BareStr("detail",
                             "SetEvent on g_kcdxReadyEvent failed — the "
                             "game-thread HookedCtor wait will block "
                             "INFINITE on this boot; falling back to the "
                             "defensive skip path requires the wait to be "
                             "skipped, which only happens when the event "
                             "handle itself is null"));
        }
    }
}

HANDLE GetReadyEventHandle() {
    return g_kcdxReadyEvent.load(std::memory_order_acquire);
}

const std::vector<void*>& GetEnabledListData() {
    return g_enabledList;
}

}  // namespace kcdx::mod_absorb
