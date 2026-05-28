#include "paths.h"

#include <windows.h>

#include <cstdio>
#include <filesystem>
#include <string>

namespace kcdx::paths {

namespace fs = std::filesystem;

namespace {

// Install layout:
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
//
// Fail-state (Batch F #18): a CWD-relative ".\\" fallback resolves the WHOLE
// engine layout (engine dir / plugins dir / builtin dir) relative to the
// game's working directory instead of next to kcdx.dll — silently wrong, no
// crash. This runs pre-log (DllMain phase, before log::Init), so OutputDebug-
// StringA is the only sink. The text carries an [ERROR]-equivalent tag because
// ODS has no severity field.
std::wstring DeriveSelfDir() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&DeriveSelfDir),
        &hMod);
    wchar_t buf[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(hMod, buf, _countof(buf));
    if (n == 0 || n == _countof(buf)) {
        DWORD err = GetLastError();
        char ods[256];
        snprintf(ods, sizeof(ods),
                 "[kcdx][ERROR] paths: GetModuleFileNameW failed (n=%lu, "
                 "err=%lu) deriving kcdx.dll's own directory; falling back to "
                 "CWD-relative \".\\\\\" — the ENTIRE engine layout (engine/"
                 "plugins/builtin dirs) will resolve relative to the game's "
                 "working directory, not next to kcdx.dll. Plugins and engine "
                 "data may not be found.\n",
                 n, err);
        OutputDebugStringA(ods);
        return L".\\";
    }
    std::wstring path(buf, n);
    auto pos = path.find_last_of(L"/\\");
    if (pos == std::wstring::npos) {
        OutputDebugStringA(
            "[kcdx][ERROR] paths: kcdx.dll module path has no path separator; "
            "falling back to CWD-relative \".\\\\\" — the ENTIRE engine layout "
            "(engine/plugins/builtin dirs) will resolve relative to the game's "
            "working directory, not next to kcdx.dll. Plugins and engine data "
            "may not be found.\n");
        return L".\\";
    }
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

    // Fail-state (Batch F #18): a create_directories failure left the layout
    // pointing at a directory that does not exist — a later plugin-discovery or
    // log-open silently finds nothing / drops, with no signal here. Check each
    // ec individually and name WHICH dir failed. Pre-log (DllMain phase) →
    // OutputDebugStringA is the floor; the [ERROR] tag substitutes for ODS's
    // missing severity field. (Init is also called idempotently post-log from
    // WorkerThread, but the FIRST call is pre-log, so ODS is correct in both —
    // ODS is always available, the file log just is not up the first time.)
    auto reportDirFail = [](const char* which, const fs::path& p,
                            const std::error_code& ec) {
        char ods[512];
        std::string ps = p.string();
        snprintf(ods, sizeof(ods),
                 "[kcdx][ERROR] paths: failed to create %s directory '%s' "
                 "(ec=%d: %s); plugins/logs/builtin data under it will be "
                 "missing this session.\n",
                 which, ps.c_str(), ec.value(), ec.message().c_str());
        OutputDebugStringA(ods);
    };

    std::error_code ec;
    fs::create_directories(engineDir, ec);
    if (ec) reportDirFail("engine", engineDir, ec);
    ec.clear();
    fs::create_directories(pluginDir, ec);
    if (ec) reportDirFail("plugins", pluginDir, ec);
    ec.clear();
    fs::create_directories(builtinDir, ec);
    if (ec) reportDirFail("builtin", builtinDir, ec);

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

namespace {

// Read a REG_SZ value from a registry key under HKEY_LOCAL_MACHINE or
// HKEY_CURRENT_USER. Returns the wide-string value on success, or an empty
// string if the key/value is absent or the value type is not REG_SZ /
// REG_EXPAND_SZ. The KCD2 appid (1771300) workshop dir composition is the only
// caller, so this stays here as a TU-private helper instead of bloating the
// public paths.h surface.
std::wstring ReadRegistryStringValue(HKEY rootKey, const wchar_t* subKey,
                                     const wchar_t* valueName) {
    HKEY hKey = nullptr;
    LONG status = RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE | KEY_WOW64_32KEY, &hKey);
    if (status != ERROR_SUCCESS) {
        // Steam's registry layout lives under WOW6432Node on a 64-bit OS for
        // HKLM but is plain on HKCU. Retry without the 32-bit view flag if the
        // first probe missed; this covers both placements.
        status = RegOpenKeyExW(rootKey, subKey, 0, KEY_QUERY_VALUE, &hKey);
        if (status != ERROR_SUCCESS) return {};
    }
    DWORD type = 0;
    DWORD cbData = 0;
    status = RegQueryValueExW(hKey, valueName, nullptr, &type, nullptr, &cbData);
    if (status != ERROR_SUCCESS ||
        (type != REG_SZ && type != REG_EXPAND_SZ) ||
        cbData == 0) {
        RegCloseKey(hKey);
        return {};
    }
    // cbData is the byte size INCLUDING the (possibly-absent) null terminator.
    // Size the buffer one wchar larger than the reported size so a value Steam
    // wrote WITHOUT a trailing NUL still leaves the tail wchar zero — the
    // strip-trailing-NULs loop then yields the right string either way.
    std::wstring buf;
    const DWORD bufWchars = (cbData / sizeof(wchar_t)) + 1;
    buf.resize(bufWchars, L'\0');
    DWORD cbBuf = bufWchars * static_cast<DWORD>(sizeof(wchar_t));
    status = RegQueryValueExW(hKey, valueName, nullptr, &type,
                              reinterpret_cast<LPBYTE>(buf.data()), &cbBuf);
    RegCloseKey(hKey);
    if (status != ERROR_SUCCESS) return {};
    // Strip trailing NULs (RegQueryValueExW may or may not include them).
    while (!buf.empty() && buf.back() == L'\0') buf.pop_back();
    return buf;
}

}  // namespace

std::wstring WorkshopContentDir() {
    // Try HKLM first (Steam's installer writes here on most 64-bit systems),
    // then HKCU (per-user installs / non-admin setups). The value-name differs
    // between the two paths Steam writes:
    //   HKLM\Software\Valve\Steam     -> 'InstallPath'
    //   HKCU\Software\Valve\Steam     -> 'SteamPath'
    // (KEY_WOW64_32KEY handles the WOW6432Node redirection for HKLM on 64-bit OS.)
    std::wstring steamRoot =
        ReadRegistryStringValue(HKEY_LOCAL_MACHINE,
                                L"Software\\Valve\\Steam",
                                L"InstallPath");
    if (steamRoot.empty()) {
        steamRoot = ReadRegistryStringValue(HKEY_CURRENT_USER,
                                            L"Software\\Valve\\Steam",
                                            L"SteamPath");
    }
    if (steamRoot.empty()) return {};

    // Compose <Steam>/steamapps/workshop/content/1771300/. The KCD2 Steam
    // appid is 1771300 (verified in-store). Workshop subscriptions Steam
    // downloads land here, one immediate subdirectory per Workshop file ID
    // (the subdirectory NAME is the Workshop ID).
    fs::path workshopPath(steamRoot);
    workshopPath /= L"steamapps";
    workshopPath /= L"workshop";
    workshopPath /= L"content";
    workshopPath /= L"1771300";

    std::error_code ec;
    if (!fs::exists(workshopPath, ec) || !fs::is_directory(workshopPath, ec)) {
        return {};
    }
    return AppendDir(workshopPath);
}

fs::path GameRootDirPath() {
    // EngineDataDir = <game-root>/Bin/<flavour>/kcdx-engine/ (trailing '\\').
    // Climb out of the bin layout: kcdx-engine -> game-bin -> Bin -> game-root.
    //
    // Trim the trailing separator FIRST (same as Init() does before its own
    // parent_path climb) so the path's last component is "kcdx-engine", not an
    // empty trailing element — then three parent_path() steps land exactly on
    // game-root, with no dependence on how ".." normalization treats a trailing
    // slash.
    std::wstring engine = g_engineDataDir;
    while (!engine.empty() && (engine.back() == L'\\' || engine.back() == L'/')) {
        engine.pop_back();
    }
    fs::path engineDir(engine);                    // ...\kcdx-engine
    return engineDir.parent_path()                 // ...\<flavour> (game-bin)
                    .parent_path()                 // ...\Bin
                    .parent_path();                // <game-root>
}

}  // namespace kcdx::paths
