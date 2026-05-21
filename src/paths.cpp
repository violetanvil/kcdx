#include "paths.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace kcdx::paths {

namespace fs = std::filesystem;

namespace {

// New install layout (Phase 1+):
//   <game-bin>/
//   ├── kcdx.exe              (launcher; user runs this)
//   ├── kcdx-engine/          (everything kcdx-owned)
//   │   ├── kcdx.dll          (engine; injected by launcher; we live here)
//   │   ├── kcdx-watchdog.exe
//   │   ├── engine.toml
//   │   ├── load_order.toml
//   │   ├── logs/
//   │   ├── address-library/
//   │   └── builtin/          (first-party engine fixes)
//   └── kcdx-plugins/         (user/third-party plugins only)
//       └── <plugin>/...
//
// The engine's perspective: self lives at <game-bin>/kcdx-engine/kcdx.dll.
// - engineDataDir = <game-bin>/kcdx-engine/   (same folder as self)
// - pluginsDir    = <game-bin>/kcdx-plugins/  (sibling of kcdx-engine/)
// - builtinDir    = <game-bin>/kcdx-engine/builtin/

std::wstring g_engineDataDir;  // <game-bin>/kcdx-engine/   (trailing '\\')
std::wstring g_pluginsDir;     // <game-bin>/kcdx-plugins/  (trailing '\\')
std::wstring g_builtinDir;     // <game-bin>/kcdx-engine/builtin/ (trailing '\\')

// Re-derive kcdx.dll's directory using the address of a function in this TU.
std::wstring DeriveSelfDir() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&DeriveSelfDir),
        &hMod);
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(hMod, buf, _countof(buf));
    if (n == 0 || n == _countof(buf)) return L".\\";
    std::wstring path(buf, n);
    auto pos = path.find_last_of(L"/\\");
    if (pos == std::wstring::npos) return L".\\";
    return path.substr(0, pos + 1);
}

std::wstring AppendDir(const fs::path& p) {
    std::wstring s = p.wstring();
    if (!s.empty() && s.back() != L'\\' && s.back() != L'/') s.push_back(L'\\');
    return s;
}

}  // namespace

void Init() {
    if (!g_engineDataDir.empty()) return;  // idempotent

    // self lives at <game-bin>/kcdx-engine/kcdx.dll. DeriveSelfDir returns
    // the path WITH a trailing slash; trim it so fs::path::parent_path
    // returns the parent directory, not the same path with the slash
    // shaved off.
    std::wstring selfDir = DeriveSelfDir();  // <game-bin>/kcdx-engine/
    while (!selfDir.empty()
           && (selfDir.back() == L'\\' || selfDir.back() == L'/')) {
        selfDir.pop_back();
    }

    fs::path engineDir(selfDir);                     // <game-bin>/kcdx-engine
    fs::path gameBin    = engineDir.parent_path();   // <game-bin>
    fs::path pluginDir  = gameBin / L"kcdx-plugins";
    fs::path builtinDir = engineDir / L"builtin";

    std::error_code ec;
    fs::create_directories(engineDir,  ec);
    fs::create_directories(pluginDir,  ec);
    fs::create_directories(builtinDir, ec);

    g_engineDataDir = AppendDir(engineDir);
    g_pluginsDir    = AppendDir(pluginDir);
    g_builtinDir    = AppendDir(builtinDir);
}

const std::wstring& PluginsDir() {
    return g_pluginsDir;
}

const std::wstring& EngineDataDir() {
    return g_engineDataDir;
}

fs::path PluginsDirPath() {
    fs::path p(g_pluginsDir);
    return p;
}

fs::path EngineDataDirPath() {
    fs::path p(g_engineDataDir);
    return p;
}

}  // namespace kcdx::paths
