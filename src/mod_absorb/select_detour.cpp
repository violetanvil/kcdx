#include "select_detour.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstring>
#include <vector>

#include "MinHook.h"

#include "ctor_probe.h"
#include "enabled_list_builder.h"
#include "../address_library.h"
#include "../log.h"

// SELECT detour — see select_detour.h for the full framing. Production
// mod-loader takeover: detour ModManager_Select (Address Library id 3100), let
// the original run, then wholesale-replace the enabled-list vector with kcdx's
// rebuilt list in resolved load order. docs/mod-loader-absorb.md "Step 4".
//
// Threading model (two threads, ONE wait point — parallel by default):
//   WORKER thread (kcdx's WorkerThread)
//     - CreateReadyEvent() creates g_kcdxReadyEvent (manual-reset, unsignaled)
//       BEFORE InstallSelectDetour goes live. This is what makes the wait gate
//       in HookedSelect honour its contract: by the time the detour can fire,
//       the event handle exists. An earlier reorder regression that moved the
//       SELECT-detour install too late observed the race this contract closes;
//       the present design installs the detour and creates the event before
//       DiscoverAndLoad runs, so the race window does not exist.
//     - DiscoverAndLoad + the full plugin load + version gate run.
//     - BuildEnabledListOnWorker() runs the BUILD: calls BuildEnabledList(...)
//       into the module-static g_enabledList + g_entries, then SetEvents
//       g_kcdxReadyEvent. Idempotent.
//   GAME main thread (CSystem::Init), inside HookedSelect
//     - Runs the ctor-probe ride-along (still the existing TRANSIENT probe).
//     - Runs the original SELECT (native records + per-mod validation pass)
//       in PARALLEL with the worker's build — the two touch disjoint state
//       (engine-owned C_ModManager records vs kcdx-owned g_enabledList +
//       g_entries) so concurrent execution is safe.
//     - WaitForSingleObject(g_kcdxReadyEvent, INFINITE) — the only wait point,
//       gating the wholesale-replace step only. On a populated tree, the
//       worker is typically still building when orig returns; the wait
//       blocks for the wall-clock difference between orig duration and
//       worker-build duration (typically ~1-2s on a populated tree). On a
//       clean install with no plugins, the worker is past SetEvent already
//       and the wait returns immediately.
//     - Wholesale-REPLACES the engine vector with kcdx's pre-built array.
// Manual-reset event: stays signaled after the first SetEvent, so a re-entry
// SELECT call's wait returns immediately (and the g_tookOver one-shot latch
// still keeps the actual repoint single-shot).

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// ModManager_Select — Address Library id 3100. Resolved at install time (never
// a hardcoded RVA — the row carries the per-build address + the verified ABI).
constexpr uint64_t kSelectId = 3100;

// The enabled-mod list at C_ModManager+0x30 is a std::vector-style range of
// 8-byte I_Mod pointers: +0x30 = begin, +0x38 = end, +0x40 = end_of_storage
// (capacity). count = (end - begin) / 8. The vector layout at these three
// offsets is the takeover's load-bearing fact (the I_Mod record layout is
// docs/mod-loader-absorb.md "The I_Mod record layout").
constexpr size_t kListBeginOff   = 0x30;
constexpr size_t kListEndOff     = 0x38;
constexpr size_t kListEndCapOff  = 0x40;

// __fastcall void(C_ModManager* this) — this-only, void return (Address Library
// id 3100, verified ABI).
using SelectFn_t = void (__fastcall*)(void* self);

std::atomic<SelectFn_t> g_orig{nullptr};
std::atomic<bool>       g_installed{false};
std::atomic<bool>       g_tookOver{false};  // one-shot rebuild latch

// === kcdx-OWNED, PROCESS-LIFETIME enabled-list array ========================
//
// The engine vector is repointed at &g_enabledList[0] .. &g_enabledList[N].
// This vector MUST outlive MOUNT + every downstream pass that walks the list,
// so it is module-static (process lifetime) — NEVER built on the stack. The
// I_Mod* elements point at records record_synth owns process-lifetime; this
// array owns only the POINTER storage. It is filled exactly once (the one-shot
// g_tookOver latch) and never reallocated after the repoint.
std::vector<void*> g_enabledList;

// A stable, non-null module-static address for the EMPTY-list case (begin ==
// end == cap point here, a valid empty range). Avoids writing a possibly-null
// vector::data() as begin; the engine computes count = (end-begin)/8 = 0 and
// never dereferences it.
void* g_emptySentinel = nullptr;

// Parallel diagnostic vector. Built by the worker (BuildEnabledListOnWorker)
// alongside g_enabledList; read by HookedSelect AFTER the readiness wait, for
// the per-record DEBUG breakdown + vanilla/plugin counts. Module-static
// (process-lifetime) so it survives the worker -> game-thread handoff. NOT
// touched by the engine — kcdx-internal diagnostics only.
std::vector<EnabledListEntry> g_entries;

// Readiness event the SELECT-detour callback waits on. Manual-reset, initially
// unsignaled. CreateReadyEvent (called by the worker BEFORE InstallSelectDetour)
// creates the handle and stores it with release ordering; HookedSelect loads it
// with acquire ordering before checking + waiting (the worker creates on one
// thread, the game-thread callback reads on another — std::atomic establishes
// the happens-before edge so the write is visible without relying on hardware
// memory-model accidents). SetEvent fires once at the end of
// BuildEnabledListOnWorker; the event stays signaled (manual-reset) for the
// rest of the session so any re-entry SELECT call's wait returns immediately.
std::atomic<HANDLE> g_kcdxReadyEvent{nullptr};

// One-shot guard for CreateReadyEvent. Ensures a second call is a no-op; the
// event is created exactly once per session.
std::atomic<bool> g_eventCreatedOnce{false};

// One-shot worker-build guard. Ensures BuildEnabledListOnWorker is a no-op on a
// second call (defensive — the call site in dllmain is single, but the guard
// keeps the function honest if some future caller invokes it twice).
std::atomic<bool> g_workerBuiltOnce{false};

void* ReadPtr(const uint8_t* base, size_t off) {
    void* v = nullptr;
    std::memcpy(&v, base + off, sizeof(v));
    return v;
}

void WritePtr(uint8_t* base, size_t off, const void* value) {
    std::memcpy(base + off, &value, sizeof(value));
}

void __fastcall HookedSelect(void* self) {
    // POINT B observation — read the C_ModManager state BEFORE the original
    // SELECT runs (post-ctor zero-init, pre-SELECT-body). One-shot inside the
    // probe; observe-only; never mutates `self`. Riding the existing detour
    // (one MinHook per site) — see ctor_probe.h and the init-cycle-ownership
    // outstanding-work doc. Deleted with the rest of the probe in step 4.
    //
    // MUST happen BEFORE the readiness wait below: the probe captures
    // pre-SELECT C_ModManager state, and the wait should not delay that
    // snapshot (the worker's build does not depend on this probe and vice
    // versa).
    kcdx::mod_absorb::ctor_probe::OnSelectEntry(self);

    // 1. Run the ORIGINAL SELECT first — it builds the native records AND runs
    //    the per-mod validation pass. The list MUST NOT be mutated before that
    //    completes (growing it mid-validation crashes the engine's own walk);
    //    wholesale-replace is safe only AFTER the original returns. Running
    //    orig BEFORE the wait below is the parallel order: the native
    //    validation pass touches engine-owned C_ModManager records, the
    //    worker's build populates the kcdx-owned g_enabledList + g_entries —
    //    disjoint state, safe to run concurrently. On a populated tree the
    //    wait below is hidden behind orig's wall-clock cost.
    SelectFn_t orig = g_orig.load(std::memory_order_acquire);
    if (!orig) {
        kcdx::log::Error("MOD_ABSORB: orig SELECT pointer null at dispatch — "
                         "cannot take over the enabled list this boot");
        return;
    }
    orig(self);

    // 2. Wait for the worker thread to finish building g_enabledList +
    //    g_entries. This wait blocks the REPLACE step below (not the orig call
    //    above — orig already ran in parallel with the worker's build). On a
    //    populated tree the worker is typically still building when orig
    //    returns, so this wait blocks for the wall-clock difference between
    //    orig duration and worker-build duration (typically ~1-2s on a
    //    populated tree). On a clean install with no plugins the worker is
    //    past SetEvent already and the wait returns immediately.
    //
    //    INFINITE is correct: the worker WILL signal unless it hangs entirely
    //    (in which case the game already cannot init). Acquire-load the
    //    handle into a local once so a hypothetical second read cannot
    //    observe a different value; pair with the release-store in
    //    CreateReadyEvent to make the cross-thread write visible. The
    //    defensive `if (handle)` guard handles the unlikely-but-possible
    //    CreateEventW-failed case (logged loud at creation); the COMMON path
    //    is the gate being TRUE because CreateReadyEvent ran on the worker
    //    before InstallSelectDetour put this detour live.
    HANDLE readyEvent = g_kcdxReadyEvent.load(std::memory_order_acquire);
    if (readyEvent) {
        LOG_DEBUG_KV(kCat, "enabled_list_wait_enter",
                     kcdx::log::KV("detail",
                         "HookedSelect about to WaitForSingleObject on "
                         "g_kcdxReadyEvent (INFINITE) — confirms the wait is "
                         "entered; on a clean install with non-trivial plugins "
                         "this BLOCKS until BuildEnabledListOnWorker signals "
                         "(typically ~1-2s; the game thread leads the worker)"));
        WaitForSingleObject(readyEvent, INFINITE);
    }

    // 3. One-shot: rebuild + replace exactly once. SELECT may be reachable more
    //    than once across a session; the takeover applies to the first (boot)
    //    selection.
    bool expected = false;
    if (!g_tookOver.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }
    if (!self) {
        kcdx::log::Error("MOD_ABSORB: SELECT self (C_ModManager) is null — "
                         "cannot repoint the enabled list");
        return;
    }

    auto* base = reinterpret_cast<uint8_t*>(self);

    // Snapshot the original vector pointers (for the log + so the original
    // native records are not silently lost from the diagnostic record).
    void* origBegin = ReadPtr(base, kListBeginOff);
    void* origEnd   = ReadPtr(base, kListEndOff);
    void* origCap   = ReadPtr(base, kListEndCapOff);
    uint64_t origCount = 0;
    if (origBegin && origEnd && origEnd >= origBegin) {
        origCount = (reinterpret_cast<uintptr_t>(origEnd) -
                     reinterpret_cast<uintptr_t>(origBegin)) / 8;
    }

    // 4. The rebuilt enabled list is already in g_enabledList + g_entries
    //    (built by the worker via BuildEnabledListOnWorker, signaled via
    //    g_kcdxReadyEvent above). Just read it.
    const size_t n = g_enabledList.size();

    // Per-record DEBUG breakdown (dev-log-routed). Plain id + path; no probe
    // framing.
    size_t vanilla = 0, plugins = 0;
    for (size_t i = 0; i < g_entries.size(); ++i) {
        if (g_entries[i].isPlugin) ++plugins; else ++vanilla;
        LOG_DEBUG_KV(kCat, "takeover_record",
                     kcdx::log::KV("idx", (uint64_t)i),
                     kcdx::log::KV("id", g_entries[i].id),
                     kcdx::log::KV("path", g_entries[i].rootPathSlash),
                     kcdx::log::KV("kind", g_entries[i].isPlugin ? "plugin" : "pak_mod"));
    }

    // 5. Repoint the vector at the kcdx-owned array. begin = &array[0];
    //    end = end_of_storage = &array[N]. An EMPTY rebuilt list (n == 0) would
    //    leave the engine with zero enabled mods — repoint all three at a stable
    //    non-null sentinel address so begin == end == cap (a valid empty vector)
    //    rather than writing a dangling &array[0] on an empty std::vector.
    if (n == 0) {
        // Empty list: point begin == end == cap at a stable, non-null
        // module-static address; the engine computes count = (end-begin)/8 = 0
        // and mounts nothing (never dereferences the address). Surface it LOUD:
        // zero enabled mods is a real, observable state (every mod disabled /
        // version-rejected, or every record's synthesis failed), not a silent
        // no-op.
        void* emptyAt = static_cast<void*>(&g_emptySentinel);
        WritePtr(base, kListBeginOff,  emptyAt);
        WritePtr(base, kListEndOff,    emptyAt);
        WritePtr(base, kListEndCapOff, emptyAt);
        kcdx::log::WarnF(
            "MOD_ABSORB: kcdx mod-loader takeover rebuilt an EMPTY enabled list "
            "(0 mods) — every discovered mod was disabled, version-rejected, or "
            "failed record synthesis; the game will mount no mods this boot "
            "(original native count was %llu)",
            (unsigned long long)origCount);
        return;
    }

    void* newBegin = static_cast<void*>(&g_enabledList[0]);
    void* newEnd   = static_cast<void*>(&g_enabledList[0] + n);
    WritePtr(base, kListBeginOff,  newBegin);
    WritePtr(base, kListEndOff,    newEnd);
    WritePtr(base, kListEndCapOff, newEnd);

    LOG_INFO_KV(kCat, "takeover_repoint",
                kcdx::log::KV("orig_begin", origBegin),
                kcdx::log::KV("orig_end",   origEnd),
                kcdx::log::KV("orig_cap",   origCap),
                kcdx::log::KV("orig_count", origCount),
                kcdx::log::KV("new_begin",  newBegin),
                kcdx::log::KV("new_end",    newEnd),
                kcdx::log::KV("new_count",  (uint64_t)n));

    kcdx::log::InfoF(
        "kcdx mod-loader takeover: rebuilt enabled list, %llu mods "
        "(%zu vanilla, %zu plugins) in kcdx load order",
        (unsigned long long)n, vanilla, plugins);
}

}  // namespace

void CreateReadyEvent() {
    // Idempotent guard. A second call is a no-op.
    bool expected = false;
    if (!g_eventCreatedOnce.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
        return;
    }

    // Manual-reset, initially unsignaled. Created HERE — on the worker thread,
    // BEFORE InstallSelectDetour goes live — so the wait gate in HookedSelect
    // observes a non-null handle the moment the detour can fire. If creation
    // were deferred to BuildEnabledListOnWorker (which runs AFTER
    // InstallSelectDetour, after DiscoverAndLoad), a game-thread SELECT call
    // arriving in the install→build window would see g_kcdxReadyEvent == null,
    // skip the wait, and race the worker's build (empty enabled list, zero
    // mods mounted). Co-locating creation with the consumer + sequencing it
    // before the install closes that window.
    HANDLE h = CreateEventW(nullptr, /*manualReset=*/TRUE,
                            /*initialState=*/FALSE, nullptr);
    if (!h) {
        DWORD err = GetLastError();
        LOG_ERROR_KV(kCat, "enabled_list_signal_failed",
                     kcdx::log::KV("stage",   "CreateEventW"),
                     kcdx::log::KV("win32_err", (uint64_t)err),
                     kcdx::log::KV("detail",
                         "CreateEventW for the SELECT-detour readiness event "
                         "failed — HookedSelect will skip the wait and fall "
                         "back to the existing g_tookOver one-shot path; the "
                         "game-thread callback may run before the worker "
                         "finishes building the list, in which case the "
                         "fallback repoints whatever g_enabledList holds "
                         "(empty if BuildEnabledList has not run yet)"));
        // Leave g_kcdxReadyEvent at its initial null; the acquire-load in
        // HookedSelect will observe null and skip the wait (defensive path).
        return;
    }

    // Release-store so the acquire-load in HookedSelect on the game thread
    // observes the fully-initialized handle (cross-thread visibility).
    g_kcdxReadyEvent.store(h, std::memory_order_release);

    LOG_INFO_KV(kCat, "enabled_list_event_created",
                kcdx::log::KV("tid",
                    (unsigned long long)GetCurrentThreadId()),
                kcdx::log::KV("detail",
                    "g_kcdxReadyEvent created (manual-reset, unsignaled) on "
                    "the worker thread BEFORE InstallSelectDetour — the wait "
                    "gate in HookedSelect will observe a non-null handle the "
                    "moment the detour can fire"));
}

void BuildEnabledListOnWorker() {
    // Idempotent guard. A second call is a no-op — the event is already
    // signaled, and re-running BuildEnabledList would race the live engine
    // vector that already points at g_enabledList[].
    bool expected = false;
    if (!g_workerBuiltOnce.compare_exchange_strong(expected, true,
                                                   std::memory_order_acq_rel)) {
        return;
    }

    LOG_INFO_KV(kCat, "enabled_list_build_start",
                kcdx::log::KV("tid",
                    (unsigned long long)GetCurrentThreadId()),
                kcdx::log::KV("detail",
                    "kcdx worker thread is building the rebuilt enabled list "
                    "eagerly; the SELECT detour's game-thread callback will "
                    "wait on the readiness event before reading it"));

    // The readiness event must have been created earlier on this same worker
    // thread by CreateReadyEvent (called BEFORE InstallSelectDetour). If the
    // handle is null here, either CreateReadyEvent was not called (programming
    // error — the WorkerThread sequence is broken) or CreateEventW itself
    // failed (already logged loud at creation time). Log the programming-error
    // case explicitly so the failure mode is named in the log trail; the build
    // still proceeds (and SetEvent below will no-op on the null handle, which
    // means HookedSelect's wait gate falls back to the defensive skip path).
    HANDLE readyEvent = g_kcdxReadyEvent.load(std::memory_order_acquire);
    if (!readyEvent) {
        LOG_ERROR_KV(kCat, "enabled_list_event_missing",
                     kcdx::log::KV("detail",
                         "g_kcdxReadyEvent is null when BuildEnabledListOnWorker "
                         "ran — CreateReadyEvent was not called before this "
                         "point (or its CreateEventW failed and was already "
                         "logged). The SELECT detour wait gate will fall back "
                         "to the defensive skip path; if the game thread races "
                         "ahead of the build, the engine will see an empty "
                         "enabled list"));
    }

    // Build the rebuilt enabled list (resolved load order, disabled +
    // failed-synth records excluded). Populates the module-static stores
    // directly so HookedSelect reads them without further work.
    g_enabledList = BuildEnabledList(&g_entries);

    // Per-thread count breakdown for the build summary (mirrors the
    // post-replace summary HookedSelect emits, just from the worker side, so a
    // log trace shows the build-time + replace-time counts and they match).
    size_t vanilla = 0, plugins = 0;
    for (const EnabledListEntry& e : g_entries) {
        if (e.isPlugin) ++plugins; else ++vanilla;
    }
    LOG_INFO_KV(kCat, "enabled_list_built",
                kcdx::log::KV("count",   (uint64_t)g_enabledList.size()),
                kcdx::log::KV("vanilla", (uint64_t)vanilla),
                kcdx::log::KV("plugins", (uint64_t)plugins),
                kcdx::log::KV("detail",
                    "worker-thread build complete; g_enabledList + g_entries "
                    "are ready for HookedSelect to read after the readiness "
                    "event is signaled"));

    // Signal readiness. Manual-reset: stays signaled for the rest of the
    // session, so any HookedSelect call that arrives after this point returns
    // from its wait immediately. Re-using the local acquire-loaded handle (no
    // second atomic read — see the readEvent local above).
    if (readyEvent) {
        if (SetEvent(readyEvent)) {
            LOG_INFO_KV(kCat, "enabled_list_signal",
                        kcdx::log::KV("detail",
                            "g_kcdxReadyEvent SetEvented (manual-reset); the "
                            "SELECT-detour game-thread callback can now read "
                            "g_enabledList without blocking"));
        } else {
            DWORD err = GetLastError();
            LOG_ERROR_KV(kCat, "enabled_list_signal_failed",
                         kcdx::log::KV("stage",   "SetEvent"),
                         kcdx::log::KV("win32_err", (uint64_t)err),
                         kcdx::log::KV("detail",
                             "SetEvent on g_kcdxReadyEvent failed — the "
                             "game-thread HookedSelect wait will block "
                             "INFINITE on this boot; falling back to the "
                             "existing g_tookOver one-shot path requires the "
                             "wait to be skipped, which only happens when the "
                             "event handle itself is null"));
        }
    }
}

bool InstallSelectDetour() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        kcdx::log::Warn("MOD_ABSORB: WHGame.dll not mapped yet; cannot install "
                        "the SELECT detour — mod-loader takeover inactive");
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // Resolve the SELECT address by Address Library id (never a hardcoded RVA —
    // the row carries the per-build address gated on a game-version match).
    const uintptr_t target = address_library::Resolve(kSelectId);
    if (target == 0) {
        kcdx::log::WarnF("MOD_ABSORB: ModManager_Select (Address Library id "
                         "%llu) did not resolve (version mismatch / unverified "
                         "row) — mod-loader takeover inactive this boot",
                         (unsigned long long)kSelectId);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook idempotent init (the worker-thread caller already initialized it).
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        kcdx::log::WarnF("MOD_ABSORB: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    void* targetPtr = reinterpret_cast<void*>(target);
    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(targetPtr,
                                reinterpret_cast<void*>(&HookedSelect),
                                &origPtr);
    if (s != MH_OK) {
        kcdx::log::WarnF("MOD_ABSORB: MH_CreateHook(SELECT @ %p) failed: %d "
                         "— mod-loader takeover inactive", targetPtr, (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig.store(reinterpret_cast<SelectFn_t>(origPtr), std::memory_order_release);

    s = MH_EnableHook(targetPtr);
    if (s != MH_OK) {
        kcdx::log::WarnF("MOD_ABSORB: MH_EnableHook(SELECT @ %p) failed: %d "
                         "— mod-loader takeover inactive", targetPtr, (int)s);
        return false;
    }

    kcdx::log::InfoF("MOD_ABSORB: ModManager_Select detour installed at %p "
                     "(Address Library id %llu) — mod-loader takeover armed",
                     targetPtr, (unsigned long long)kSelectId);
    return true;
}

}  // namespace kcdx::mod_absorb
