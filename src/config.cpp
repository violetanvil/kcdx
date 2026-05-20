#include "config.h"

#include <windows.h>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// toml++ is header-only.
#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include "hook_engine.h"
#include "dev.h"
#include "log.h"
#include "patch_engine.h"
#include "paths.h"
#include "plugin_loader.h"
#include "scan_engine.h"
#include "test.h"
#include "trampoline_engine.h"

namespace fs = std::filesystem;
namespace kcdx::config {

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
    out.displayName  = OptString(t, "display_name", out.name);
    out.author       = OptString(t, "author");
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
        out.dllEntrypointRel = OptString(et, "dll");
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

void LoadOneFile(const fs::path& path) {
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
        // a plugin in g_plugins / g_manifests).
        {
            kcdx::plugins::PluginManifest manifest;
            std::string mErr;
            if (ParsePluginManifest(doc, path, manifest, mErr)) {
                manifest.testSuiteOnly = isTestSuiteOnly;
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
                    log::InfoF("Loaded patch '%s' (priority %d) from %s",
                               entry.name.c_str(), entry.priority, fileLabel.c_str());
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
                    log::InfoF("Loaded hook '%s' (priority %d) from %s",
                               entry.name.c_str(), entry.priority, fileLabel.c_str());
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
                    log::InfoF("Loaded mid_hook '%s' (priority %d) from %s",
                               entry.name.c_str(), entry.priority, fileLabel.c_str());
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
                    log::InfoF("Loaded trampoline '%s' (priority %d) from %s",
                               entry.name.c_str(), entry.priority, fileLabel.c_str());
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
//   2. kcdx.toml at depth 0 (directly in plugins/) is rejected with a
//      helpful warn line — plugins/ is for plugin folders, not data.
//   3. From any folder, recurse infinitely until a kcdx.toml is found.
//      That folder IS the plugin; we do NOT descend into it further.
//      Subfolders of a plugin folder are the plugin's private space
//      for sub-DLLs, data files, configs, etc — kcdx ignores them.
//   4. Hidden folders (starting with `.`) and *.disabled* folders
//      are skipped silently.
//
// folders/files counters track plugin folders (with TOML) and TOML
// files loaded, respectively.
void WalkForTomls(const fs::path& dir, int depth,
                  size_t& folders, size_t& files) {
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(dir, ec)) {
        const auto p = entry.path();
        const auto name = p.filename().wstring();

        // Skip hidden / disabled folders + files. A name is "disabled"
        // only when `.disabled` is its final suffix (e.g.
        // `kcdx.toml.disabled` or a folder explicitly suffixed
        // `something.disabled`). A `.disabled` substring elsewhere in
        // the name does NOT skip — the previous substring rule would
        // match any name containing `.disabled` anywhere, including
        // benign user folder names that happened to contain that
        // literal in the middle.
        if (!name.empty() && name[0] == L'.') continue;
        {
            const std::wstring kSuffix = L".disabled";
            if (name.size() >= kSuffix.size()
                && name.compare(name.size() - kSuffix.size(),
                                kSuffix.size(), kSuffix) == 0) {
                continue;
            }
        }

        if (entry.is_directory(ec)) {
            // Does this folder claim itself as a plugin?
            fs::path candidate = p / "kcdx.toml";
            if (fs::exists(candidate, ec)) {
                ++folders;
                ++files;
                LoadOneFile(candidate);
                // Plugin claimed. Do NOT descend.
            } else {
                // Container folder. Recurse.
                WalkForTomls(p, depth + 1, folders, files);
            }
        } else if (entry.is_regular_file(ec)) {
            // Files at depth 0 (directly in plugins/) are misplaced.
            // Files at depth > 0 inside a container folder (no kcdx.toml
            // in the same folder) are silently ignored — they're junk or
            // pre-plugin debris.
            if (depth == 0) {
                const auto ext = p.extension().wstring();
                if (_wcsicmp(name.c_str(), L"kcdx.toml") == 0) {
                    log::WarnF("Ignoring '%s': the plugins/ directory "
                               "should host folders of installed plugins, "
                               "not the plugin data itself. Move this file "
                               "into a subfolder.",
                               WideToUtf8(p.wstring()).c_str());
                } else if (_wcsicmp(ext.c_str(), L".dll") == 0
                           && _wcsicmp(name.c_str(), L"kcdx.asi") != 0) {
                    log::WarnF("Ignoring '%s': the plugins/ directory "
                               "should host folders of installed plugins, "
                               "not the plugin data itself. Move this DLL "
                               "into a subfolder with a kcdx.toml.",
                               WideToUtf8(p.wstring()).c_str());
                }
            }
        }
    }
}

}  // namespace

void LoadAllConfigs(const std::wstring& pluginsDir) {
    fs::path root(pluginsDir);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        log::WarnF("plugins dir not found: %s", WideToUtf8(pluginsDir).c_str());
        return;
    }

    // Engine config first. Settles dev_mode + dry_run + dev_categories
    // before any plugin TOML is parsed (so test_suite_only gating sees
    // the final IsEnabled() value). Production users don't ship this
    // file; dev mode stays off.
    LoadEngineConfig(kcdx::paths::EngineDataDirPath() / L"engine.toml");

    size_t folders = 0, files = 0;
    WalkForTomls(root, /*depth=*/0, folders, files);

    // Stable sort by (priority asc, name asc).
    std::sort(kcdx::patch::g_patches.begin(), kcdx::patch::g_patches.end(),
              [](const kcdx::patch::PatchEntry& a, const kcdx::patch::PatchEntry& b) {
                  if (a.priority != b.priority) return a.priority < b.priority;
                  return a.name < b.name;
              });
    std::sort(kcdx::hook_engine::g_hooks.begin(), kcdx::hook_engine::g_hooks.end(),
              [](const kcdx::hook_engine::HookEntry& a, const kcdx::hook_engine::HookEntry& b) {
                  if (a.priority != b.priority) return a.priority < b.priority;
                  return a.name < b.name;
              });
    std::sort(kcdx::trampoline_engine::g_trampolines.begin(),
              kcdx::trampoline_engine::g_trampolines.end(),
              [](const kcdx::trampoline_engine::TrampolineEntry& a,
                 const kcdx::trampoline_engine::TrampolineEntry& b) {
                  if (a.priority != b.priority) return a.priority < b.priority;
                  return a.name < b.name;
              });

    log::InfoF("Discovered %zu patch(es), %zu hook(s), %zu trampoline(s) from "
               "%zu config file(s) across %zu plugin folder(s)",
               kcdx::patch::g_patches.size(),
               kcdx::hook_engine::g_hooks.size(),
               kcdx::trampoline_engine::g_trampolines.size(),
               files, folders);

    // Production-quiet: tell the user about gated-off test plugins even
    // when dev mode is off. No-op when count == 0.
    kcdx::test::EmitGatedOffSummary();
}

}  // namespace kcdx::config
