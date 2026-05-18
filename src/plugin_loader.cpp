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
#include "pe_helpers.h"

namespace fs = std::filesystem;

namespace kcdx::plugins {

std::vector<LoadedPlugin> g_plugins;
uint32_t                  g_runtimeGameVersion = 0;

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

// Read the kcdxPluginVersionData from a DLL using LoadLibraryEx with
// LOAD_LIBRARY_AS_IMAGE_RESOURCE — maps the PE without executing any code or
// running DllMain. Safe to call on untrusted plugins; we validate before
// any real LoadLibrary.
//
// Returns a copy of the version data on success. The original lives inside
// the resource-mapped image which we unmap before returning, so we own this
// copy by value.
bool PeekVersionData(const fs::path& dllPath, kcdxPluginVersionData& out) {
    std::wstring wpath = dllPath.wstring();
    HMODULE peek = LoadLibraryExW(wpath.c_str(), nullptr,
                                  LOAD_LIBRARY_AS_IMAGE_RESOURCE |
                                  LOAD_LIBRARY_AS_DATAFILE);
    if (!peek) {
        log::WarnF("Plugin '%s': LoadLibraryEx(peek) failed (err=%lu)",
                   dllPath.string().c_str(), GetLastError());
        return false;
    }
    // LOAD_LIBRARY_AS_IMAGE_RESOURCE returns an HMODULE with the low bit set
    // (per MSDN). Strip it to get the base.
    uintptr_t base = reinterpret_cast<uintptr_t>(peek) & ~uintptr_t(1);

    // GetProcAddress doesn't work on a resource-mapped module. We walk the
    // export table by hand and resolve the name.
    auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        FreeLibrary(peek);
        return false;
    }
    auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS*>(base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        FreeLibrary(peek);
        return false;
    }
    auto& expDir = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_EXPORT];
    if (expDir.VirtualAddress == 0 || expDir.Size == 0) {
        log::WarnF("Plugin '%s': no export table", dllPath.string().c_str());
        FreeLibrary(peek);
        return false;
    }
    auto* exp = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base + expDir.VirtualAddress);
    auto* names = reinterpret_cast<const uint32_t*>(base + exp->AddressOfNames);
    auto* funcs = reinterpret_cast<const uint32_t*>(base + exp->AddressOfFunctions);
    auto* ords  = reinterpret_cast<const uint16_t*>(base + exp->AddressOfNameOrdinals);

    const void* dataPtr = nullptr;
    for (DWORD i = 0; i < exp->NumberOfNames; ++i) {
        const char* name = reinterpret_cast<const char*>(base + names[i]);
        if (std::strcmp(name, "kcdxPluginVersionData") == 0) {
            uint16_t ord = ords[i];
            if (ord >= exp->NumberOfFunctions) break;
            dataPtr = reinterpret_cast<const void*>(base + funcs[ord]);
            break;
        }
    }
    if (!dataPtr) {
        log::WarnF("Plugin '%s': missing kcdxPluginVersionData export",
                   dllPath.string().c_str());
        FreeLibrary(peek);
        return false;
    }

    // Copy by value. The resource-mapped image is going away.
    std::memcpy(&out, dataPtr, sizeof(out));
    FreeLibrary(peek);
    return true;
}

// Validate a kcdxPluginVersionData against the engine's runtime invariants.
// Returns true if the plugin is loadable. Logs the specific reason on false.
bool ValidateVersionData(const kcdxPluginVersionData& v,
                         const std::string& filePathForLog) {
    if (v.dataVersion != kcdxPluginVersionData_CurrentVersion) {
        log::ErrorF("Plugin '%s': dataVersion %u not supported (engine expects %u)",
                    filePathForLog.c_str(), v.dataVersion,
                    kcdxPluginVersionData_CurrentVersion);
        return false;
    }
    if (v.name[0] == '\0') {
        log::ErrorF("Plugin '%s': name field is empty", filePathForLog.c_str());
        return false;
    }
    // Ensure name is null-terminated within the field.
    bool nameTerminated = false;
    for (size_t i = 0; i < sizeof(v.name); ++i) {
        if (v.name[i] == '\0') { nameTerminated = true; break; }
    }
    if (!nameTerminated) {
        log::ErrorF("Plugin '%s': name field not null-terminated", filePathForLog.c_str());
        return false;
    }

    if (v.kcdxVersionRequired > kEngineVersion) {
        log::ErrorF("Plugin '%s' requires kcdx >= 0x%08X but engine is 0x%08X",
                    v.name, v.kcdxVersionRequired, kEngineVersion);
        return false;
    }

    // Compatibility: either compatibleGameVersions includes the running build,
    // or the plugin opted into AddressLibrary version-independence.
    bool versionOK = false;
    bool hasAnyVersion = false;
    for (uint32_t gv : v.compatibleGameVersions) {
        if (gv == 0) break;
        hasAnyVersion = true;
        if (gv == g_runtimeGameVersion) { versionOK = true; break; }
    }
    bool addrLibOptIn = (v.versionIndependence & kcdxVersionIndependent_AddressLibrary) != 0;

    // Graceful-degradation case: if we couldn't detect the runtime game version
    // (g_runtimeGameVersion == 0), the version-compat check is unreliable. Don't
    // refuse plugins over our own self-detection failure — log a warning and
    // let them load. The plugin author's compatibleGameVersions array still
    // shows up in the log so users can see what was claimed.
    if (g_runtimeGameVersion == 0 && !addrLibOptIn) {
        log::WarnF("Plugin '%s': engine couldn't determine the running KCD2 version, "
                   "so compatibleGameVersions cannot be verified. Loading anyway. "
                   "Plugin claims compatibility with:",
                   v.name);
        if (!hasAnyVersion) {
            log::Warn("    (none — empty compatibleGameVersions array)");
        } else {
            for (uint32_t gv : v.compatibleGameVersions) {
                if (gv == 0) break;
                log::WarnF("    0x%08X", gv);
            }
        }
        return true;
    }

    if (!versionOK && !addrLibOptIn) {
        if (!hasAnyVersion) {
            log::ErrorF("Plugin '%s': empty compatibleGameVersions array and "
                        "AddressLibrary flag NOT set — refusing to load. "
                        "Either list your tested game versions or opt into the "
                        "AddressLibrary version-independence flag.",
                        v.name);
        } else {
            log::ErrorF("Plugin '%s' not compatible with running game version 0x%08X. "
                        "Its compatibleGameVersions:", v.name, g_runtimeGameVersion);
            for (uint32_t gv : v.compatibleGameVersions) {
                if (gv == 0) break;
                log::ErrorF("    0x%08X", gv);
            }
        }
        return false;
    }

    return true;
}

// One DLL on disk waiting to be validated and loaded. Lifecycle:
//   1. Discovery: PE-peek the version data into `vdataCopy`. Set `dllPath`.
//   2. Validation: ValidateVersionData → if ok, queue for real load.
//   3. Real load: LoadLibraryW + GetProcAddress for entry points → LoadedPlugin.
struct Candidate {
    fs::path                folderPath;
    fs::path                dllPath;
    kcdxPluginVersionData   vdataCopy;
    bool                    valid = false;
};

// Scan one folder for a DLL. Convention: a plugin folder contains exactly one
// DLL (any name); also tolerated is a "loose" DLL sitting directly in plugins/
// with no folder. Returns the DLL path or empty if none found.
fs::path FindDllInFolder(const fs::path& folder) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(folder, ec)) {
        if (!entry.is_regular_file(ec)) continue;
        auto p = entry.path();
        if (p.extension() == L".dll" || p.extension() == L".DLL") {
            return p;
        }
    }
    return {};
}

// Topologically sort the candidate list by dependencies. Stable: preserves
// the input order among nodes with no dependency relation. Logs cycles and
// missing required deps; returns true if every required edge resolved.
bool TopoSort(std::vector<Candidate>& cands) {
    // Index candidates by name.
    std::unordered_map<std::string, size_t> byName;
    byName.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) {
        byName[cands[i].vdataCopy.name] = i;
    }

    // Adjacency: edges[i] = indices of candidates that must load before i.
    std::vector<std::vector<size_t>> edges(cands.size());
    std::vector<size_t> indeg(cands.size(), 0);

    for (size_t i = 0; i < cands.size(); ++i) {
        const auto& v = cands[i].vdataCopy;
        const kcdxPluginDependency* d = v.dependencies;
        if (!d) continue;
        for (; d->name != nullptr; ++d) {
            auto it = byName.find(d->name);
            bool optional = (d->flags & kcdxDependencyFlag_Optional) != 0;
            if (it == byName.end()) {
                if (optional) {
                    log::InfoF("Plugin '%s': optional dependency '%s' not present",
                               v.name, d->name);
                } else {
                    log::ErrorF("Plugin '%s' requires '%s' but no such plugin is loaded — "
                                "skipping '%s'",
                                v.name, d->name, v.name);
                    cands[i].valid = false;
                }
                continue;
            }
            const auto& depV = cands[it->second].vdataCopy;
            if (depV.pluginVersion < d->minVersion) {
                if (optional) {
                    log::InfoF("Plugin '%s': optional dependency '%s' is version "
                               "0x%08X but minVersion 0x%08X required",
                               v.name, d->name, depV.pluginVersion, d->minVersion);
                } else {
                    log::ErrorF("Plugin '%s' requires '%s' >= 0x%08X but loaded version "
                                "is 0x%08X — skipping '%s'",
                                v.name, d->name, d->minVersion, depV.pluginVersion, v.name);
                    cands[i].valid = false;
                }
                continue;
            }
            edges[it->second].push_back(i);  // dep i' → cand i
            ++indeg[i];
        }
    }

    // Kahn's algorithm. Use a stable queue (vector) so equal-indegree nodes
    // preserve discovery order.
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
        if (!cands[i].valid) continue;  // already-failed plugins don't count as cycle members
        log::ErrorF("Plugin '%s' is part of a dependency cycle — skipping", cands[i].vdataCopy.name);
    }

    cands = std::move(out);
    return true;  // we don't fail the whole load over cycles; we skip cycle members
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

    // Phase 1 — discover all candidate DLLs. Scan subdirectories of plugins/.
    // (Loose DLLs sitting directly in plugins/ are also picked up, matching
    // the SKSE convention.)
    std::vector<Candidate> cands;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        fs::path dllPath;
        fs::path folderPath;
        if (entry.is_directory(ec)) {
            folderPath = entry.path();
            dllPath = FindDllInFolder(folderPath);
            if (dllPath.empty()) continue;
        } else if (entry.is_regular_file(ec)) {
            auto p = entry.path();
            // Skip kcdx.asi itself.
            std::wstring filename = p.filename().wstring();
            if (_wcsicmp(filename.c_str(), L"kcdx.asi") == 0) continue;
            if (p.extension() == L".dll" || p.extension() == L".DLL") {
                dllPath = p;
                folderPath = p.parent_path();
            } else {
                continue;
            }
        } else {
            continue;
        }

        Candidate c;
        c.folderPath = folderPath;
        c.dllPath = dllPath;
        if (!PeekVersionData(dllPath, c.vdataCopy)) continue;
        c.valid = ValidateVersionData(c.vdataCopy, dllPath.string());
        cands.push_back(std::move(c));
    }

    log::InfoF("Plugin DLL loader: discovered %zu candidate(s)", cands.size());
    if (cands.empty()) return;

    // Phase 2 — check name uniqueness. Two plugins with the same stable
    // name is a load-time error; both abort.
    {
        std::unordered_map<std::string, size_t> firstSeen;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (!cands[i].valid) continue;
            std::string nm = cands[i].vdataCopy.name;
            auto [it, inserted] = firstSeen.try_emplace(nm, i);
            if (!inserted) {
                log::ErrorF("Two plugins both export name '%s' (%s and %s) — "
                            "aborting both.",
                            nm.c_str(),
                            cands[it->second].dllPath.string().c_str(),
                            cands[i].dllPath.string().c_str());
                cands[it->second].valid = false;
                cands[i].valid = false;
            }
        }
    }

    // Phase 3 — topo-sort by dependencies. Drops cycle members and any
    // plugin missing a required dep. The surviving cands are in load order.
    TopoSort(cands);

    if (cands.empty()) {
        log::Info("Plugin DLL loader: no plugins survived validation");
        return;
    }

    // Phase 4 — real load. Allocate handles in topo-sort order.
    g_plugins.reserve(cands.size());
    for (size_t i = 0; i < cands.size(); ++i) {
        Candidate& c = cands[i];

        std::wstring wpath = c.dllPath.wstring();
        HMODULE mod = LoadLibraryW(wpath.c_str());
        if (!mod) {
            log::ErrorF("Plugin '%s': LoadLibrary failed (err=%lu) — skipping",
                        c.vdataCopy.name, GetLastError());
            continue;
        }

        LoadedPlugin lp;
        lp.filePath = c.dllPath.string();
        lp.folderName = c.folderPath.filename().string();
        lp.module = mod;
        // The version data lives at a static address in the real DLL; we look it
        // up again on the LoadLibrary'd module so the pointer is stable for the
        // lifetime of the process.
        lp.versionData = reinterpret_cast<const kcdxPluginVersionData*>(
            GetProcAddress(mod, "kcdxPluginVersionData"));
        if (!lp.versionData) {
            log::ErrorF("Plugin '%s': kcdxPluginVersionData symbol resolved during "
                        "discovery but missing after real load — skipping",
                        c.vdataCopy.name);
            FreeLibrary(mod);
            continue;
        }
        lp.preloadFn = reinterpret_cast<kcdxPlugin_Preload_t>(
            GetProcAddress(mod, "kcdxPlugin_Preload"));
        lp.loadFn = reinterpret_cast<kcdxPlugin_Load_t>(
            GetProcAddress(mod, "kcdxPlugin_Load"));

        // Handle assignment. Sequential from 0.
        lp.handle = static_cast<kcdxPluginHandle>(g_plugins.size());

        log::InfoF("Loaded plugin '%s' (version 0x%08X) from %s [handle=%u]",
                   lp.versionData->name, lp.versionData->pluginVersion,
                   lp.filePath.c_str(), lp.handle);
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
                        p.versionData->name);
            ok = false;
        }
        if (!ok) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Preload returned false",
                        p.versionData->name);
            // We continue running subsequent plugins; a failed Preload is logged
            // but doesn't propagate.
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_Preload OK", p.versionData->name);
        }
    }

    // Phase 6 — Load wave.
    for (auto& p : g_plugins) {
        if (!p.loadFn) {
            // No Load function — that's OK if the plugin is purely declarative
            // (versionData.inlinePatchesToml non-null). Otherwise log a warning.
            if (!p.versionData->inlinePatchesToml && !p.preloadFn) {
                log::WarnF("Plugin '%s' has no kcdxPlugin_Load, no kcdxPlugin_Preload, "
                           "and no inlinePatchesToml — nothing to do.",
                           p.versionData->name);
            }
            p.loaded = true;
            continue;
        }
        bool ok = false;
        try {
            ok = p.loadFn(api);
        } catch (...) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Load threw an exception",
                        p.versionData->name);
            ok = false;
        }
        if (!ok) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Load returned false",
                        p.versionData->name);
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_Load OK", p.versionData->name);
        }
        p.loaded = ok;
    }

    size_t okCount = 0;
    for (const auto& p : g_plugins) if (p.loaded) ++okCount;
    log::InfoF("Plugin DLL loader: %zu of %zu plugin(s) loaded successfully",
               okCount, g_plugins.size());
}

const LoadedPlugin* FindByName(const char* name) {
    if (!name) return nullptr;
    for (const auto& p : g_plugins) {
        if (p.versionData && std::strcmp(p.versionData->name, name) == 0) {
            return &p;
        }
    }
    return nullptr;
}

kcdxPluginHandle HandleOf(const char* name) {
    if (const auto* p = FindByName(name)) return p->handle;
    return kcdxInvalidPluginHandle;
}

}  // namespace kcdx::plugins
