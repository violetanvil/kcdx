#include <windows.h>

#include <string>

#include "config.h"
#include "hooks.h"
#include "log.h"
#include "paths.h"
#include "plugin_loader.h"

DWORD WINAPI WorkerThread(LPVOID) {
    kcdx::paths::Init();

    kcdx::log::Init();
    kcdx::log::Info("");
    kcdx::log::Info("kcdx.asi loaded");
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
