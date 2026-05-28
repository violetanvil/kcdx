#include "plugin_loader.h"

#include <windows.h>
#include <psapi.h>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "crash_guard.h"
#include "load_order.h"
#include "log.h"
#include "messaging.h"
#include "paths.h"
#include "pe_helpers.h"
#include "version_compat.h"  // shared game-version compat decision (pak-mod path shares it)
#include "zone_gate.h"  // RejectReason() — distinguish engine-reject from user-disabled in skip-logs

namespace fs = std::filesystem;

namespace kcdx::plugins {

std::vector<LoadedPlugin>   g_plugins;
std::vector<PluginManifest> g_manifests;
uint32_t                    g_runtimeGameVersion = 0;
std::string                 g_runtimeGameVersionString;

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

}  // namespace

// Try every strategy in order. Returns 0 if all fail; the caller (validation
// code) treats 0 as "skip the version-compat check with a warning, don't
// refuse the plugin". External linkage (declared in plugin_loader.h) so the
// worker thread (dllmain.cpp) can call it at WHGame-mapped time — version
// detection now runs EARLY (ctx B, right after WaitForGameDll), before
// hooks::Install + the full plugin load, not late inside DiscoverAndLoad.
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
               "Game-version compatibility checks will be skipped with a warning.");
    return 0;
}

// Pure string parse of a CryEngine cfg body for `key = value`. No file I/O —
// the caller reads the file; this scans the text. Kept external (declared in
// plugin_loader.h) so the cap-53 self-test can feed it literal cfg text.
std::string ExtractCfgValue(const std::string& cfgText, const char* key) {
    auto trim = [](std::string s) {
        size_t b = s.find_first_not_of(" \t\r\n");
        if (b == std::string::npos) return std::string{};
        size_t e = s.find_last_not_of(" \t\r\n");
        return s.substr(b, e - b + 1);
    };
    auto iequals = [](const std::string& a, const std::string& b) {
        if (a.size() != b.size()) return false;
        for (size_t i = 0; i < a.size(); ++i) {
            if (std::tolower(static_cast<unsigned char>(a[i])) !=
                std::tolower(static_cast<unsigned char>(b[i]))) {
                return false;
            }
        }
        return true;
    };

    const std::string wantKey = key;
    std::istringstream in(cfgText);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = trim(line);
        if (t.empty()) continue;
        if (t.size() >= 2 && t[0] == '-' && t[1] == '-') continue;  // -- comment
        const size_t eq = t.find('=');
        if (eq == std::string::npos) continue;
        const std::string lhs = trim(t.substr(0, eq));
        if (!iequals(lhs, wantKey)) continue;
        std::string rhs = trim(t.substr(eq + 1));
        // Strip a single layer of surrounding double quotes.
        if (rhs.size() >= 2 && rhs.front() == '"' && rhs.back() == '"') {
            rhs = rhs.substr(1, rhs.size() - 2);
        }
        return rhs;
    }
    return {};
}

// Detect the running KCD2 version STRING by pattern-scanning WHGame.dll's
// .rdata for the canonical engine build tag `release_<major>_<minor>_<build>_<patch>`.
// The reference DB (refdb game_versions.tag) stores the build tag as
// `<major>.<minor>.<build>` (e.g. "1.5.1164953"); we parse out that triplet
// and drop the trailing `_<patch>`. The PE-walk pattern (find .rdata bounds
// off the IMAGE_DOS_HEADER → IMAGE_NT_HEADERS64 → section table) mirrors the
// idiom in ctor_probe.cpp's ResolveWhgameBounds.
//
// The byte scan is a hand-rolled loop (not <regex>) — Windows-internal char
// scan is simpler + smaller and the pattern is a strict literal+digit shape.
// Graceful-degrade: "" + WARN on failure (WHGame not mapped, or .rdata yields
// no match) — the unified <supports> gate then treats the runtime version as
// unknown and loads mods/plugins anyway (same shape as the integer path).
std::string DetectRuntimeGameVersionString() {
    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        LOG_WARN_KV("VERSION", "WHGame.dll not loaded — runtime version "
                    "detection deferred",
                    log::KV::BareStr("source",
                        "WHGame.dll .rdata 'release_X_Y_Z_P' tag"));
        return {};
    }

    const auto base = reinterpret_cast<uintptr_t>(whgame);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(whgame);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE) {
        LOG_WARN_KV("VERSION", "WHGame.dll .rdata did not contain a "
                    "release_X_Y_Z_P pattern — version-string gate will treat "
                    "the runtime version as unknown and load mods/plugins "
                    "anyway",
                    log::KV::BareStr("reason", "bad_dos_signature"));
        return {};
    }
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS64*>(
        base + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE) {
        LOG_WARN_KV("VERSION", "WHGame.dll .rdata did not contain a "
                    "release_X_Y_Z_P pattern — version-string gate will treat "
                    "the runtime version as unknown and load mods/plugins "
                    "anyway",
                    log::KV::BareStr("reason", "bad_nt_signature"));
        return {};
    }

    const uint8_t* rdataLo = nullptr;
    const uint8_t* rdataHi = nullptr;
    {
        const auto* section = IMAGE_FIRST_SECTION(nt);
        for (WORD i = 0; i < nt->FileHeader.NumberOfSections; ++i, ++section) {
            if (std::memcmp(section->Name, ".rdata", 6) == 0) {
                rdataLo = reinterpret_cast<const uint8_t*>(
                    base + section->VirtualAddress);
                rdataHi = rdataLo + section->Misc.VirtualSize;
                break;
            }
        }
    }
    if (!rdataLo || !rdataHi || rdataHi <= rdataLo) {
        LOG_WARN_KV("VERSION", "WHGame.dll .rdata did not contain a "
                    "release_X_Y_Z_P pattern — version-string gate will treat "
                    "the runtime version as unknown and load mods/plugins "
                    "anyway",
                    log::KV::BareStr("reason", "rdata_section_not_found"));
        return {};
    }

    // Hand-rolled byte scan for `release_<digits>_<digits>_<digits>_<digits>`.
    // Accept up to 3 digits / 3 digits / 8 digits / 4 digits per the spec; the
    // observed live tag is `release_1_5_1164953_841` (1/1/7/3 digits). Take
    // the FIRST match. The two known live occurrences both encode the same
    // build, so either match yields the same result.
    static constexpr char kPrefix[] = "release_";
    constexpr size_t kPrefixLen = sizeof(kPrefix) - 1;  // 8

    auto isDigit = [](uint8_t c) { return c >= '0' && c <= '9'; };
    auto consumeDigits = [&](const uint8_t* p, const uint8_t* end,
                             size_t maxDigits, unsigned& out, size_t& consumed)
        -> bool {
        consumed = 0;
        out = 0;
        while (consumed < maxDigits && p + consumed < end
               && isDigit(p[consumed])) {
            out = out * 10 + static_cast<unsigned>(p[consumed] - '0');
            ++consumed;
        }
        return consumed > 0;
    };

    for (const uint8_t* p = rdataLo;
         p + kPrefixLen + 4 /*minimum body*/ <= rdataHi; ++p) {
        if (std::memcmp(p, kPrefix, kPrefixLen) != 0) continue;
        const uint8_t* q = p + kPrefixLen;
        unsigned major = 0, minor = 0, build = 0, patch = 0;
        size_t n = 0;
        if (!consumeDigits(q, rdataHi, 3, major, n)) continue;
        q += n;
        if (q >= rdataHi || *q != '_') continue;
        ++q;
        if (!consumeDigits(q, rdataHi, 3, minor, n)) continue;
        q += n;
        if (q >= rdataHi || *q != '_') continue;
        ++q;
        if (!consumeDigits(q, rdataHi, 8, build, n)) continue;
        q += n;
        if (q >= rdataHi || *q != '_') continue;
        ++q;
        if (!consumeDigits(q, rdataHi, 4, patch, n)) continue;

        char buf[64];
        std::snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, build);
        const std::string value(buf);
        char fullBuf[96];
        std::snprintf(fullBuf, sizeof(fullBuf), "release_%u_%u_%u_%u",
                      major, minor, build, patch);

        const unsigned long long rva =
            static_cast<unsigned long long>(p - reinterpret_cast<const uint8_t*>(base));
        LOG_INFO_KV("VERSION",
                    "detected runtime version string from WHGame.dll .rdata",
                    log::KV("value", value),
                    log::KV("full_tag", std::string(fullBuf)),
                    log::KV("rva", rva),
                    log::KV::BareStr("source",
                        "WHGame.dll .rdata 'release_X_Y_Z_P' tag"));
        return value;
    }

    LOG_WARN_KV("VERSION", "WHGame.dll .rdata did not contain a "
                "release_X_Y_Z_P pattern — version-string gate will treat the "
                "runtime version as unknown and load mods/plugins anyway",
                log::KV::BareStr("reason", "pattern_not_found"));
    return {};
}

namespace {

// Validate a parsed PluginManifest against engine + game-version invariants.
// Returns true if the plugin is loadable; logs the specific reason on false.
bool ValidateManifest(const PluginManifest& m) {
    if (m.name.empty()) {
        LOG_ERROR("MANIFEST", "reject: manifest at %s has empty name field",
                  m.tomlPath.string().c_str());
        return false;
    }

    if (m.kcdxMinVersion > kEngineVersion) {
        LOG_ERROR("MANIFEST",
            "reject '%s': requires kcdx >= 0x%08X but engine is 0x%08X",
            m.name.c_str(), m.kcdxMinVersion, kEngineVersion);
        return false;
    }

    // Game-version compatibility. The CORE decision is the shared UNIFIED
    // <supports> helper (version_compat::DecideGameVersionCompatString) so the
    // pak-mod path (mod_absorb) runs the IDENTICAL policy — see
    // docs/mod-loader-absorb.md "ONE kcdx-owned version policy for BOTH plugins
    // and pak mods" / "Version gate UNIFICATION". The plugin's `supports`
    // patterns are string-compared (trailing-'*' prefix wildcard) against
    // g_runtimeGameVersionString (wh_sys_version, e.g. "1.5.5"). This function
    // keeps its own logging (the pak-mod path's log lines differ).
    //
    // Emit the plugin's supports patterns (one line each, indented) at the
    // given level — shared by the Unknown (WARN) and Incompatible (ERROR)
    // branches. Empty list prints an explicit "(none)" so the log is
    // self-explanatory.
    auto logSupports = [&](log::Level lvl) {
        if (m.supports.empty()) {
            log::EmitEngine(lvl, "MANIFEST", "    (none — empty supports list)");
        } else {
            for (const std::string& pat : m.supports) {
                char buf[KCDX_LOG_FORMAT_BUF_SIZE];
                log::detail::FormatTo(buf, sizeof(buf), "    %s", pat.c_str());
                log::EmitEngine(lvl, "MANIFEST", buf);
            }
        }
    };
    switch (version_compat::DecideGameVersionCompatString(
                m.supports, g_runtimeGameVersionString)) {
    case version_compat::CompatResult::UnknownGameVersion:
        // Graceful-degradation: couldn't determine the running game version;
        // don't refuse over our own self-detection failure.
        LOG_WARN("MANIFEST",
            "Plugin '%s': engine couldn't determine the running KCD2 "
            "version; loading anyway. Plugin's supports patterns:",
            m.name.c_str());
        logSupports(log::Level::Warn);
        return true;

    case version_compat::CompatResult::Incompatible:
        // Non-empty supports list, none of whose patterns match the running
        // version (an empty list yields Compatible, never Incompatible — see
        // DecideGameVersionCompatString rule 1).
        LOG_ERROR("MANIFEST",
            "reject '%s': running game version '%s' is not matched by any "
            "of its supports patterns:",
            m.name.c_str(), g_runtimeGameVersionString.c_str());
        logSupports(log::Level::Error);
        return false;

    case version_compat::CompatResult::Compatible:
        return true;
    }

    return true;  // unreachable — switch is exhaustive over CompatResult.
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
    // g_runtimeGameVersion is ALREADY set by this point: the worker thread
    // (dllmain.cpp) ran DetectRuntimeGameVersion right after WaitForGameDll
    // (ctx B, GameDllMapped → VersionDetected), before this call. The
    // per-plugin version-compat gate below (ValidateManifest) READS it; it no
    // longer detects the version itself. A value of 0 means detection found no
    // source (logged WARN there) → the compat check is skipped, exactly as
    // before — the only change is WHEN the value was computed (early), not the
    // gate's behavior.
    fs::path root(pluginsDir);
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        log::WarnF("Plugin DLL loader: plugins dir not found at %s",
                   WideToUtf8(pluginsDir).c_str());
        return;
    }

    // Step 1 — manifests already populated by config::LoadAllConfigs from
    // each plugin's kcdx.toml [plugin] table. Build a Candidate list from
    // them, applying validation (game-version compat, kcdx_min_version).
    std::vector<Candidate> cands;
    cands.reserve(g_manifests.size());
    for (const auto& m : g_manifests) {
        Candidate c;
        c.manifest = m;
        c.valid    = ValidateManifest(m);
        cands.push_back(std::move(c));
    }

    // MANIFEST funnel summary: how many considered vs how many survived
    // ValidateManifest. Pair this with WARN/ERROR lines above to track
    // why any specific plugin was rejected.
    size_t validCount = 0;
    for (const auto& c : cands) if (c.valid) ++validCount;
    LOG_INFO("MANIFEST",
        "Plugin DLL loader: %zu manifest(s) considered, %zu valid, "
        "%zu rejected (see WARN/ERROR above)",
        g_manifests.size(), validCount, g_manifests.size() - validCount);
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

    // Step 2 — name uniqueness. Two plugins with the same stable name is
    // a load-time error; both abort.
    {
        std::unordered_map<std::string, size_t> firstSeen;
        for (size_t i = 0; i < cands.size(); ++i) {
            if (!cands[i].valid) continue;
            const std::string& nm = cands[i].manifest.name;
            auto [it, inserted] = firstSeen.try_emplace(nm, i);
            if (!inserted) {
                LOG_ERROR("MANIFEST",
                    "reject: two plugins both declare name '%s' (%s and %s) — "
                    "aborting both",
                    nm.c_str(),
                    cands[it->second].manifest.tomlPath.string().c_str(),
                    cands[i].manifest.tomlPath.string().c_str());
                cands[it->second].valid = false;
                cands[i].valid          = false;
            }
        }
    }

    // Step 3 — topo-sort by dependencies. Drops cycle members and any
    // plugin missing a required dep. Surviving cands are in load order.
    TopoSort(cands);

    if (cands.empty()) {
        log::Info("Plugin DLL loader: no plugins survived validation");
        return;
    }

    // Step 4 — resolve DLL entrypoints + LoadLibraryW. For TOML-only
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
                lp.preloadFn      = reinterpret_cast<kcdxPlugin_Preload_t>(
                                    GetProcAddress(mod, "kcdxPlugin_Preload"));
                lp.loadFn         = reinterpret_cast<kcdxPlugin_Load_t>(
                                    GetProcAddress(mod, "kcdxPlugin_Load"));
                // Optional after-game export — the C++ mirror of lua_after.
                // Resolved off the SAME module as Preload/Load (one DLL,
                // multiple optional exports). Null when absent -> skipped by
                // RunPostGameLoad, like a plugin with no Preload.
                lp.postGameLoadFn = reinterpret_cast<kcdxPlugin_PostGameLoad_t>(
                                    GetProcAddress(mod, "kcdxPlugin_PostGameLoad"));
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
        kcdxPluginHandle h = lp.handle;
        bool hasDll = (lp.module != nullptr);
        g_plugins.push_back(std::move(lp));

        // Eagerly create this plugin's logs/<folder>_<ts>.log so every
        // DLL-bearing plugin has a logs/ folder, even if the plugin
        // itself never calls api->Log. TOML-only plugins are skipped
        // — they have no code path that calls Log and creating an
        // empty file for them would just be noise.
        if (hasDll) {
            log::OpenPluginStream(h);
        }
    }

    if (g_plugins.empty()) return;

    // Step 5 — Preload wave.
    const kcdxInterface* api = GetEngineInterface();
    for (auto& p : g_plugins) {
        if (!p.preloadFn) continue;
        // load_order.toml disabled gate. A disabled plugin still gets
        // its DLL mapped (LoadLibraryW already ran above to read the
        // manifest version + export discovery), but kcdxPlugin_Preload
        // is NOT called — so the plugin's static initializers ran but
        // its declarative entry-point code does not. Skip-log
        // distinguishes engine-reject (zone_gate) from user-disabled
        // cause; PLUGIN_REJECTED was already emitted loudly.
        if (!load_order::IsPluginEnabled(p.manifest.name)) {
            // zone_gate keys g_rejected on the 2-dot <author>.<plugin>
            // form (matches kcdx.plugin.is_rejected lookup shape). Pass
            // the composed key here.
            const std::string& rejectReason =
                zone_gate::RejectReason(
                    p.manifest.author + "." + p.manifest.name);
            if (!rejectReason.empty()) {
                log::InfoF("Plugin '%s' kcdxPlugin_Preload skipped "
                           "(rejected by zone_gate: %s)",
                           p.manifest.name.c_str(), rejectReason.c_str());
            } else {
                log::InfoF("Plugin '%s' kcdxPlugin_Preload skipped "
                           "(plugin disabled via load_order.toml)",
                           p.manifest.name.c_str());
            }
            continue;
        }
        struct Ctx {
            kcdxPlugin_Preload_t fn;
            const kcdxInterface* api;
            bool                 result;
        };
        Ctx ctx{p.preloadFn, api, false};
        bool noFault = guard::Call(
            "plugin.preload",
            p.manifest.name.c_str(),
            [](void* ud) {
                Ctx* c = static_cast<Ctx*>(ud);
                c->result = c->fn(c->api);
            },
            &ctx);
        bool ok = noFault && ctx.result;
        if (!noFault) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Preload faulted (see GUARD line)",
                        p.manifest.name.c_str());
        } else if (!ctx.result) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Preload returned false",
                        p.manifest.name.c_str());
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_Preload OK", p.manifest.name.c_str());
        }
        (void)ok;
    }

    // Step 6 — Load wave.
    for (auto& p : g_plugins) {
        // load_order.toml disabled gate. Mark the plugin as not-loaded
        // so enumeration reflects the user's choice. The DLL stays in
        // process (already mapped) but kcdxPlugin_Load is not called.
        // Skip-log distinguishes engine-reject (zone_gate) from
        // user-disabled cause; PLUGIN_REJECTED was already emitted loudly.
        if (!load_order::IsPluginEnabled(p.manifest.name)) {
            // zone_gate keys g_rejected on the 2-dot <author>.<plugin>
            // form (matches kcdx.plugin.is_rejected lookup shape). Pass
            // the composed key here.
            const std::string& rejectReason =
                zone_gate::RejectReason(
                    p.manifest.author + "." + p.manifest.name);
            if (!rejectReason.empty()) {
                log::InfoF("Plugin '%s' kcdxPlugin_Load skipped "
                           "(rejected by zone_gate: %s)",
                           p.manifest.name.c_str(), rejectReason.c_str());
            } else {
                log::InfoF("Plugin '%s' kcdxPlugin_Load skipped "
                           "(plugin disabled via load_order.toml)",
                           p.manifest.name.c_str());
            }
            p.loaded = false;
            continue;
        }
        if (!p.loadFn) {
            // No Load function. That's OK for TOML-only plugins (purely
            // declarative — patches/hooks already applied via config.cpp's
            // entry processors). Note as "TOML-only" rather than warning.
            p.loaded = true;
            continue;
        }
        struct Ctx {
            kcdxPlugin_Load_t    fn;
            const kcdxInterface* api;
            bool                 result;
        };
        Ctx ctx{p.loadFn, api, false};
        bool noFault = guard::Call(
            "plugin.load",
            p.manifest.name.c_str(),
            [](void* ud) {
                Ctx* c = static_cast<Ctx*>(ud);
                c->result = c->fn(c->api);
            },
            &ctx);
        bool ok = noFault && ctx.result;
        if (!noFault) {
            log::ErrorF("Plugin '%s' kcdxPlugin_Load faulted (see GUARD line)",
                        p.manifest.name.c_str());
        } else if (!ctx.result) {
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

    // Step 7 — Lifecycle: fire kcdxMessage_PostLoad, then kcdxMessage_PostPostLoad.
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

// Convert a plugin handle to the registering plugin's [plugin].name (the
// namespace prefix the symbol / author-target resolvers expect). The handle
// is the index into g_plugins (assigned in DiscoverAndLoad); guard it exactly
// as Thunk_Log does. An invalid / unknown handle yields "" — the anonymous
// (engine-seed-only, no self tier) path, which is correct-but-narrower, never
// a mis-attribution to the wrong owner. Moved out of interfaces.cpp so
// kcdxHookInterface thunks (which live in src/hook_interface.cpp, not
// src/interfaces.cpp) can call it directly without a TU-cross fanout helper.
const std::string& NameForHandle(kcdxPluginHandle owner) {
    static const std::string kEmpty;
    if (owner == kcdxInvalidPluginHandle) return kEmpty;
    size_t idx = static_cast<size_t>(owner);
    if (idx >= g_plugins.size()) return kEmpty;
    return g_plugins[idx].manifest.name;
}

// Convert a plugin handle to the registering plugin's [plugin].author
// (the leading namespace component under the 2-dot
// <author>.<plugin>.<bare> model). Same invalid-handle discipline as
// NameForHandle. An empty result is the
// in-progress namespace refactor's "legacy 1-dot row" tier (the corpus
// today; a later step populates [plugin].author across the
// manifests), which the resolver tolerates by walking the legacy 1-dot
// scope under (plugin, name). Same relocation as NameForHandle.
const std::string& AuthorForHandle(kcdxPluginHandle owner) {
    static const std::string kEmpty;
    if (owner == kcdxInvalidPluginHandle) return kEmpty;
    size_t idx = static_cast<size_t>(owner);
    if (idx >= g_plugins.size()) return kEmpty;
    return g_plugins[idx].manifest.author;
}

void RunPostGameLoad(const kcdxInterface* api) {
    // The C++ mirror of lua_after. Like RunAfterEntrypoints, this runs in
    // LOAD-ORDER PRIORITY, NOT g_plugins (topo/discovery) order: a
    // PostGameLoad body may call game functions and observe before-work, so
    // its RUN order is observable and must follow the plugin's load_order
    // priority. Build an index over plugins that export PostGameLoad, sorted
    // by (priority asc, name asc) — zone is irrelevant: PostGameLoad is
    // after_game by construction. Mirrors the sort in
    // lua_plugin_loader::RunAfterEntrypoints.
    std::vector<LoadedPlugin*> ordered;
    ordered.reserve(g_plugins.size());
    for (auto& p : g_plugins) {
        if (!p.postGameLoadFn) continue;  // optional export; skip if absent
        ordered.push_back(&p);
    }
    std::sort(ordered.begin(), ordered.end(),
              [](const LoadedPlugin* a, const LoadedPlugin* b) {
                  const auto& ea = load_order::Of(a->manifest.name);
                  const auto& eb = load_order::Of(b->manifest.name);
                  if (ea.priority != eb.priority) {
                      return ea.priority < eb.priority;
                  }
                  // orderIndex tiebreak (INT_MAX for plugins — a no-op among
                  // them; finite only for folded pak-mod rows).
                  if (ea.orderIndex != eb.orderIndex) {
                      return ea.orderIndex < eb.orderIndex;
                  }
                  return a->manifest.name < b->manifest.name;
              });

    size_t ran = 0;
    for (LoadedPlugin* pp : ordered) {
        LoadedPlugin& p = *pp;

        // Honor the load_order.toml enabled gate — same as the load wave
        // and the lua_after slot. Skip-log distinguishes engine-reject
        // (zone_gate) from user-disabled cause; PLUGIN_REJECTED was
        // already emitted loudly.
        if (!load_order::IsPluginEnabled(p.manifest.name)) {
            // zone_gate keys g_rejected on the 2-dot <author>.<plugin>
            // form (matches kcdx.plugin.is_rejected lookup shape). Pass
            // the composed key here.
            const std::string& rejectReason =
                zone_gate::RejectReason(
                    p.manifest.author + "." + p.manifest.name);
            if (!rejectReason.empty()) {
                log::InfoF("Plugin '%s' kcdxPlugin_PostGameLoad skipped "
                           "(rejected by zone_gate: %s)",
                           p.manifest.name.c_str(), rejectReason.c_str());
            } else {
                log::InfoF("Plugin '%s' kcdxPlugin_PostGameLoad skipped "
                           "(plugin disabled via load_order.toml)",
                           p.manifest.name.c_str());
            }
            continue;
        }

        struct Ctx {
            kcdxPlugin_PostGameLoad_t fn;
            const kcdxInterface*      api;
            bool                      result;
        };
        Ctx ctx{p.postGameLoadFn, api, false};
        bool noFault = guard::Call(
            "plugin.post_game_load",
            p.manifest.name.c_str(),
            [](void* ud) {
                Ctx* c = static_cast<Ctx*>(ud);
                c->result = c->fn(c->api);
            },
            &ctx);
        if (!noFault) {
            log::ErrorF("Plugin '%s' kcdxPlugin_PostGameLoad faulted (see GUARD line)",
                        p.manifest.name.c_str());
        } else if (!ctx.result) {
            log::ErrorF("Plugin '%s' kcdxPlugin_PostGameLoad returned false",
                        p.manifest.name.c_str());
        } else {
            log::InfoF("Plugin '%s' kcdxPlugin_PostGameLoad OK", p.manifest.name.c_str());
        }
        ++ran;
    }

    if (ran > 0) {
        log::InfoF("Plugin DLL loader (after_game): %zu plugin(s) ran kcdxPlugin_PostGameLoad",
                   ran);
    }
}

}  // namespace kcdx::plugins
