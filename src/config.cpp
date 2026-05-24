#include "config.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

#include "address_library.h"  // ValidatePluginName (hard-rejects illegal [plugin].name)

// toml++ is header-only.
#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include "hook_engine.h"
#include "dev.h"
#include "load_order.h"
#include "log.h"
#include "patch_engine.h"
#include "paths.h"
#include "plugin_loader.h"
#include "scan_engine.h"
#include "target_manifest.h"
#include "test.h"
#include "trampoline_engine.h"

namespace fs = std::filesystem;
namespace kcdx::config {

// Bring KV into scope for LOG_*_KV macro call sites below. The macros
// expand to `{ KV(...), KV(...) }` initializer lists and need an
// unqualified KV resolveable in the macro-expansion's local scope.
using KV = ::kcdx::log::KV;

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

std::string OptString(const toml::table& tbl, std::string_view key,
                      std::string_view fallback = "") {
    if (auto* v = tbl.get(key); v && v->is_string()) {
        return std::string(*v->value<std::string>());
    }
    return std::string(fallback);
}

int OptInt(const toml::table& tbl, std::string_view key, int fallback) {
    if (auto* v = tbl.get(key); v && v->is_integer()) {
        return static_cast<int>(*v->value<int64_t>());
    }
    return fallback;
}

bool OptBool(const toml::table& tbl, std::string_view key, bool fallback) {
    if (auto* v = tbl.get(key); v && v->is_boolean()) {
        return *v->value<bool>();
    }
    return fallback;
}

// Parse a semver string like "1.2.3" into the packed format kcdx uses
// internally: (major << 24) | (minor << 16) | (patch << 8). Trailing field
// is reserved (currently always 0). Missing fields default to 0.
//
// Returns true on success. On parse error, leaves out unchanged and returns
// false with err populated.
bool ParseSemver(const std::string& s, uint32_t& out, std::string& err) {
    if (s.empty()) {
        out = 0;
        return true;  // empty == 0 == "any version"
    }
    uint32_t major = 0, minor = 0, patch = 0;
    const char* p = s.c_str();
    char* endp = nullptr;
    auto parse_field = [&](uint32_t& field, const char* labelForErr) -> bool {
        unsigned long v = std::strtoul(p, &endp, 10);
        if (endp == p) {
            err = std::string("expected ") + labelForErr + " number near '" + p + "'";
            return false;
        }
        if (v > 0xFFu) {
            err = std::string(labelForErr) + " number out of range (0..255)";
            return false;
        }
        field = static_cast<uint32_t>(v);
        p = endp;
        return true;
    };
    if (!parse_field(major, "major")) return false;
    if (*p == '.') { ++p; if (!parse_field(minor, "minor")) return false; }
    if (*p == '.') { ++p; if (!parse_field(patch, "patch")) return false; }
    // Allow trailing junk (pre-release suffixes like "-rc1") by ignoring.
    out = (major << 24) | (minor << 16) | (patch << 8);
    return true;
}

// Parse a KCD2 game-version string ("1.5.1164953") into the kcdxMakeGameVersion
// packed form (major<<24 | minor<<16 | (build & 0xFFFF)). Returns 0 + err on
// parse failure.
bool ParseGameVersion(const std::string& s, uint32_t& out, std::string& err) {
    uint32_t major = 0, minor = 0, build = 0;
    const char* p = s.c_str();
    char* endp = nullptr;
    auto parse_field = [&](uint32_t& field, const char* labelForErr) -> bool {
        unsigned long v = std::strtoul(p, &endp, 10);
        if (endp == p) {
            err = std::string("expected ") + labelForErr + " number near '" + p + "'";
            return false;
        }
        field = static_cast<uint32_t>(v);
        p = endp;
        return true;
    };
    if (!parse_field(major, "major")) return false;
    if (*p != '.') { err = "expected '.' after major"; return false; }
    ++p;
    if (!parse_field(minor, "minor")) return false;
    if (*p != '.') { err = "expected '.' after minor"; return false; }
    ++p;
    if (!parse_field(build, "build")) return false;
    if (major > 0xFFu || minor > 0xFFu) {
        err = "major/minor out of range (0..255)";
        return false;
    }
    out = ((major & 0xFFu) << 24) | ((minor & 0xFFu) << 16) | (build & 0xFFFFu);
    return true;
}

// Parse the [plugin] + [entrypoints] sections of a kcdx.toml into a
// PluginManifest. Returns true on success. The 'name' field is the only
// required key; if [plugin] is absent or has no 'name', returns false (the
// plugin is treated as a config-only kcdx.toml with no identity — patches
// and hooks in the same file still apply, but the plugin doesn't appear in
// the loaded-plugins list or participate in dependency topo-sort).
bool ParsePluginManifest(const toml::table& doc,
                         const fs::path& tomlPath,
                         kcdx::plugins::PluginManifest& out,
                         std::string& err) {
    auto* pluginTbl = doc.get("plugin");
    if (!pluginTbl || !pluginTbl->is_table()) {
        err = "no [plugin] table";
        return false;
    }
    const auto& t = *pluginTbl->as_table();

    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "[plugin] missing required field 'name'";
        return false;
    }
    // Enforce the [plugin].name shape at discovery — naming-namespaces.md
    // requires a charset-legal bare name (no '.', no '-', no uppercase, etc.)
    // and rejects the reserved 'kcdx' root + 'kcdx.' prefix. A plugin with an
    // illegal name corrupts every shared name it would export, so a HARD load
    // rejection here (per the restructure's loud-full-rejection-over-silent-
    // partial stance) is correct. The author-target / alias surfaces already
    // re-validate via RegisterAuthorTarget / RegisterAlias, but those fire
    // only IF a plugin uses them — wiring the check here gates EVERY plugin
    // at load.
    std::string nameErr;
    if (!kcdx::address_library::ValidatePluginName(out.name.c_str(), nameErr)) {
        err = "[plugin] name: " + nameErr;
        return false;
    }
    out.displayName  = OptString(t, "display_name", out.name);
    out.author       = OptString(t, "author", "");
    // [plugin].author is the leading component of the 2-dot
    // <author>.<plugin>.<bare> namespace prefix (naming-namespaces.md). It is
    // OPTIONAL during the in-progress namespace-refactor transition — the
    // corpus is re-migrated in a later step, after which the field can be
    // made required. For now: empty is accepted (no validation, no prefix
    // contribution); non-empty must obey the same charset / length /
    // reserved-root rules a namespace component obeys, identical to
    // [plugin].name (validated via ValidateAuthorName, which mirrors
    // ValidatePluginName's shape).
    if (!out.author.empty()) {
        std::string authorErr;
        if (!kcdx::address_library::ValidateAuthorName(out.author.c_str(),
                                                        authorErr)) {
            err = "[plugin] author: " + authorErr;
            return false;
        }
    }
    out.description  = OptString(t, "description");
    out.url          = OptString(t, "url");
    out.supportEmail = OptString(t, "support_email");

    std::string verStr = OptString(t, "version");
    std::string semErr;
    if (!ParseSemver(verStr, out.version, semErr)) {
        err = "[plugin] version: " + semErr;
        return false;
    }

    std::string kcdxMinStr = OptString(t, "kcdx_min_version");
    if (!ParseSemver(kcdxMinStr, out.kcdxMinVersion, semErr)) {
        err = "[plugin] kcdx_min_version: " + semErr;
        return false;
    }

    out.versionIndependent = OptBool(t, "version_independent", false);

    // Load-order author hints (zone + priority). Both are optional. See
    // docs/load-order.md for the full model — zones partition plugins into
    // before_game vs after_game (the game.exe sentinel divides them);
    // priority orders plugins within their zone.
    //
    //   default_position = "before_game" | "after_game" | ""
    //     ""  → engine derives from capabilities at load time
    //           (engine builtins default to before_game; user plugins
    //           default to after_game when their entries are flexible).
    //
    //   default_priority = 0..100  (default 50)
    //     0 = earliest in zone, 100 = latest. Sparse range gives users /
    //     authors room to insert "definitely before X" without renumbering.
    {
        std::string pos = OptString(t, "default_position");
        if (!pos.empty() && pos != "before_game" && pos != "after_game") {
            err = "[plugin] default_position: unknown value '" + pos + "' "
                  "(expected 'before_game' or 'after_game')";
            return false;
        }
        out.defaultPosition = pos;
    }
    {
        int prio = OptInt(t, "default_priority", 50);
        if (prio < 0 || prio > 100) {
            err = "[plugin] default_priority: out of range (" +
                  std::to_string(prio) + "); expected 0..100";
            return false;
        }
        out.defaultPriority = prio;
    }

    // log_level is a per-plugin floor for the plugin's own log file.
    // Maps to the kcdxLog_* enum ordering:
    //   trace(0) < debug(1) < info(2) < warn(3) < error(4) < off(5)
    //
    // Default "info" passes Info/Warn/Error to the plugin's file;
    // Debug/Trace are gated by dev mode. "off" suppresses Info and
    // below — Warn and Error always pass to the plugin file
    // regardless of this setting (the floor never gates problems;
    // see kcdx/docs/logging.md).
    {
        std::string lvl = OptString(t, "log_level", "info");
        if      (lvl == "trace") out.logLevel = 0;  // kcdxLog_Trace
        else if (lvl == "debug") out.logLevel = 1;  // kcdxLog_Debug
        else if (lvl == "info")  out.logLevel = 2;  // kcdxLog_Info
        else if (lvl == "warn")  out.logLevel = 3;  // kcdxLog_Warn
        else if (lvl == "error") out.logLevel = 4;  // kcdxLog_Error
        else if (lvl == "off")   out.logLevel = 5;  // synthetic "drop all"
        else {
            err = "[plugin] log_level: unknown level '" + lvl + "' "
                  "(expected trace|debug|info|warn|error|off)";
            return false;
        }
    }

    // compatible_game_versions = [ "1.5.1164953", ... ]
    if (auto* arr = t.get("compatible_game_versions"); arr && arr->is_array()) {
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_string()) continue;
            std::string s = std::string(*elem.value<std::string>());
            uint32_t v = 0;
            std::string gvErr;
            if (!ParseGameVersion(s, v, gvErr)) {
                err = "[plugin] compatible_game_versions[\"" + s + "\"]: " + gvErr;
                return false;
            }
            out.compatibleGameVersions.push_back(v);
        }
    }

    // [[plugin.dependencies]] array
    if (auto* arr = t.get("dependencies"); arr && arr->is_array()) {
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_table()) continue;
            const auto& dt = *elem.as_table();
            kcdx::plugins::ManifestDependency dep;
            dep.name = OptString(dt, "name");
            if (dep.name.empty()) {
                err = "[[plugin.dependencies]] entry missing 'name'";
                return false;
            }
            std::string minStr = OptString(dt, "min_version");
            if (!ParseSemver(minStr, dep.minVersion, semErr)) {
                err = "[[plugin.dependencies]] '" + dep.name + "' min_version: " + semErr;
                return false;
            }
            dep.optional = OptBool(dt, "optional", false);
            out.dependencies.push_back(std::move(dep));
        }
    }

    // [entrypoints] section
    if (auto* entryTbl = doc.get("entrypoints"); entryTbl && entryTbl->is_table()) {
        const auto& et = *entryTbl->as_table();
        out.dllEntrypointRel      = OptString(et, "dll");
        // No "dll_after" key: a C++ plugin's after-game work is an OPTIONAL
        // kcdxPlugin_PostGameLoad export on the SAME plugin DLL (the `dll`
        // entrypoint), mirroring how Preload/Load coexist on one module —
        // not a separate DLL file. The export is resolved at discovery
        // (plugin_loader.cpp) and dispatched by plugins::RunPostGameLoad in
        // the after_game phase. The speculative dll_after path field was
        // removed once that export model was settled.

        // lua = "plugin.lua"  OR  lua = ["plugin.lua", "scripts/extras.lua"].
        // Accept both forms: a bare string is treated as a one-element list.
        // Files run in declared order at the plugin's load-order slot. This
        // is the BEFORE-or-default slot (runs in the plugin's declared zone).
        if (auto* luaNode = et.get("lua")) {
            if (luaNode->is_string()) {
                out.luaEntrypointsRel.push_back(
                    std::string(*luaNode->value<std::string>()));
            } else if (luaNode->is_array()) {
                for (const auto& elem : *luaNode->as_array()) {
                    if (elem.is_string()) {
                        out.luaEntrypointsRel.push_back(
                            std::string(*elem.value<std::string>()));
                    }
                }
            }
        }

        // lua_after = "after.lua"  OR  lua_after = ["after.lua", ...] — the
        // OPTIONAL after-game Lua slot. Same string-or-array shape as `lua`;
        // these files run in the AFTER_GAME phase at the plugin's priority,
        // regardless of declared zone (see RunAfterEntrypoints). A both-phase
        // plugin declares `lua` (before) AND `lua_after` (after).
        if (auto* luaAfterNode = et.get("lua_after")) {
            if (luaAfterNode->is_string()) {
                out.luaAfterEntrypointsRel.push_back(
                    std::string(*luaAfterNode->value<std::string>()));
            } else if (luaAfterNode->is_array()) {
                for (const auto& elem : *luaAfterNode->as_array()) {
                    if (elem.is_string()) {
                        out.luaAfterEntrypointsRel.push_back(
                            std::string(*elem.value<std::string>()));
                    }
                }
            }
        }
    }

    // [plugin] test_names = ["CAP-XX", ...] — for test-suite plugins,
    // the matrix row IDs this plugin promises to report. Used by the
    // aggregator to track PENDING (registered but no report yet) vs
    // reported. Empty = no expectation; plugin still counts in
    // "N gated off" but no PENDING tracking.
    if (auto* arr = t.get("test_names"); arr && arr->is_array()) {
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_string()) continue;
            out.testNames.push_back(std::string(*elem.value<std::string>()));
        }
    }

    out.tomlPath   = tomlPath;
    out.folderPath = tomlPath.parent_path();
    return true;
}

bool ParseOneScan(const toml::table& t,
                  const std::string& sourceFile,
                  kcdx::scan_engine::ScanEntry& out,
                  std::string& err) {
    using namespace kcdx::patch;
    out.sourceFile = sourceFile;
    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "missing required field 'name'";
        return false;
    }
    out.module = OptString(t, "module", "WHGame.dll");

    std::string patternStr = OptString(t, "pattern");
    if (patternStr.empty()) {
        err = "missing required field 'pattern'";
        return false;
    }
    try {
        out.pattern = ParsePattern(patternStr);
    } catch (const std::exception& e) {
        err = std::string("parse error in 'pattern': ") + e.what();
        return false;
    }

    out.offset = OptInt(t, "offset", 0);

    if (auto* v = t.get("context"); v && v->is_string()) {
        try {
            out.context = ParsePattern(std::string(*v->value<std::string>()));
        } catch (const std::exception& e) {
            err = std::string("parse error in 'context': ") + e.what();
            return false;
        }
    }

    // Anchors — same shape as [[patch]] / [[hook]].
    int anchorCount = 0;
    if (auto* v = t.get("anchor_string"); v && v->is_string()) {
        out.anchor = AnchorString{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_function_by_export"); v && v->is_string()) {
        out.anchor = AnchorFunctionByExport{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_symbol"); v && v->is_string()) {
        out.anchor = AnchorSymbol{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (anchorCount > 1) {
        err = "only one of anchor_string / anchor_function_by_export / anchor_symbol may be declared";
        return false;
    }
    out.maxAnchorDistance = static_cast<uint32_t>(OptInt(t, "max_anchor_distance", 4096));

    return true;
}

bool ParseOnePatch(const toml::table& t,
                   const std::string& sourceFile,
                   kcdx::patch::PatchEntry& out,
                   std::string& err) {
    using namespace kcdx::patch;
    out.sourceFile = sourceFile;
    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "missing required field 'name'";
        return false;
    }
    out.description = OptString(t, "description");
    out.priority = OptInt(t, "priority", 100);
    out.module = OptString(t, "module", "WHGame.dll");

    // Locator: exactly one of 'pattern' (v1 AOB path),
    // 'target_symbol' (cross-plugin symbol table), or 'address_id'
    // (kcdx Address Library, Phase 7).
    std::string patternStr      = OptString(t, "pattern");
    std::string targetSymbolStr = OptString(t, "target_symbol");
    int64_t     addressIdInt    = OptInt(t, "address_id", 0);
    int        locatorCount     = (!patternStr.empty() ? 1 : 0)
                                 + (!targetSymbolStr.empty() ? 1 : 0)
                                 + (addressIdInt != 0 ? 1 : 0);
    if (locatorCount == 0) {
        err = "missing locator: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (locatorCount > 1) {
        err = "conflicting locators: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (!patternStr.empty()) {
        try {
            out.pattern = ParsePattern(patternStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'pattern': ") + e.what();
            return false;
        }
    } else if (!targetSymbolStr.empty()) {
        out.targetSymbol = targetSymbolStr;
    } else {
        out.addressId = static_cast<uint64_t>(addressIdInt);
    }

    out.offset = OptInt(t, "offset", 0);

    std::string originalStr = OptString(t, "original");
    std::string replacementStr = OptString(t, "replacement");
    if (originalStr.empty() || replacementStr.empty()) {
        err = "missing required field 'original' or 'replacement'";
        return false;
    }
    try {
        out.original = ParseBytes(originalStr);
        out.replacement = ParseBytes(replacementStr);
    } catch (const std::exception& e) {
        err = std::string("parse error in original/replacement: ") + e.what();
        return false;
    }
    if (out.original.size() != out.replacement.size()) {
        err = "original and replacement must be the same length";
        return false;
    }

    out.idempotent = OptBool(t, "idempotent", true);

    // Tier 2 — context
    if (auto* v = t.get("context"); v && v->is_string()) {
        try {
            out.context = ParsePattern(std::string(*v->value<std::string>()));
        } catch (const std::exception& e) {
            err = std::string("parse error in 'context': ") + e.what();
            return false;
        }
    }

    // Tier 3 — anchors (mutually exclusive)
    int anchorCount = 0;
    if (auto* v = t.get("anchor_string"); v && v->is_string()) {
        out.anchor = AnchorString{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_function_by_export"); v && v->is_string()) {
        out.anchor = AnchorFunctionByExport{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_symbol"); v && v->is_string()) {
        out.anchor = AnchorSymbol{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (anchorCount > 1) {
        err = "only one of anchor_string / anchor_function_by_export / anchor_symbol may be declared";
        return false;
    }
    out.maxAnchorDistance = static_cast<uint32_t>(OptInt(t, "max_anchor_distance", 4096));

    return true;
}

bool ParseOneHook(const toml::table& t,
                  const std::string& sourceFile,
                  kcdx::hook_engine::HookEntry& out,
                  std::string& err) {
    using namespace kcdx::patch;
    out.sourceFile = sourceFile;
    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "missing required field 'name'";
        return false;
    }
    out.description = OptString(t, "description");
    out.priority = OptInt(t, "priority", 100);
    out.module = OptString(t, "module", "WHGame.dll");

    // Locator: exactly one of 'pattern', 'target_symbol' (Phase 4b.2),
    // or 'address_id' (Phase 7).
    std::string patternStr      = OptString(t, "pattern");
    std::string targetSymbolStr = OptString(t, "target_symbol");
    int64_t     addressIdInt    = OptInt(t, "address_id", 0);
    int        locatorCount     = (!patternStr.empty() ? 1 : 0)
                                 + (!targetSymbolStr.empty() ? 1 : 0)
                                 + (addressIdInt != 0 ? 1 : 0);
    if (locatorCount == 0) {
        err = "missing locator: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (locatorCount > 1) {
        err = "conflicting locators: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (!patternStr.empty()) {
        try {
            out.pattern = ParsePattern(patternStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'pattern': ") + e.what();
            return false;
        }
    } else if (!targetSymbolStr.empty()) {
        out.targetSymbol = targetSymbolStr;
    } else {
        out.addressId = static_cast<uint64_t>(addressIdInt);
    }

    out.offset = OptInt(t, "offset", 0);

    // Detour body: exactly one of 'bytes' (raw machine code) or
    // 'lua_callback' (dotted Lua function name; Phase 5f) is required.
    std::string bytesStr      = OptString(t, "bytes");
    std::string luaCallback   = OptString(t, "lua_callback");
    std::string luaPostCb     = OptString(t, "lua_post_callback");

    if (!bytesStr.empty() && !luaCallback.empty()) {
        err = "'bytes' and 'lua_callback' are mutually exclusive";
        return false;
    }
    if (bytesStr.empty() && luaCallback.empty()) {
        err = "must declare exactly one of 'bytes' or 'lua_callback'";
        return false;
    }
    if (!bytesStr.empty() && !luaPostCb.empty()) {
        err = "'lua_post_callback' requires 'lua_callback' (not 'bytes')";
        return false;
    }

    if (!bytesStr.empty()) {
        try {
            out.bytes = ParseBytes(bytesStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'bytes': ") + e.what();
            return false;
        }
    } else {
        out.lua_callback      = luaCallback;
        out.lua_post_callback = luaPostCb;
        out.return_type       = OptString(t, "return_type", "void");

        // param_types: array of strings, default empty.
        if (auto* v = t.get("param_types"); v && v->is_array()) {
            for (const auto& el : *v->as_array()) {
                if (auto s = el.value<std::string>(); s.has_value()) {
                    out.param_types.push_back(*s);
                }
            }
        }
    }

    // Tier 2 — context (same shape as [[patch]])
    if (auto* v = t.get("context"); v && v->is_string()) {
        try {
            out.context = ParsePattern(std::string(*v->value<std::string>()));
        } catch (const std::exception& e) {
            err = std::string("parse error in 'context': ") + e.what();
            return false;
        }
    }

    // Tier 3 — anchors (mutually exclusive, same as [[patch]])
    int anchorCount = 0;
    if (auto* v = t.get("anchor_string"); v && v->is_string()) {
        out.anchor = AnchorString{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_function_by_export"); v && v->is_string()) {
        out.anchor = AnchorFunctionByExport{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_symbol"); v && v->is_string()) {
        out.anchor = AnchorSymbol{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (anchorCount > 1) {
        err = "only one of anchor_string / anchor_function_by_export / anchor_symbol may be declared";
        return false;
    }
    out.maxAnchorDistance = static_cast<uint32_t>(OptInt(t, "max_anchor_distance", 4096));

    return true;
}

bool ParseOneMidHook(const toml::table& t,
                     const std::string& sourceFile,
                     kcdx::hook_engine::MidHookEntry& out,
                     std::string& err) {
    using namespace kcdx::patch;
    out.sourceFile = sourceFile;
    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "missing required field 'name'";
        return false;
    }
    out.description = OptString(t, "description");
    out.priority = OptInt(t, "priority", 100);
    out.module = OptString(t, "module", "WHGame.dll");

    // Locator: exactly one of 'pattern', 'target_symbol' (cross-plugin
    // symbol table), or 'address_id' (Phase 7).
    std::string patternStr      = OptString(t, "pattern");
    std::string targetSymbolStr = OptString(t, "target_symbol");
    int64_t     addressIdInt    = OptInt(t, "address_id", 0);
    int        locatorCount     = (!patternStr.empty() ? 1 : 0)
                                 + (!targetSymbolStr.empty() ? 1 : 0)
                                 + (addressIdInt != 0 ? 1 : 0);
    if (locatorCount == 0) {
        err = "missing locator: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (locatorCount > 1) {
        err = "conflicting locators: declare exactly one of 'pattern', "
              "'target_symbol', or 'address_id'";
        return false;
    }
    if (!patternStr.empty()) {
        try {
            out.pattern = ParsePattern(patternStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'pattern': ") + e.what();
            return false;
        }
    } else if (!targetSymbolStr.empty()) {
        out.targetSymbol = targetSymbolStr;
    } else {
        out.addressId = static_cast<uint64_t>(addressIdInt);
    }

    out.offset = OptInt(t, "offset", 0);

    if (auto* v = t.get("context"); v && v->is_string()) {
        try {
            out.context = ParsePattern(std::string(*v->value<std::string>()));
        } catch (const std::exception& e) {
            err = std::string("parse error in 'context': ") + e.what();
            return false;
        }
    }

    int anchorCount = 0;
    if (auto* v = t.get("anchor_string"); v && v->is_string()) {
        out.anchor = AnchorString{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_function_by_export"); v && v->is_string()) {
        out.anchor = AnchorFunctionByExport{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (auto* v = t.get("anchor_symbol"); v && v->is_string()) {
        out.anchor = AnchorSymbol{std::string(*v->value<std::string>())};
        ++anchorCount;
    }
    if (anchorCount > 1) {
        err = "only one of anchor_string / anchor_function_by_export / anchor_symbol may be declared";
        return false;
    }
    out.maxAnchorDistance = static_cast<uint32_t>(OptInt(t, "max_anchor_distance", 4096));

    out.lua_callback = OptString(t, "lua_callback");
    if (out.lua_callback.empty()) {
        err = "missing required field 'lua_callback'";
        return false;
    }

    out.stack_restore_offset = OptInt(t, "stack_restore_offset", 0);

    // call_original — bool true/false OR string "auto". Default is
    // back-compat: true (the original instruction runs after the
    // callback). See hook_engine.h::CallOriginalMode.
    if (auto* v = t.get("call_original")) {
        if (v->is_boolean()) {
            out.callOriginal = *v->value<bool>()
                ? kcdx::hook_engine::CallOriginalMode::True
                : kcdx::hook_engine::CallOriginalMode::False;
        } else if (v->is_string()) {
            std::string s = std::string(*v->value<std::string>());
            if (s == "auto") {
                out.callOriginal = kcdx::hook_engine::CallOriginalMode::Auto;
            } else if (s == "true") {
                out.callOriginal = kcdx::hook_engine::CallOriginalMode::True;
            } else if (s == "false") {
                out.callOriginal = kcdx::hook_engine::CallOriginalMode::False;
            } else {
                err = "call_original: must be true, false, or \"auto\"";
                return false;
            }
        } else {
            err = "call_original: must be a boolean or the string \"auto\"";
            return false;
        }
    }

    // param_types + param_captures: arrays of strings, same length.
    if (auto* v = t.get("param_types"); v && v->is_array()) {
        for (const auto& el : *v->as_array()) {
            if (auto s = el.value<std::string>(); s.has_value()) {
                out.param_types.push_back(*s);
            }
        }
    }
    if (auto* v = t.get("param_captures"); v && v->is_array()) {
        for (const auto& el : *v->as_array()) {
            if (auto s = el.value<std::string>(); s.has_value()) {
                out.param_captures.push_back(*s);
            }
        }
    }
    if (out.param_types.size() != out.param_captures.size()) {
        err = "param_types and param_captures must be the same length";
        return false;
    }

    return true;
}

bool ParseOneTrampoline(const toml::table& t,
                        const std::string& sourceFile,
                        kcdx::trampoline_engine::TrampolineEntry& out,
                        std::string& err) {
    using namespace kcdx::patch;  // for ParseBytes
    out.sourceFile = sourceFile;
    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "missing required field 'name'";
        return false;
    }
    out.description = OptString(t, "description");
    out.priority = OptInt(t, "priority", 100);

    // bytes is optional only if `size` is set (allocate empty NOP region).
    std::string bytesStr = OptString(t, "bytes");
    if (!bytesStr.empty()) {
        try {
            out.bytes = ParseBytes(bytesStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'bytes': ") + e.what();
            return false;
        }
    }

    // Optional size override (allows NOP-padded tail for other plugins to
    // patch into).
    if (auto* v = t.get("size"); v && v->is_integer()) {
        out.size = static_cast<size_t>(*v->value<int64_t>());
    }
    if (out.bytes.empty() && !out.size.has_value()) {
        err = "trampoline must declare either 'bytes' or 'size' (or both)";
        return false;
    }

    out.pool = OptString(t, "pool", "branch");
    out.exportSymbol = OptString(t, "export");
    return true;
}

// Load the engine-config file at <kcdx-engine>/engine.toml. This is
// the ONLY place engine-wide settings live (dev_mode, dev_log_*,
// dry_run). Plugin kcdx.toml files cannot turn these on — that would
// be cross-plugin contamination.
//
// Called once at startup before any plugin TOML is walked, so the
// engine's IsEnabled() / dry-run flags are settled before any
// downstream gating (test_suite_only, etc.) is evaluated.
//
// Schema:
//
//   [kcdx]
//   dev_mode       = false      # default false; turns on kcdx-dev_<ts>.log
//   dev_categories = ["LUA", "SCRIPTING"]   # empty = all
//   dry_run        = false
//
// Log files are per-session (one file per launch) and live in
// <kcdx-engine>/logs/. Retention is fixed at kcdx::log::kLogRetainCount
// per stream — no TOML knob.
//
// Returns silently if the file doesn't exist (dev mode stays off,
// which is the production default).
void LoadEngineConfig(const fs::path& enginePath) {
    std::error_code ec;
    if (!fs::exists(enginePath, ec)) return;

    std::string fileLabel = enginePath.string();
    try {
        std::ifstream in(enginePath);
        if (!in) {
            log::ErrorF("Failed to open engine config %s", fileLabel.c_str());
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        toml::table doc = toml::parse(ss.str(), enginePath.string());

        auto* top = doc.get("kcdx");
        if (!top || !top->is_table()) {
            log::WarnF("%s: missing [kcdx] table; ignoring file",
                       fileLabel.c_str());
            return;
        }
        const auto& tbl = *top->as_table();

        if (OptBool(tbl, "dry_run", false)) {
            kcdx::patch::g_dryRun = true;
            log::InfoF("dry_run enabled by %s", fileLabel.c_str());
        }

        if (OptBool(tbl, "dev_mode", false)) {
            kcdx::dev::SetEnabled(true);
            log::InfoF("dev_mode enabled by %s", fileLabel.c_str());

            // Optional category filter. If absent or empty, emit every
            // category. If present, only listed categories emit.
            std::vector<std::string> cats;
            if (auto* arr = tbl.get("dev_categories");
                arr && arr->is_array()) {
                for (const auto& elem : *arr->as_array()) {
                    if (elem.is_string()) {
                        cats.push_back(std::string(*elem.value<std::string>()));
                    }
                }
            }
            if (!cats.empty()) {
                kcdx::dev::SetCategoryFilter(cats);
                std::string joined;
                for (size_t i = 0; i < cats.size(); ++i) {
                    if (i) joined += ", ";
                    joined += cats[i];
                }
                log::InfoF("dev_categories filter active: [%s]",
                           joined.c_str());
            }
        }
    } catch (const toml::parse_error& e) {
        log::ErrorF("TOML parse error in engine config %s: %s",
                    fileLabel.c_str(), e.what());
    } catch (const std::exception& e) {
        log::ErrorF("Error reading engine config %s: %s",
                    fileLabel.c_str(), e.what());
    }
}

void LoadOneFile(const fs::path& path, Source source) {
    std::string fileLabel = path.string();
    try {
        std::ifstream in(path);
        if (!in) {
            log::ErrorF("Failed to open %s", fileLabel.c_str());
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        toml::table doc = toml::parse(ss.str(), path.string());

        // Top-level [kcdx] section. Engine-level settings (dev_mode,
        // dev_log_*, dry_run) are NOT allowed here — they live in
        // <kcdx-engine>/engine.toml. Warn if a plugin sets them so
        // the author moves them to the right place.
        bool isTestSuiteOnly = false;
        if (auto* top = doc.get("kcdx"); top && top->is_table()) {
            const auto& tbl = *top->as_table();
            for (const char* k : { "dev_mode", "dev_log_cap_mb",
                                   "dev_log_max_files", "dev_categories",
                                   "dry_run" }) {
                if (tbl.get(k) != nullptr) {
                    log::WarnF("%s: [kcdx] %s is an engine-level setting and "
                               "cannot be set by plugins. Move it to "
                               "<kcdx-engine>/engine.toml instead.",
                               fileLabel.c_str(), k);
                }
            }
            isTestSuiteOnly = OptBool(tbl, "test_suite_only", false);
        }

        // test_suite_only + dev_mode OFF: production-quiet path.
        // We still want to COUNT the plugin so the user sees
        // "Test suite: N plugin(s) gated off" at boot. Bump the
        // counter and skip everything else (no [[patch]]/[[hook]]
        // entries register, plugin DLLs early-return at Plugin_Load).
        if (isTestSuiteOnly && !kcdx::dev::IsEnabled()) {
            kcdx::test::IncrementGatedOffCount();
            return;
        }

        // [plugin] + [entrypoints] sections — plugin identity. Optional;
        // a kcdx.toml with only [[patch]] entries and no [plugin] table is
        // still valid (the patches apply, but the file doesn't register
        // a plugin in g_plugins / g_manifests). We capture the plugin name
        // here and stamp it onto every [[patch]] / [[hook]] / [[mid_hook]]
        // / [[trampoline]] entry parsed from this file, so the load-order
        // sort can look up the owning plugin's effective zone + priority.
        std::string pluginName;    // empty if this file has no [plugin] table
        std::string pluginAuthor;  // empty when no [plugin].author OR no [plugin] table
        {
            kcdx::plugins::PluginManifest manifest;
            std::string mErr;
            if (ParsePluginManifest(doc, path, manifest, mErr)) {
                manifest.testSuiteOnly = isTestSuiteOnly;
                pluginName   = manifest.name;
                pluginAuthor = manifest.author;

                // Optional per-plugin targets.toml sidecar — author-declared
                // hook/byte targets registered under this plugin's namespace.
                // Both identity components (author + plugin name) are passed
                // through so the targets register under the full 2-dot key.
                LoadTargetsFor(path.parent_path().string(),
                               pluginAuthor, pluginName);

                log::InfoF("Discovered plugin '%s' v0x%08X from %s",
                           manifest.name.c_str(), manifest.version,
                           fileLabel.c_str());

                // Register expected test names with the aggregator so it
                // can track PENDING (registered but not yet reported).
                if (isTestSuiteOnly) {
                    for (const auto& tn : manifest.testNames) {
                        kcdx::test::RegisterExpectedTestName(tn, manifest.name);
                    }
                    KCDX_DEV("TEST", "REGISTERED",
                        kcdx::dev::KV("plugin", manifest.name),
                        kcdx::dev::KV("expected_names",
                            (unsigned long long)manifest.testNames.size()));
                }

                kcdx::plugins::g_manifests.push_back(std::move(manifest));
            } else {
                // "no [plugin] table" is fine — file is config-only.
                // Other errors warrant a log line.
                if (mErr != "no [plugin] table") {
                    log::WarnF("%s: %s (plugin not registered)",
                               fileLabel.c_str(), mErr.c_str());
                }
            }
        }

        // [[patch]] array
        if (auto* arr = doc.get("patch"); arr && arr->is_array()) {
            for (const auto& elem : *arr->as_array()) {
                if (!elem.is_table()) continue;
                kcdx::patch::PatchEntry entry;
                std::string err;
                if (ParseOnePatch(*elem.as_table(), fileLabel, entry, err)) {
                    entry.source = source;
                    entry.pluginAuthor = pluginAuthor;
                    entry.pluginName   = pluginName;
                    log::InfoF("Loaded patch '%s' (priority %d, source=%s) from %s",
                               entry.name.c_str(), entry.priority,
                               source == Source::Engine ? "engine" : "user",
                               fileLabel.c_str());
                    kcdx::patch::g_patches.push_back(std::move(entry));
                } else {
                    log::ErrorF("Skipped patch in %s: %s", fileLabel.c_str(), err.c_str());
                }
            }
        }

        // [[hook]] array (Phase 4b.1)
        if (auto* arr = doc.get("hook"); arr && arr->is_array()) {
            for (const auto& elem : *arr->as_array()) {
                if (!elem.is_table()) continue;
                kcdx::hook_engine::HookEntry entry;
                std::string err;
                if (ParseOneHook(*elem.as_table(), fileLabel, entry, err)) {
                    entry.source = source;
                    entry.pluginName = pluginName;
                    log::InfoF("Loaded hook '%s' (priority %d, source=%s) from %s",
                               entry.name.c_str(), entry.priority,
                               source == Source::Engine ? "engine" : "user",
                               fileLabel.c_str());
                    kcdx::hook_engine::g_hooks.push_back(std::move(entry));
                } else {
                    log::ErrorF("Skipped hook in %s: %s", fileLabel.c_str(), err.c_str());
                }
            }
        }

        // [[mid_hook]] array (Phase 5g)
        if (auto* arr = doc.get("mid_hook"); arr && arr->is_array()) {
            for (const auto& elem : *arr->as_array()) {
                if (!elem.is_table()) continue;
                kcdx::hook_engine::MidHookEntry entry;
                std::string err;
                if (ParseOneMidHook(*elem.as_table(), fileLabel, entry, err)) {
                    entry.source = source;
                    entry.pluginName = pluginName;
                    log::InfoF("Loaded mid_hook '%s' (priority %d, source=%s) from %s",
                               entry.name.c_str(), entry.priority,
                               source == Source::Engine ? "engine" : "user",
                               fileLabel.c_str());
                    kcdx::hook_engine::g_mid_hooks.push_back(std::move(entry));
                } else {
                    log::ErrorF("Skipped mid_hook in %s: %s", fileLabel.c_str(), err.c_str());
                }
            }
        }

        // [[trampoline]] array (Phase 4b.2)
        if (auto* arr = doc.get("trampoline"); arr && arr->is_array()) {
            for (const auto& elem : *arr->as_array()) {
                if (!elem.is_table()) continue;
                kcdx::trampoline_engine::TrampolineEntry entry;
                std::string err;
                if (ParseOneTrampoline(*elem.as_table(), fileLabel, entry, err)) {
                    entry.source = source;
                    entry.pluginAuthor = pluginAuthor;
                    entry.pluginName   = pluginName;
                    log::InfoF("Loaded trampoline '%s' (priority %d, source=%s) from %s",
                               entry.name.c_str(), entry.priority,
                               source == Source::Engine ? "engine" : "user",
                               fileLabel.c_str());
                    kcdx::trampoline_engine::g_trampolines.push_back(std::move(entry));
                } else {
                    log::ErrorF("Skipped trampoline in %s: %s", fileLabel.c_str(), err.c_str());
                }
            }
        }

        // [[scan]] — diagnostic-only locator-resolve entries (Phase 5h
        // closeout, design-gaps gap #7). No write, no apply; logs the
        // match count and surrounding bytes for new modders learning
        // the locator pipeline.
        if (auto* arr = doc.get("scan"); arr && arr->is_array()) {
            for (const auto& elem : *arr->as_array()) {
                if (!elem.is_table()) continue;
                kcdx::scan_engine::ScanEntry entry;
                std::string err;
                if (ParseOneScan(*elem.as_table(), fileLabel, entry, err)) {
                    entry.pluginName = pluginName;
                    log::InfoF("Loaded scan '%s' from %s",
                               entry.name.c_str(), fileLabel.c_str());
                    kcdx::scan_engine::g_scans.push_back(std::move(entry));
                } else {
                    log::ErrorF("Skipped scan in %s: %s", fileLabel.c_str(), err.c_str());
                }
            }
        }
    } catch (const toml::parse_error& e) {
        log::ErrorF("TOML parse error in %s: %s", fileLabel.c_str(), e.what());
    } catch (const std::exception& e) {
        log::ErrorF("Unexpected error reading %s: %s", fileLabel.c_str(), e.what());
    }
}

// Discovery rules (also apply to plugin_loader.cpp's DLL walk):
//
//   1. kcdx.toml is the plugin marker. Every plugin folder has one.
//   2. kcdx.toml at depth 0 (directly in the root) is rejected with
//      a helpful warn line — the root is for plugin folders, not
//      data. Applies to both `plugins/` (user) and
//      `kcdx-engine/builtin/` (engine).
//   3. From any folder, recurse infinitely until a kcdx.toml is found.
//      That folder IS the plugin; we do NOT descend into it further.
//      Subfolders of a plugin folder are the plugin's private space
//      for sub-DLLs, data files, configs, etc — kcdx ignores them.
//   4. Hidden folders/files (starting with `.`) are skipped, both
//      roots.
//
// To disable a plugin without uninstalling it, set `enabled = false`
// for it in `kcdx-engine/load_order.toml`. The launcher (when shipped)
// does this via a per-row toggle. Folder-rename `.disabled` was
// removed once `enabled` was wired into every apply path — having
// two ways to express the same intent was a foot-gun.
//
// Every walker decision (examine, skip, accept, recurse) emits a
// DEBUG/TRACE line under the DISCOVERY category so the funnel is
// visible when something silently disappears. Always-on funnel
// summary is emitted by LoadAllConfigs after the walk completes.
//
// folders/files counters track plugin folders (with TOML) and TOML
// files loaded. `examined` counts every directory_iterator entry
// visited (folders + files combined). `source` flows down into
// LoadOneFile so every entry it appends is stamped with the
// discovery root that produced it.
void WalkForTomls(const fs::path& dir, int depth, Source source,
                  size_t& folders, size_t& files, size_t& examined) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto p = entry.path();
        const auto name = p.filename().wstring();
        ++examined;

        const bool isDir = entry.is_directory(ec);
        LOG_TRACE_KV("DISCOVERY", "examine",
            KV("path",  WideToUtf8(p.wstring())),
            KV("depth", depth),
            KV::BareStr("kind", isDir ? "dir" : "file"));

        // Skip hidden folders/files (`.git`, `.DS_Store`, etc.).
        if (!name.empty() && name[0] == L'.') {
            LOG_DEBUG_KV("DISCOVERY", "skip",
                KV("path",   WideToUtf8(p.wstring())),
                KV("reason", "hidden"));
            continue;
        }

        if (isDir) {
            // Does this folder claim itself as a plugin?
            fs::path candidate = p / "kcdx.toml";
            if (fs::exists(candidate, ec)) {
                ++folders;
                ++files;
                LOG_DEBUG_KV("DISCOVERY", "accept",
                    KV("folder", WideToUtf8(p.wstring())),
                    KV("toml",   WideToUtf8(candidate.wstring())),
                    KV::BareStr("source",
                        source == Source::Engine ? "engine" : "user"));
                LoadOneFile(candidate, source);
                // Plugin claimed. Do NOT descend.
            } else {
                // Container folder. Recurse.
                LOG_DEBUG_KV("DISCOVERY", "recurse",
                    KV("path",  WideToUtf8(p.wstring())),
                    KV("depth", depth + 1));
                WalkForTomls(p, depth + 1, source, folders, files, examined);
            }
        } else if (entry.is_regular_file(ec)) {
            // Files at depth 0 (directly in plugins/) are misplaced.
            // Files at depth > 0 inside a container folder (no
            // kcdx.toml in the same folder) are silently ignored —
            // they're junk or pre-plugin debris.
            if (depth == 0) {
                const auto ext = p.extension().wstring();
                if (_wcsicmp(name.c_str(), L"kcdx.toml") == 0) {
                    LOG_WARN("DISCOVERY",
                        "Ignoring '%s': the plugins/ directory "
                        "should host folders of installed plugins, "
                        "not the plugin data itself. Move this file "
                        "into a subfolder.",
                        WideToUtf8(p.wstring()).c_str());
                } else if (_wcsicmp(ext.c_str(), L".dll") == 0
                           && _wcsicmp(name.c_str(), L"kcdx.asi") != 0) {
                    LOG_WARN("DISCOVERY",
                        "Ignoring '%s': the plugins/ directory "
                        "should host folders of installed plugins, "
                        "not the plugin data itself. Move this DLL "
                        "into a subfolder with a kcdx.toml.",
                        WideToUtf8(p.wstring()).c_str());
                } else {
                    LOG_TRACE_KV("DISCOVERY", "skip",
                        KV("path",   WideToUtf8(p.wstring())),
                        KV("reason", "depth-0-non-plugin-file"));
                }
            } else {
                LOG_TRACE_KV("DISCOVERY", "skip",
                    KV("path",   WideToUtf8(p.wstring())),
                    KV("reason", "loose-file-in-container"));
            }
        }
    }
}

}  // namespace

void LoadAllConfigs(const std::wstring& pluginsDir) {
    // Idempotence guard. LoadAllConfigs is called from two places when
    // the before_game-zone path is enabled:
    //   1. kcdx.asi DllMain (synchronously, so before_game patches can
    //      resolve their TOMLs before WHGame.dll's DllMain).
    //   2. The worker thread (the historical site).
    // We want the worker-thread call to be a no-op if DllMain already
    // parsed everything — otherwise we'd double-populate g_patches /
    // g_hooks / g_mid_hooks / g_trampolines.
    static std::atomic<bool> sAlreadyLoaded{false};
    bool expected = false;
    if (!sAlreadyLoaded.compare_exchange_strong(expected, true)) {
        log::Info("config::LoadAllConfigs: skipping (already loaded earlier "
                  "this session)");
        return;
    }

    // Engine config first. Settles dev_mode + dry_run + dev_categories
    // before any plugin TOML is parsed (so test_suite_only gating sees
    // the final IsEnabled() value). Production users don't ship this
    // file; dev mode stays off.
    LoadEngineConfig(kcdx::paths::EngineDataDirPath() / L"engine.toml");

    // Walk discovery roots in order:
    //   1) kcdx-engine/builtin/  — first-party engine fixes
    //   2) plugins/              — third-party user plugins
    //
    // Walking the engine root first is mostly cosmetic — the
    // final sort comparator uses Source as its primary key, so
    // engine-fix entries land at the front of each global vector
    // regardless of walk order. We still walk Engine first for
    // log readability (engine-fix DISCOVERY lines appear before
    // user-plugin ones).
    size_t engFolders = 0, engFiles = 0, engExamined = 0;
    fs::path engineBuiltin = kcdx::paths::EngineDataDirPath() / L"builtin";
    if (fs::exists(engineBuiltin) && fs::is_directory(engineBuiltin)) {
        WalkForTomls(engineBuiltin, /*depth=*/0, Source::Engine,
                     engFolders, engFiles, engExamined);
    }

    size_t usrFolders = 0, usrFiles = 0, usrExamined = 0;
    fs::path userRoot(pluginsDir);
    if (fs::exists(userRoot) && fs::is_directory(userRoot)) {
        WalkForTomls(userRoot, /*depth=*/0, Source::User,
                     usrFolders, usrFiles, usrExamined);
    } else {
        log::WarnF("plugins dir not found: %s", WideToUtf8(pluginsDir).c_str());
    }

    LOG_INFO("DISCOVERY",
        "walk complete: engine=%zu/%zu user=%zu/%zu (accepted/examined)",
        engFolders, engExamined, usrFolders, usrExamined);

    // Load-order resolution. Discovery is done; entry vectors are
    // populated with their pluginName stamps. Read the user's
    // load_order.toml (if any), then Resolve() to compute each
    // plugin's effective (zone, priority, enabled) — applying
    // capability gating where the user's request is impossible
    // given the plugin's declared entries.
    kcdx::load_order::Read(
        kcdx::paths::EngineDataDirPath() / L"load_order.toml");
    kcdx::load_order::Resolve();

    // Sort key:
    //   (Zone asc, plugin_effective_priority asc, plugin_name asc,
    //    Source asc, entry.priority asc, entry.name asc)
    //
    // Zone first ensures every before_game-zoned entry lands before
    // every after_game-zoned entry — that's the immovable game.exe
    // sentinel materialized in the sort.
    //
    // Plugin priority next gives the user / author control over
    // intra-zone position. plugin_name breaks priority ties for
    // determinism.
    //
    // Source (Engine < User) preserves the "engine fixes lead within
    // their zone" invariant for ties at the plugin level (e.g. two
    // engine-fix plugins at the same priority).
    //
    // Entry priority + name break ties for plugins that ship multiple
    // entries.
    auto pluginKey = [](const std::string& pluginName, int entrySource,
                        int entryPriority, const std::string& entryName) {
        const auto& eff = kcdx::load_order::Of(pluginName);
        return std::tuple<int, int, std::string, int, int, std::string>{
            static_cast<int>(eff.zone),
            eff.priority,
            pluginName,
            entrySource,
            entryPriority,
            entryName
        };
    };
    auto patchLess = [&](const kcdx::patch::PatchEntry& a,
                         const kcdx::patch::PatchEntry& b) {
        return pluginKey(a.pluginName, static_cast<int>(a.source),
                         a.priority, a.name) <
               pluginKey(b.pluginName, static_cast<int>(b.source),
                         b.priority, b.name);
    };
    auto hookLess = [&](const kcdx::hook_engine::HookEntry& a,
                        const kcdx::hook_engine::HookEntry& b) {
        return pluginKey(a.pluginName, static_cast<int>(a.source),
                         a.priority, a.name) <
               pluginKey(b.pluginName, static_cast<int>(b.source),
                         b.priority, b.name);
    };
    auto midHookLess = [&](const kcdx::hook_engine::MidHookEntry& a,
                           const kcdx::hook_engine::MidHookEntry& b) {
        return pluginKey(a.pluginName, static_cast<int>(a.source),
                         a.priority, a.name) <
               pluginKey(b.pluginName, static_cast<int>(b.source),
                         b.priority, b.name);
    };
    auto trampLess = [&](const kcdx::trampoline_engine::TrampolineEntry& a,
                         const kcdx::trampoline_engine::TrampolineEntry& b) {
        return pluginKey(a.pluginName, static_cast<int>(a.source),
                         a.priority, a.name) <
               pluginKey(b.pluginName, static_cast<int>(b.source),
                         b.priority, b.name);
    };
    std::sort(kcdx::patch::g_patches.begin(), kcdx::patch::g_patches.end(),
              patchLess);
    std::sort(kcdx::hook_engine::g_hooks.begin(), kcdx::hook_engine::g_hooks.end(),
              hookLess);
    std::sort(kcdx::hook_engine::g_mid_hooks.begin(),
              kcdx::hook_engine::g_mid_hooks.end(),
              midHookLess);
    std::sort(kcdx::trampoline_engine::g_trampolines.begin(),
              kcdx::trampoline_engine::g_trampolines.end(),
              trampLess);

    size_t totalFolders = engFolders + usrFolders;
    size_t totalFiles   = engFiles   + usrFiles;
    log::InfoF("Discovered %zu patch(es), %zu hook(s), %zu trampoline(s) from "
               "%zu config file(s) across %zu plugin folder(s) "
               "(%zu engine + %zu user)",
               kcdx::patch::g_patches.size(),
               kcdx::hook_engine::g_hooks.size(),
               kcdx::trampoline_engine::g_trampolines.size(),
               totalFiles, totalFolders,
               engFolders, usrFolders);

    // Production-quiet: tell the user about gated-off test plugins even
    // when dev mode is off. No-op when count == 0.
    kcdx::test::EmitGatedOffSummary();
}

}  // namespace kcdx::config
