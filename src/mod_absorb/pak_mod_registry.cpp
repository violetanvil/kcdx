#include "pak_mod_registry.h"

#include <windows.h>  // WideCharToMultiByte (folder name -> UTF-8 mod id)

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <unordered_map>
#include <vector>

#include "mod_manifest.h"
#include "../load_order.h"
#include "../log.h"
#include "../version_compat.h"

namespace fs = std::filesystem;

namespace kcdx::mod_absorb {

namespace {

// Process-lifetime registry. Heap container so a later push_back never tears a
// stored modId out from under a "mods.<modid>" lookup mid-resolve.
std::vector<PakMod> g_registry;

// Trim ASCII whitespace from both ends.
std::string TrimWs(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

std::vector<PakMod>& Registry() { return g_registry; }

void ClearRegistry() { g_registry.clear(); }

std::string LoadOrderNameFor(const std::string& modId) {
    return "mods." + modId;
}

std::unordered_map<std::string, int> ParseModOrderText(const std::string& text) {
    // One mod id per line, file order == load/mount order. Strip '#' comment
    // lines (a line whose first non-blank char is '#') and blank lines; trim
    // each surviving line. The index is the 0-based position AMONG SURVIVORS,
    // so a comment between two entries does not create a gap.
    std::unordered_map<std::string, int> out;
    std::istringstream in(text);
    std::string line;
    int idx = 0;
    while (std::getline(in, line)) {
        const std::string trimmed = TrimWs(line);
        if (trimmed.empty()) continue;       // blank line
        if (trimmed[0] == '#') continue;     // full-line comment
        // First-wins on a duplicate modid (the vanilla file is one-id-per-line;
        // a dup is a malformed file — keep the earliest position, do not silently
        // re-index).
        out.emplace(trimmed, idx);
        ++idx;
    }
    return out;
}

std::unordered_map<std::string, int> ReadModOrder(const fs::path& modsDir) {
    const fs::path orderPath = modsDir / "mod_order.txt";
    std::error_code ec;
    if (!fs::exists(orderPath, ec)) {
        LOG_INFO("MOD_ABSORB",
            "no mod_order.txt at '%s' — pak mods will order by mod id "
            "(none have a vanilla baseline position this run)",
            orderPath.string().c_str());
        return {};
    }
    std::ifstream f(orderPath, std::ios::binary);
    if (!f) {
        LOG_WARN("MOD_ABSORB",
            "mod_order.txt present but unreadable: '%s' — pak mods will order "
            "by mod id (the vanilla baseline order is lost this run)",
            orderPath.string().c_str());
        return {};
    }
    std::ostringstream ss;
    ss << f.rdbuf();
    auto map = ParseModOrderText(ss.str());
    LOG_INFO("MOD_ABSORB",
        "mod_order.txt: %zu listed mod(s) from '%s'",
        map.size(), orderPath.string().c_str());
    return map;
}

void Discover(const fs::path& root, bool fromModsDir) {
    std::error_code ec;
    if (!fs::exists(root, ec) || !fs::is_directory(root, ec)) {
        // Not a finding — an absent root is a normal install state (no mods/,
        // or no kcdx-plugins/). The caller logs the funnel summary; here we
        // only note the skip at DEBUG so the discovery funnel is traceable.
        LOG_DEBUG_KV("MOD_ABSORB", "discover_skip",
            kcdx::log::KV("root", root.string()),
            kcdx::log::KV("reason", "root absent or not a directory"));
        return;
    }

    for (const auto& entry : fs::directory_iterator(root, ec)) {
        const fs::path p = entry.path();
        const std::wstring name = p.filename().wstring();

        // Skip hidden folders/files (.git, .DS_Store, etc.) — same rule as the
        // plugin walker.
        if (!name.empty() && name[0] == L'.') continue;

        if (!entry.is_directory(ec)) continue;  // pak mods are folders

        const fs::path kcdxToml    = p / "kcdx.toml";
        const fs::path modManifest = p / "mod.manifest";

        if (fs::exists(kcdxToml, ec)) {
            // A kcdx plugin (even if dropped in mods/). The plugin walker owns
            // it; do NOT double-register as a pak mod.
            LOG_DEBUG_KV("MOD_ABSORB", "discover_skip",
                kcdx::log::KV("folder", p.string()),
                kcdx::log::KV("reason", "has kcdx.toml — owned by the plugin walker"));
            continue;
        }

        if (fs::exists(modManifest, ec)) {
            ModManifest m = ReadModManifest(modManifest);
            if (!m.ok) {
                // ReadModManifest already logged LOUD (unreadable / malformed).
                // A pak mod with no usable manifest is NOT registered — fail
                // loud, never a silent empty record.
                continue;
            }
            PakMod mod;
            // The mod id: manifest <modid> if present, else the folder name (the
            // native loader uses the folder name as the id — design field map).
            std::string folderName;
            {
                int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                            nullptr, 0, nullptr, nullptr);
                folderName.assign(n > 0 ? n - 1 : 0, '\0');
                if (n > 0) {
                    WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                        folderName.data(), n, nullptr, nullptr);
                }
            }
            mod.modId = !m.modId.empty() ? m.modId : folderName;
            if (mod.modId.empty()) {
                LOG_WARN("MOD_ABSORB",
                    "pak mod at '%s' has no <modid> AND no usable folder name — "
                    "cannot synthesize a load-order key; skipping",
                    p.string().c_str());
                continue;
            }
            // Root path with + without a trailing separator (the record needs
            // both, per the I_Mod field map). Use forward '/' for the
            // trailing-slash form to match the native record shape (path +0x08
            // ends in '/').
            std::string noSlash = p.string();
            mod.rootPathNoSlash = noSlash;
            mod.rootPathSlash   = noSlash + "/";
            mod.manifest        = std::move(m);
            mod.fromModsDir     = fromModsDir;
            mod.modOrderIndex   = -1;  // caller populates from the mod_order map

            LOG_INFO("MOD_ABSORB",
                "pak mod discovered: id='%s' from %s ('%s')",
                mod.modId.c_str(),
                fromModsDir ? "mods/" : "kcdx-plugins/",
                p.string().c_str());
            g_registry.push_back(std::move(mod));
            // A pak mod folder is claimed — do NOT recurse into it.
            continue;
        }

        // Neither marker: a container folder. Recurse to find pak mods nested
        // one level down (mirrors the plugin walker's container recursion).
        Discover(p, fromModsDir);
    }
}

void DiscoverWorkshop(const fs::path& workshopRoot) {
    LOG_INFO_KV("MOD_ABSORB", "discover_workshop_start",
        kcdx::log::KV("path", workshopRoot.string()));

    std::error_code ec;
    if (workshopRoot.empty()
        || !fs::exists(workshopRoot, ec)
        || !fs::is_directory(workshopRoot, ec)) {
        // Not an error — a player without Steam, or with Steam but no KCD2
        // Workshop subscriptions, is a valid install state. Info-log the skip
        // with the reason so the discovery funnel is traceable.
        const char* reason = workshopRoot.empty()
            ? "KCD2 is not a Steam install (no Steam library detected via "
              "filesystem walk) — Steam Workshop content is not applicable "
              "for this install"
            : "workshop root absent or not a directory";
        LOG_INFO_KV("MOD_ABSORB", "discover_workshop_skipped",
            kcdx::log::KV("path", workshopRoot.string()),
            kcdx::log::KV("reason", reason));
        return;
    }

    size_t examined = 0, registered = 0, rejected = 0;
    for (const auto& entry : fs::directory_iterator(workshopRoot, ec)) {
        const fs::path p = entry.path();
        const std::wstring name = p.filename().wstring();

        // Skip hidden folders/files (same rule as the mods/ + plugins walks).
        if (!name.empty() && name[0] == L'.') continue;

        // Workshop items are folders one level under workshopRoot. Loose files
        // (a stray .vdf, a downloader temp file) are not items — skip silently.
        if (!entry.is_directory(ec)) continue;

        ++examined;

        const fs::path kcdxToml    = p / "kcdx.toml";
        const fs::path modManifest = p / "mod.manifest";

        if (fs::exists(kcdxToml, ec)) {
            // A Workshop folder containing a kcdx plugin is not a current
            // distribution path (Steam Workshop only accepts pak mods), but if
            // one appears we refuse to double-register it as a vanilla pak mod.
            // The plugin walker does not scan workshop/, so this is genuinely
            // unowned — log loud and skip.
            LOG_WARN("MOD_ABSORB",
                "Workshop item at '%s' contains a kcdx.toml — Workshop is a "
                "pak-mod distribution channel, not a kcdx-plugin one; the "
                "folder is NOT registered. Move the plugin to kcdx-plugins/.",
                p.string().c_str());
            ++rejected;
            continue;
        }

        if (!fs::exists(modManifest, ec)) {
            // Fail LOUD: a Workshop subscription that does not contain a
            // mod.manifest is malformed (Steam Workshop items for KCD2 are
            // vanilla pak mods; mod.manifest is the marker). Never a silent
            // skip — the user installed something, and it being absent from
            // the registry without a signal would mis-attribute "my Workshop
            // mod isn't loading" to kcdx.
            LOG_WARN("MOD_ABSORB",
                "Workshop item at '%s' has no mod.manifest — not a valid pak "
                "mod; skipping. (Workshop folder names should be the Steam "
                "Workshop file ID; the contents should match a vanilla pak "
                "mod layout: mod.manifest + Data/*.pak.)",
                p.string().c_str());
            ++rejected;
            continue;
        }

        ModManifest m = ReadModManifest(modManifest);
        if (!m.ok) {
            // ReadModManifest already logged LOUD (unreadable / malformed) —
            // a pak mod with no usable manifest is NOT registered.
            ++rejected;
            continue;
        }

        PakMod mod;
        // The mod id: manifest <modid> if present, else the folder name. The
        // folder name in workshop/content/1771300/<id>/ IS the Steam Workshop
        // file ID — a perfectly serviceable id when no <modid> is declared.
        std::string folderName;
        {
            int n = WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                        nullptr, 0, nullptr, nullptr);
            folderName.assign(n > 0 ? n - 1 : 0, '\0');
            if (n > 0) {
                WideCharToMultiByte(CP_UTF8, 0, name.c_str(), -1,
                                    folderName.data(), n, nullptr, nullptr);
            }
        }
        mod.modId = !m.modId.empty() ? m.modId : folderName;
        if (mod.modId.empty()) {
            LOG_WARN("MOD_ABSORB",
                "Workshop pak mod at '%s' has no <modid> AND no usable folder "
                "name — cannot synthesize a load-order key; skipping",
                p.string().c_str());
            ++rejected;
            continue;
        }

        // Root path with + without a trailing separator (same shape as
        // Discover for the mods/ + kcdx-plugins/ paths).
        std::string noSlash = p.string();
        mod.rootPathNoSlash = noSlash;
        mod.rootPathSlash   = noSlash + "/";
        mod.manifest        = std::move(m);
        mod.fromModsDir     = false;
        mod.fromWorkshop    = true;
        mod.modOrderIndex   = -1;  // mod_order.txt does not list Workshop ids;
                                   // Workshop mods sort AFTER listed mods/ ones.

        LOG_INFO("MOD_ABSORB",
            "pak mod discovered: id='%s' from Steam Workshop ('%s')",
            mod.modId.c_str(), p.string().c_str());
        g_registry.push_back(std::move(mod));
        ++registered;
    }

    LOG_INFO_KV("MOD_ABSORB", "discover_workshop_walked",
        kcdx::log::KV("path", workshopRoot.string()),
        kcdx::log::KV("examined", examined),
        kcdx::log::KV("registered", registered),
        kcdx::log::KV("rejected", rejected));
}

size_t ApplyVersionGate(const std::string& runtimeVersionString) {
    using version_compat::CompatResult;
    size_t disabled = 0;
    for (const PakMod& mod : g_registry) {
        const std::string loName = LoadOrderNameFor(mod.modId);
        switch (DecideModCompat(mod.manifest, runtimeVersionString)) {
            case CompatResult::Incompatible:
                // A known game version, no declared <supports> pattern matches.
                // Disable via the SAME mechanism zone_gate + the plugin path
                // use — IsPluginEnabled("mods.<modid>") then returns false and
                // step 4's enabled-list build honors it.
                kcdx::load_order::SetEngineAccepted(loName, false);
                LOG_INFO("MOD_ABSORB",
                    "pak mod '%s' not compatible with game '%s' — disabled "
                    "(its mod.manifest <supports> declares no matching version)",
                    mod.modId.c_str(), runtimeVersionString.c_str());
                ++disabled;
                break;
            case CompatResult::UnknownGameVersion:
                // We couldn't evaluate a declared restriction (no runtime
                // version string). Graceful-degrade: stay enabled, but WARN —
                // mirrors the plugin path's "loading anyway".
                if (!mod.manifest.supports.empty()) {
                    LOG_WARN("MOD_ABSORB",
                        "pak mod '%s' declares a <supports> restriction but the "
                        "running game version is unknown — enabling anyway",
                        mod.modId.c_str());
                }
                break;
            case CompatResult::Compatible:
                // No restriction, or a matching one. Stay enabled (the default).
                break;
        }
    }
    return disabled;
}

}  // namespace kcdx::mod_absorb
