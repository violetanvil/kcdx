#include <windows.h>

#include <string>

#include "config.h"
#include "hooks.h"
#include "log.h"
#include "plugin_loader.h"

namespace {

HMODULE g_self = nullptr;

std::wstring GetSelfDirectory() {
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(g_self, buf, _countof(buf));
    if (n == 0 || n == _countof(buf)) return L".\\";
    std::wstring path(buf, n);
    auto pos = path.find_last_of(L"/\\");
    if (pos == std::wstring::npos) return L".\\";
    return path.substr(0, pos + 1);
}

DWORD WINAPI WorkerThread(LPVOID) {
    std::wstring selfDir = GetSelfDirectory();

    kcdx::log::Init(selfDir);
    kcdx::log::Info("");
    kcdx::log::Info("kcdx.asi loaded");
    char dirUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, selfDir.c_str(), -1, dirUtf8, sizeof(dirUtf8), nullptr, nullptr);
    kcdx::log::InfoF("module directory: %s", dirUtf8);

    // The ASI itself sits in plugins/, plugin subfolders are siblings of kcdx.asi.
    kcdx::config::LoadAllConfigs(selfDir);

    if (!kcdx::hooks::Install()) {
        kcdx::log::Error("hooks::Install failed — no patches will be applied");
        return 1;
    }

    // Plugin DLL discovery + load. Runs after the engine's own hooks are
    // installed so plugins can rely on the MinHook + lua_State infrastructure
    // being present. Plugin_Preload + Plugin_Load fire here, before the first
    // game `update` tick.
    kcdx::plugins::DiscoverAndLoad(selfDir);

    return 0;
}

}  // namespace

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        g_self = hModule;
        DisableThreadLibraryCalls(hModule);
        HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (h) CloseHandle(h);
    }
    return TRUE;
}
