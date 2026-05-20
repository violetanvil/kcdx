#include <windows.h>

#include <string>

#include "config.h"
#include "crash_guard.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"
#include "plugin_loader.h"
#include "save_load_hooks.h"
#include "serialization.h"

DWORD WINAPI WorkerThread(LPVOID) {
    kcdx::paths::Init();

    kcdx::log::Init();
    kcdx::log::Info("");
    kcdx::log::Info("kcdx.asi loaded");

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

    // The ASI itself sits in plugins/, plugin subfolders are siblings of kcdx.asi.
    kcdx::config::LoadAllConfigs(kcdx::paths::PluginsDir());

    if (!kcdx::hooks::Install()) {
        kcdx::log::Error("hooks::Install failed — no patches will be applied");
        return 1;
    }

    // Phase 6 save/load lifecycle hooks. ABIs from ROUND 3 RECON via
    // _research/phase6_abi_walker.py — full-body capstone analysis,
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

    // Plugin DLL discovery + load. Runs after the engine's own hooks are
    // installed so plugins can rely on the MinHook + lua_State infrastructure
    // being present. Plugin_Preload + Plugin_Load fire here, before the first
    // game `update` tick.
    kcdx::plugins::DiscoverAndLoad(kcdx::paths::PluginsDir());

    return 0;
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
