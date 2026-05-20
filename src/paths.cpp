#include "paths.h"

#include <windows.h>

#include <filesystem>
#include <string>

namespace kcdx::paths {

namespace fs = std::filesystem;

namespace {

std::wstring g_pluginsDir;     // ASI module dir, with trailing '\\'
std::wstring g_engineDataDir;  // <PluginsDir>/../kcdx-engine/, with trailing '\\'

// Re-derive the ASI module directory using the address of a function
// inside this translation unit.
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

}  // namespace

void Init() {
    if (!g_pluginsDir.empty()) return;  // idempotent

    g_pluginsDir = DeriveSelfDir();

    fs::path sibling = fs::path(g_pluginsDir).parent_path().parent_path()
                       / L"kcdx-engine";
    std::error_code ec;
    fs::create_directories(sibling, ec);
    g_engineDataDir = sibling.wstring();
    if (!g_engineDataDir.empty() && g_engineDataDir.back() != L'\\') {
        g_engineDataDir.push_back(L'\\');
    }
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
