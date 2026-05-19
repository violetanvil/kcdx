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
#include "log.h"
#include "patch_engine.h"

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

    std::string bytesStr = OptString(t, "bytes");
    if (bytesStr.empty()) {
        err = "missing required field 'bytes' (the detour body)";
        return false;
    }
    try {
        out.bytes = ParseBytes(bytesStr);
    } catch (const std::exception& e) {
        err = std::string("parse error in 'bytes': ") + e.what();
        return false;
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

        // Top-level [kcdx] section
        if (auto* top = doc.get("kcdx"); top && top->is_table()) {
            const auto& tbl = *top->as_table();
            if (OptBool(tbl, "dry_run", false)) {
                kcdx::patch::g_dryRun = true;
                log::InfoF("dry_run enabled by %s", fileLabel.c_str());
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
    } catch (const toml::parse_error& e) {
        log::ErrorF("TOML parse error in %s: %s", fileLabel.c_str(), e.what());
    } catch (const std::exception& e) {
        log::ErrorF("Unexpected error reading %s: %s", fileLabel.c_str(), e.what());
    }
}

}  // namespace

void LoadAllConfigs(const std::wstring& pluginsDir) {
    fs::path root(pluginsDir);
    if (!fs::exists(root) || !fs::is_directory(root)) {
        log::WarnF("plugins dir not found: %s", WideToUtf8(pluginsDir).c_str());
        return;
    }

    size_t folders = 0, files = 0;
    std::error_code ec;
    for (const auto& entry : fs::directory_iterator(root, ec)) {
        if (!entry.is_directory(ec)) continue;
        ++folders;
        fs::path candidate = entry.path() / "kcdx.toml";
        if (fs::exists(candidate, ec)) {
            LoadOneFile(candidate);
            ++files;
        }
    }

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

    log::InfoF("Discovered %zu patch(es) and %zu hook(s) from %zu config file(s) "
               "across %zu plugin folder(s)",
               kcdx::patch::g_patches.size(),
               kcdx::hook_engine::g_hooks.size(),
               files, folders);
}

}  // namespace kcdx::config
