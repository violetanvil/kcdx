#include "plugin_loader.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "log.h"
#include "messaging.h"
#include "pe_helpers.h"

namespace fs = std::filesystem;

namespace kcdx::plugins {

std::vector<LoadedPlugin>   g_plugins;
std::vector<PluginManifest> g_manifests;
uint32_t                    g_runtimeGameVersion = 0;

namespace {

std::string WideToUtf8(const std::wstring& w) {
    if (w.empty()) return {};
    int n = WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, nullptr, 0, nullptr, nullptr);
    std::string out(n > 0 ? n - 1 : 0, '\0');
    if (n > 0) {
        WideCharToMultiByte(CP_UTF8, 0, w.c_str(), -1, out.data(), n, nullptr, nullptr);
    }
    return out;
}

// Locate WHGame.dll's full path. Used by both version-detection paths.
// Returns empty wstring on failure.
std::wstring LocateWHGamePath() {
    HMODULE h = GetModuleHandleW(L"WHGame.dll");
    if (!h) return {};
    wchar_t path[MAX_PATH];
    DWORD n = GetModuleFileNameW(h, path, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    return std::wstring(path, n);
}

// Strategy 1: parse `<game_root>/kcd_launcher.log` for the build description.
//
// The launcher log is written on every game launch with a header like:
//   Build info:
//   * date/time: Apr 16 2026  11:16:26
//   * computer: RACK-BUILD21
//   * configuration: MasterMasterSteamPGO
//   * description: release_1_5_1164953_841
//
// We pattern-match the `description:` line and pull out major/minor/build
// from the `release_<major>_<minor>_<build>_<subbuild>` token.
//
// Returns 0 on any failure (file missing, header missing, parse failure).
// We pass the WHGame.dll path so we can locate the game root by walking up.
uint32_t TryParseLauncherLog(const std::wstring& whgamePath) {
    if (whgamePath.empty()) return 0;
    fs::path root = fs::path(whgamePath).parent_path()    // Win64MasterMasterSteamPGO
                                          .parent_path()  // Bin
                                          .parent_path(); // game root
    fs::path logPath = root / "kcd_launcher.log";
    std::error_code ec;
    if (!fs::exists(logPath, ec)) return 0;

    std::ifstream in(logPath);
    if (!in) return 0;

    // Read just the first ~40 lines. The build header is in the first
    // handful; reading the entire file would be wasteful (the launcher
    // log can grow to MBs).
    std::string line;
    for (int i = 0; i < 64 && std::getline(in, line); ++i) {
        // Look for "* description: release_<maj>_<min>_<build>_..."
        auto descPos = line.find("description:");
        if (descPos == std::string::npos) continue;
        auto relPos = line.find("release_", descPos);
        if (relPos == std::string::npos) continue;

        // Parse three underscore-separated integers after "release_".
        const char* p = line.c_str() + relPos + 8;  // skip "release_"
        unsigned major = 0, minor = 0, build = 0;
        if (std::sscanf(p, "%u_%u_%u", &major, &minor, &build) != 3) {
            continue;
        }
        uint32_t encoded = kcdxMakeGameVersion(major, minor, build);
        log::InfoF("Detected KCD2 runtime version: %u.%u.%u "
                   "(encoded 0x%08X, source: kcd_launcher.log)",
                   major, minor, build, encoded);
        return encoded;
    }
    return 0;
}

// Strategy 2: pull VS_VERSIONINFO out of WHGame.dll. KCD2 doesn't ship one as
// of 1.5.1164953 so this almost always fails — kept as a fallback in case a
// future build adds it.
uint32_t TryVersionInfoResource(const std::wstring& whgamePath) {
    if (whgamePath.empty()) return 0;

    DWORD verHandle = 0;
    DWORD verSize = GetFileVersionInfoSizeW(whgamePath.c_str(), &verHandle);
    if (verSize == 0) return 0;

    std::vector<uint8_t> buf(verSize);
    if (!GetFileVersionInfoW(whgamePath.c_str(), verHandle, verSize, buf.data())) {
        return 0;
    }
    VS_FIXEDFILEINFO* ffi = nullptr;
    UINT ffiLen = 0;
    if (!VerQueryValueW(buf.data(), L"\\",
                        reinterpret_cast<LPVOID*>(&ffi), &ffiLen) ||
        !ffi) {
        return 0;
    }
    uint16_t major = HIWORD(ffi->dwFileVersionMS);
    uint16_t minor = LOWORD(ffi->dwFileVersionMS);
    uint16_t build = HIWORD(ffi->dwFileVersionLS);
    uint32_t encoded = kcdxMakeGameVersion(major, minor, build);
    log::InfoF("Detected KCD2 runtime version: %u.%u.%u "
               "(encoded 0x%08X, source: WHGame.dll VS_VERSIONINFO)",
               major, minor, build, encoded);
    return encoded;
}

// Try every strategy in order. Returns 0 if all fail; the caller (validation
// code) treats 0 as "skip the version-compat check with a warning, don't
// refuse the plugin".
uint32_t DetectRuntimeGameVersion() {
    std::wstring path = LocateWHGamePath();
    if (path.empty()) {
        log::Warn("DetectRuntimeGameVersion: WHGame.dll not loaded");
        return 0;
    }

    if (uint32_t v = TryParseLauncherLog(path); v != 0) return v;
    if (uint32_t v = TryVersionInfoResource(path); v != 0) return v;

    log::WarnF("DetectRuntimeGameVersion: could not determine KCD2 build "
               "(checked kcd_launcher.log and WHGame.dll VS_VERSIONINFO). "
               "compatibleGameVersions checks will be skipped with a warning.");
    return 0;
}

// Validate a parsed PluginManifest against engine + game-version invariants.
// Returns true if the plugin is loadable; logs the specific reason on false.
// matchedGameVersion (out): on success, set to the entry of compatibleGameVersions
// that matched the running game (or 0 if versionIndependent).
bool ValidateManifest(const PluginManifest& m, uint32_t& matchedGameVersion) {
    matchedGameVersion = 0;

    if (m.name.empty()) {
        log::ErrorF("Plugin manifest at %s: name field is empty",
                    m.tomlPath.string().c_str());
        return false;
    }

    if (m.kcdxMinVersion > kEngineVersion) {
        log::ErrorF("Plugin '%s' requires kcdx >= 0x%08X but engine is 0x%08X",
                    m.name.c_str(), m.kcdxMinVersion, kEngineVersion);
        return false;
    }

    // Game-version compatibility.
    bool versionOK = false;
    for (uint32_t gv : m.compatibleGameVersions) {
        if (gv == g_runtimeGameVersion) {
            versionOK = true;
            matchedGameVersion = gv;
            break;
        }
    }

    // Graceful-degradation: if we couldn't detect the runtime game version,
    // don't refuse over our own self-detection failure.
    if (g_runtimeGameVersion == 0 && !m.versionIndependent) {
        log::WarnF("Plugin '%s': engine couldn't determine the running KCD2 "
                   "version; loading anyway. Plugin claims compatibility with:",
                   m.name.c_str());
        if (m.compatibleGameVersions.empty()) {
            log::Warn("    (none — empty compatible_game_versions list)");
        } else {
            for (uint32_t gv : m.compatibleGameVersions) {
                log::WarnF("    0x%08X", gv);
            }
        }
        return true;
    }

    if (!versionOK && !m.versionIndependent) {
        if (m.compatibleGameVersions.empty()) {
            log::ErrorF("Plugin '%s': empty compatible_game_versions list and "
                        "version_independent NOT set — refusing to load. "
                        "Either list your tested game versions or set "
                        "version_independent=true in [plugin].",
                        m.name.c_str());
        } else {
            log::ErrorF("Plugin '%s' not compatible with running game version "
                        "0x%08X. Its compatible_game_versions:",
                        m.name.c_str(), g_runtimeGameVersion);
            for (uint32_t gv : m.compatibleGameVersions) {
                log::ErrorF("    0x%08X", gv);
            }
        }
        return false;
    }

    return true;
}

// One plugin slot during the load wave. Owns a manifest (by value, separate
// from g_manifests so we can shuffle / drop without disturbing the source-of-
// truth collection) and tracks whether it's still valid after each validation
// pass. After topo-sort + DLL resolution, surviving Candidates get promoted
// to LoadedPlugin.
struct Candidate {
    PluginManifest manifest;
    fs::path       dllPath;     // empty for TOML-only plugins
    HMODULE        module = nullptr;   // owned by Candidate until promoted to LoadedPlugin
    uint32_t       matchedGameVersion = 0;  // from ValidateManifest
    bool           valid = false;
};

// Resolve a plugin's DLL entrypoint path. Two paths:
//
//   1. If the manifest declares [entrypoints] dll = "...", that's an explicit
//      relative path. We verify it exists and return it. No fallback —
//      authors who declare it want THIS dll, not "whatever else is in the
//      folder."
//
//   2. Otherwise, auto-discover: scan the plugin folder root for exactly one
//      *.dll. Subfolders are private to the plugin (bundled libraries etc.)
//      and ignored. If multiple DLLs are present, that's ambiguous — fail
//      with a helpful error directing the author to declare [entrypoints]
//      dll. If zero DLLs, return empty (TOML-only plugin).
//
// Returns empty path for "no DLL" (TOML-only plugin) or "ambiguous /
// declared-but-missing" (logged with detail).
fs::path ResolveDllEntrypoint(const PluginManifest& m) {
    if (!m.dllEntrypointRel.empty()) {
        fs::path explicitDll = m.folderPath / m.dllEntrypointRel;
        std::error_code ec;
        if (!fs::exists(explicitDll, ec)) {
            log::ErrorF("Plugin '%s': [entrypoints] dll = \"%s\" but file does "
                        "not exist (resolved to %s)",
                        m.name.c_str(), m.dllEntrypointRel.c_str(),
                        explicitDll.string().c_str());
            return {};
        }
        return explicitDll;
    }

    // Auto-discover: scan folder root only (NOT subfolders).
    std::error_code ec;
    std::vector<fs::path> dlls;
    for (const auto& entry : fs::directory_iterator(m.folderPath, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto p = entry.path();
        auto ext = p.extension().wstring();
        if (_wcsicmp(ext.c_str(), L".dll") == 0) {
            dlls.push_back(p);
        }
    }
    if (dlls.empty()) return {};  // TOML-only plugin
    if (dlls.size() == 1) return dlls[0];

    // Multiple DLLs in root — author must disambiguate.
    std::sort(dlls.begin(), dlls.end());
    log::ErrorF("Plugin '%s': %zu DLLs found in plugin folder root; "
                "auto-discovery is ambiguous. Declare [entrypoints] dll = "
                "\"primary.dll\" in kcdx.toml to specify the entrypoint, "
                "or move bundled libraries to a subfolder. Found:",
                m.name.c_str(), dlls.size());
    for (const auto& d : dlls) {
        log::ErrorF("    %s", d.filename().string().c_str());
    }
    return {};
}

// Topologically sort the candidate list by dependencies. Stable: preserves
// the input order among nodes with no dependency relation. Logs cycles and
// missing required deps; returns true if every required edge resolved.
//
// Uses Kahn's algorithm. Each candidate's manifest.dependencies array names
// other plugins that must load first.
bool TopoSort(std::vector<Candidate>& cands) {
    // Index candidates by name.
    std::unordered_map<std::string, size_t> byName;
    byName.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) {
        byName[cands[i].manifest.name] = i;
    }

    // Adjacency: edges[i] = indices of candidates that must load AFTER i.
    std::vector<std::vector<size_t>> edges(cands.size());
    std::vector<size_t> indeg(cands.size(), 0);

    for (size_t i = 0; i < cands.size(); ++i) {
        const auto& m = cands[i].manifest;
        for (const auto& dep : m.dependencies) {
            auto it = byName.find(dep.name);
            if (it == byName.end()) {
                if (dep.optional) {
                    log::InfoF("Plugin '%s': optional dependency '%s' not present",
                               m.name.c_str(), dep.name.c_str());
                } else {
                    log::ErrorF("Plugin '%s' requires '%s' but no such plugin is "
                                "loaded — skipping '%s'",
                                m.name.c_str(), dep.name.c_str(), m.name.c_str());
                    cands[i].valid = false;
                }
                continue;
            }
            const auto& depM = cands[it->second].manifest;
            if (depM.version < dep.minVersion) {
                if (dep.optional) {
                    log::InfoF("Plugin '%s': optional dependency '%s' is version "
                               "0x%08X but min_version 0x%08X required",
                               m.name.c_str(), dep.name.c_str(),
                               depM.version, dep.minVersion);
                } else {
                    log::ErrorF("Plugin '%s' requires '%s' >= 0x%08X but loaded "
                                "version is 0x%08X — skipping '%s'",
                                m.name.c_str(), dep.name.c_str(),
                                dep.minVersion, depM.version, m.name.c_str());
                    cands[i].valid = false;
                }
                continue;
            }
            edges[it->second].push_back(i);  // dep i' → cand i
            ++indeg[i];
        }
    }

    // Kahn's algorithm. Stable queue (vector scan) preserves discovery order
    // among equal-indegree nodes.
    std::vector<Candidate> out;
    out.reserve(cands.size());
    std::vector<bool> emitted(cands.size(), false);

    while (out.size() < cands.size()) {
        bool progressed = false;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (emitted[i]) continue;
            if (indeg[i] != 0) continue;
            if (!cands[i].valid) {
                emitted[i] = true;  // skip invalid; don't count in output
                continue;
            }
            out.push_back(std::move(cands[i]));
            emitted[i] = true;
            for (size_t j : edges[i]) --indeg[j];
            progressed = true;
        }
        if (!progressed) break;
    }

    // Any candidate not emitted is part of a cycle (or depends on one).
    for (size_t i = 0; i < cands.size(); ++i) {
        if (emitted[i]) continue;
        if (!cands[i].valid) continue;  // already-failed; don't count as cycle member
        log::ErrorF("Plugin '%s' is part of a dependency cycle — skipping",
                    cands[i].manifest.name.c_str());
    }

    cands = std::move(out);
    return true;  // cycles don't fail the whole load; we skip cycle members
}

}  // namespace

// -----------------------------------------------------------------------------
// kcdxInterface stub — populated below, used as the engine→plugin contract
// -----------------------------------------------------------------------------

// Implementations live in interfaces.cpp. Declared here as forward refs so
// DiscoverAndLoad can grab the address.
extern const kcdxInterface* GetEngineInterfaceImpl();

const kcdxInterface* GetEngineInterface() {
    return GetEngineInterfaceImpl();
}

// -----------------------------------------------------------------------------
// DiscoverAndLoad — the orchestration entry point called from dllmain
// -----------------------------------------------------------------------------

void DiscoverAndLoad(const std::wstring& pluginsDir) {
    g_runtimeGameVersion = DetectRuntimeGameVersion();

    fs::path root(pluginsDir);
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        log::WarnF("Plugin DLL loader: plugins dir not found at %s",
                   WideToUtf8(pluginsDir).c_str());
        return;
    }

    // Phase 1 — manifests already populated by config::LoadAllConfigs from
    // each plugin's kcdx.toml [plugin] table. Build a Candidate list from
    // them, applying validation (game-version compat, kcdx_min_version).
    std::vector<Candidate> cands;
    cands.reserve(g_manifests.size());
    for (const auto& m : g_manifests) {
        Candidate c;
        c.manifest = m;
        c.valid    = ValidateManifest(m, c.matchedGameVersion);
        cands.push_back(std::move(c));
    }

    log::InfoF("Plugin DLL loader: %zu manifest(s) discovered from kcdx.toml files",
               g_manifests.size());
    if (cands.empty()) {
        // No manifests at all — but config-only kcdx.toml files (no [plugin]
        // section) may still have applied patches. That's fine; nothing to
        // do here. Fire the lifecycle messages so any engine subsystem that
        // gates on PostLoad still gets the signal.
        log::Info("Firing kcdxMessage_PostLoad...");
        messaging::FireEngineMessage(kcdxMessage_PostLoad);
        log::Info("Firing kcdxMessage_PostPostLoad...");
        messaging::FireEngineMessage(kcdxMessage_PostPostLoad);
        return;
    }

    // Phase 2 — name uniqueness. Two plugins with the same stable name is
    // a load-time error; both abort.
    {
        std::unordered_map<std::string, size_t> firstSeen;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (!cands[i].valid) continue;
            const std::string& nm = cands[i].manifest.name;
            auto [it, inserted] = firstSeen.try_emplace(nm, i);
            if (!inserted) {
                log::ErrorF("Two plugins both declare name '%s' (%s and %s) — "
                            "aborting both.",
                            nm.c_str(),
                            cands[it->second].manifest.tomlPath.string().c_str(),
                            cands[i].manifest.tomlPath.string().c_str());
                cands[it->second].valid = false;
                cands[i].valid          = false;
            }
        }
    }

    // Phase 3 — topo-sort by dependencies. Drops cycle members and any
    // plugin missing a required dep. Surviving cands are in load order.
    TopoSort(cands);

    if (cands.empty()) {
        log::Info("Plugin DLL loader: no plugins survived validation");
        return;
    }

    // Phase 4 — resolve DLL entrypoints + LoadLibraryW. For TOML-only
    // plugins (no DLL), still create a LoadedPlugin record so they appear
    // in g_plugins with a handle (for GetPluginHandle / GetPluginInfo
    // lookups from other plugins). They just have no module + no entry fns.
    g_plugins.reserve(cands.size());
    for (Candidate& c : cands) {
        LoadedPlugin lp;
        lp.manifest   = c.manifest;
        lp.folderName = c.manifest.folderPath.filename().string();
        lp.folderPath = c.manifest.folderPath.wstring();
        lp.handle     = static_cast<kcdxPluginHandle>(g_plugins.size());

        fs::path dllPath = ResolveDllEntrypoint(c.manifest);
        if (!dllPath.empty()) {
            std::wstring wpath = dllPath.wstring();
            HMODULE mod = LoadLibraryW(wpath.c_str());
            if (!mod) {
                log::ErrorF("Plugin '%s': LoadLibrary failed for %s (err=%lu) — "
                            "registering as metadata-only plugin",
                            c.manifest.name.c_str(),
                            dllPath.string().c_str(), GetLastError());
            } else {
                lp.module     = mod;
                lp.filePath   = dllPath.string();
                lp.preloadFn  = reinterpret_cast<kcdxPlugin_Preload_t>(
                                    GetProcAddress(mod, "kcdxPlugin_Preload"));
                lp.loadFn     = reinterpret_cast<kcdxPlugin_Load_t>(
                                    GetProcAddress(mod, "kcdxPlugin_Load"));
                if (!lp.loadFn && !lp.preloadFn) {
                    log::WarnF("Plugin '%s' DLL has neither kcdxPlugin_Load nor "
                               "kcdxPlugin_Preload exports — DLL is dead weight. "
                               "Did you forget extern \"C\" __declspec(dllexport)?",
                               c.manifest.name.c_str());
                }
            }
        }
        // else: TOML-only plugin (no DLL). Valid; just no entry points.

        log::InfoF("Loaded plugin '%s' v0x%08X [handle=%u]%s%s",
                   c.manifest.name.c_str(), c.manifest.version, lp.handle,
                   lp.filePath.empty() ? " (TOML-only)" : " from ",
                   lp.filePath.empty() ? "" : lp.filePath.c_str());
        g_plugins.push_back(std::move(lp));
    }

    if (g_plugins.empty()) return;

    // Phase 5 — Preload wave.
    const kcdxInterface* api = GetEngineInterface();
    for (auto& p : g_plugins) {
        if (!p.preloadFn) continue;
        bool ok = false;
        try {
            ok = p.preloadFn(api);
        } catch (...) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Preload threw an exception",
                        p.manifest.name.c_str());
            ok = false;
        }
        if (!ok) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Preload returned false",
                        p.manifest.name.c_str());
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_Preload OK", p.manifest.name.c_str());
        }
    }

    // Phase 6 — Load wave.
    for (auto& p : g_plugins) {
        if (!p.loadFn) {
            // No Load function. That's OK for TOML-only plugins (purely
            // declarative — patches/hooks already applied via config.cpp's
            // entry processors). Note as "TOML-only" rather than warning.
            p.loaded = true;
            continue;
        }
        bool ok = false;
        try {
            ok = p.loadFn(api);
        } catch (...) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Load threw an exception",
                        p.manifest.name.c_str());
            ok = false;
        }
        if (!ok) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Load returned false",
                        p.manifest.name.c_str());
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_Load OK", p.manifest.name.c_str());
        }
        p.loaded = ok;
    }

    size_t okCount = 0;
    for (const auto& p : g_plugins) if (p.loaded) ++okCount;
    log::InfoF("Plugin DLL loader: %zu of %zu plugin(s) loaded successfully",
               okCount, g_plugins.size());

    // Phase 7 — Lifecycle: fire kcdxMessage_PostLoad, then kcdxMessage_PostPostLoad.
    // Plugin B's PostLoad handler can confirm plugin A is loaded (its Load
    // returned). Plugin B's PostPostLoad handler can confirm plugin A's
    // PostLoad handler returned — the wave is settled.
    log::Info("Firing kcdxMessage_PostLoad...");
    messaging::FireEngineMessage(kcdxMessage_PostLoad);

    log::Info("Firing kcdxMessage_PostPostLoad...");
    messaging::FireEngineMessage(kcdxMessage_PostPostLoad);
}

const LoadedPlugin* FindByName(const char* name) {
    if (!name) return nullptr;
    for (const auto& p : g_plugins) {
        if (p.manifest.name == name) return &p;
    }
    return nullptr;
}

kcdxPluginHandle HandleOf(const char* name) {
    if (const auto* p = FindByName(name)) return p->handle;
    return kcdxInvalidPluginHandle;
}

}  // namespace kcdx::plugins
