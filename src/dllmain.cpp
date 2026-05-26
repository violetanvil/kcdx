#include <windows.h>

#include <string>

#include "config.h"
#include "crash_guard.h"
#include "hooks.h"
#include "ldr_notify.h"
#include "load_order.h"
#include "log.h"
#include "lua_bind_bytes.h"   // RegisterHandlers() — Kind::Bytes apply handler
#include "lua_bind_hook.h"    // RegisterHandlers() — Kind::Hook apply handler
#include "paths.h"
#include "plugin_loader.h"
#include "save_load_hooks.h"
#include "serialization.h"
#include "watchdog_spawn.h"

#include "probes/bugsplat_ctor_probe.h"  // KEEP — proven before_game-hook install
                                         // machinery; Phase 11 generalizes it
                                         // (docs/outstanding-work/before-game-hooks.md §5)
#include "probes/loc_dump_probe.h"       // loc runtime-dump feature, step 1: minimal
                                         // dev-mode probe (ctor capture + by-ID getter)

DWORD WINAPI WorkerThread(LPVOID) {
    // paths::Init is also called from DllMain (idempotent). Calling it
    // again here is safe and keeps this worker startup self-contained.
    kcdx::paths::Init();

    kcdx::log::Init();
    kcdx::log::Info("");
    kcdx::log::Info("kcdx.dll loaded");

    // Install the unhandled-exception backstop early so even a crash
    // during config load / hook install gets a final log line. The
    // filter chains to the prior handler (BugSplat once KCD2 has
    // installed it).
    kcdx::guard::InstallUnhandledExceptionFilter();

    char pluginsUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, kcdx::paths::PluginsDir().c_str(), -1,
                        pluginsUtf8, sizeof(pluginsUtf8), nullptr, nullptr);
    kcdx::log::InfoF("plugins directory: %s", pluginsUtf8);
    char engineUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, kcdx::paths::EngineDataDir().c_str(), -1,
                        engineUtf8, sizeof(engineUtf8), nullptr, nullptr);
    kcdx::log::InfoF("engine data directory: %s", engineUtf8);

    // LoadAllConfigs has already run inside DllMain (synchronously, so
    // before_game-zoned [[patch]] entries could apply against ntdll /
    // kernel32 / etc. that were already mapped, and the LDR-notification
    // callback could be registered for WHGame.dll). The idempotence
    // guard inside LoadAllConfigs makes this call a no-op that simply
    // logs "skipping (already loaded earlier this session)".
    //
    // We keep the call here so the test-suite reporter, dev_mode flag
    // propagation, etc. all see a uniform code path regardless of
    // whether DllMain's pre-load succeeded or aborted partway. If
    // DllMain ever fails to parse configs (loader-lock edge case
    // someone hits), the worker thread's call is the fallback.
    kcdx::config::LoadAllConfigs(kcdx::paths::PluginsDir());

    // Spawn the external crash-bundle watchdog. It blocks on our
    // process handle and zips up logs + crash artifacts when we die
    // with a non-zero exit code. This catches crash classes that the
    // in-process exception filter can't (fast-fail, stack overflow
    // with no stack left, kernel-level termination). On launch
    // failure we just log a WARN — game still runs, just no
    // auto-bundle. Spawned AFTER LoadAllConfigs so the dev-mode flag
    // we pass it is the final post-config value.
    kcdx::watchdog::Spawn();

    // Wait for KingdomCome.exe's startup code to load WHGame.dll
    // before installing our hooks. The launcher (kcdx.exe) injects
    // kcdx.dll into a CREATE_SUSPENDED game process, so kcdx's
    // DllMain + this worker thread run BEFORE KingdomCome.exe begins
    // executing — WHGame.dll isn't mapped yet. The ldr_notify
    // callback registered in DllMain signals an event the moment
    // WHGame.dll lands; we block here until it does. After
    // ResumeThread() (launcher) → KCD2 startup → LoadLibrary
    // WHGame.dll → notification → SetEvent, this wait returns and
    // hooks::Install proceeds normally.
    //
    // 60s timeout is generous (typical wait is well under a second).
    // On timeout we bail loudly rather than crashing inside
    // hooks::Install's "WHGame.dll not loaded" error path.
    if (!kcdx::ldr_notify::WaitForGameDll(/*timeoutMs=*/60'000)) {
        kcdx::log::Error("worker thread: WHGame.dll did not load within "
                         "60s — aborting hook install. Game will run "
                         "vanilla.");
        return 1;
    }

    if (!kcdx::hooks::Install()) {
        kcdx::log::Error("hooks::Install failed — no patches will be applied");
        return 1;
    }

    // Localization runtime-dump feature, step 1: arm the minimal dev-mode probe
    // (CLocalizedStringsManager ctor capture + by-ID getter slot-1 hook). Runs
    // here, after hooks::Install (WHGame.dll mapped + MinHook initialized), and
    // BEFORE CryEngine's system init constructs the loc manager — so the ctor
    // detour is live when the ctor runs. Dev-mode-gated + idempotent internally;
    // a no-op in production.
    kcdx::probes::loc_dump_probe::Install();

    // Phase 6 save/load lifecycle hooks. ABIs from ROUND 3 RECON via
    // _research/phase6-save-load/phase6_abi_walker.py — full-body capstone analysis,
    // not prologue-shape guessing. SaveGame correctly forwards all 7
    // args. See _research/phase6-save-load/SAVE-LOAD-CANDIDATES.md
    // §"ROUND 3 ABI RECON" for the derivation.
    kcdx::save_load_hooks::Install();

    // Phase 6b kcdxSerializationInterface — subscribes to save/load
    // lifecycle messages for the cosave (.kcdx) read/write pipeline.
    // Must initialize AFTER save_load_hooks::Install so the messages
    // exist; the order also matches the engine-internal listener
    // path (messaging::FireEngineMessage calls serialization
    // synchronously after firing to plugin listeners).
    kcdx::serialization::Init();

    // Register the deferred-apply handlers for the registry Kinds the C++
    // plugin interfaces queue (Kind::Hook via kcdxHookInterface, Kind::Bytes
    // via the future kcdxBytesInterface). These MUST run before
    // DiscoverAndLoad: a C++ DLL's kcdxPlugin_Load (driven below) can install
    // a hook through kcdxHookInterface, which queues a Kind::Hook entry into
    // lua_registry — and lua_registry::Append rejects any Kind with no
    // registered handler. The Lua-side bind() (RegisterKcdxTable, first-update-
    // tick) is too LATE for the C++ Load-time caller; these handlers are engine
    // state, not Lua-surface state, so they register at engine init.
    kcdx::lua_bind_hook::RegisterHandlers();
    kcdx::lua_bind_bytes::RegisterHandlers();

    // Plugin DLL discovery + load. Runs after the engine's own hooks are
    // installed so plugins can rely on the MinHook + lua_State infrastructure
    // being present. Plugin_Preload + Plugin_Load fire here, before the first
    // game `update` tick.
    kcdx::plugins::DiscoverAndLoad(kcdx::paths::PluginsDir());

    return 0;
}

// Synchronous DllMain-phase work for the before_game-zone path.
//
// Reads + parses every plugin's kcdx.toml, computes load-order
// resolution, applies before_game-zoned [[patch]] entries to any
// already-loaded target module, and registers an LdrRegisterDllNotification
// callback so future module loads (notably WHGame.dll) get their
// before_game patches applied right after they're mapped, BEFORE
// their own DllMain runs.
//
// Loader-safety contract: this runs under ntdll's loader lock during
// kcdx.dll's own DllMain. dumpbin /imports kcdx.dll confirms zero
// delay-loaded DLLs — std::filesystem, tomlplusplus, std::string,
// std::vector are all safe. MinHook init / CreateThread / LoadLibrary
// are NOT done here; those stay in the worker thread.
//
// See docs/load-order.md §"Loader-safety contract for before_game zone".
static void RunBeforeGameZoneInDllMain() {
    kcdx::paths::Init();

    // Synchronous config parse. Sets the idempotence flag so the
    // worker thread's later LoadAllConfigs call is a no-op.
    kcdx::config::LoadAllConfigs(kcdx::paths::PluginsDir());

    // load_order::Read + Resolve are called inside LoadAllConfigs, so
    // by this point every plugin has an Effective(zone, priority,
    // enabled) row computed.

    // Apply before_game [[patch]] entries against modules already mapped
    // (ntdll, kernel32, and kcdx.dll itself — the launcher injected us via
    // CreateRemoteThread(LoadLibraryW) before the game's own startup ran).
    kcdx::ldr_notify::ApplyAlreadyLoaded();

    // Register the notification callback for future module loads.
    // WHGame.dll mapping is what triggers any before_game patch
    // targeting it.
    kcdx::ldr_notify::Register();

    // KEEP (PROBE S/T, answered 2026-05-26): install the BugSplat ctor
    // log-only hook at DllMain time (immediate if BugSplat64.dll is
    // already mapped, otherwise via LDR notification). PROBE T CONFIRMED
    // DllMain-time install timing catches the ctor call that worker-thread
    // install (PROBE S) missed. This is the proven before_game-hook install
    // machinery — Phase 11 generalizes it into the real builtin; it is NOT
    // removed here (docs/outstanding-work/before-game-hooks.md §5).
    kcdx::probes::bugsplat_ctor_probe::ArmLdrInstall();
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // before_game-zone path runs unconditionally. Zone is the
        // single source of truth for apply timing: zone=before_game
        // means before WHGame.dll's DllMain, full stop. No env-var
        // gating, no flag — the load order says when the patch
        // applies, and the engine honors it.
        RunBeforeGameZoneInDllMain();

        HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
