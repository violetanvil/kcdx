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

    // Locator: either 'pattern' (the v1 AOB path) or 'target_symbol' (the
    // cross-plugin path that resolves against the global symbol table).
    // Exactly one of the two must be set.
    std::string patternStr      = OptString(t, "pattern");
    std::string targetSymbolStr = OptString(t, "target_symbol");
    if (patternStr.empty() && targetSymbolStr.empty()) {
        err = "missing locator: declare either 'pattern' or 'target_symbol'";
        return false;
    }
    if (!patternStr.empty() && !targetSymbolStr.empty()) {
        err = "conflicting locators: declare only one of 'pattern' or 'target_symbol'";
        return false;
    }
    if (!patternStr.empty()) {
        try {
            out.pattern = ParsePattern(patternStr);
        } catch (const std::exception& e) {
            err = std::string("parse error in 'pattern': ") + e.what();
            return false;
        }
    } else {
        out.targetSymbol = targetSymbolStr;
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
            // Dev mode: any-true wins, most-permissive caps win. We call
            // SetCapBytes/SetMaxFiles BEFORE SetEnabled so the open
            // happens with the right caps.
            if (OptBool(tbl, "dev_mode", false)) {
                int cap_mb    = OptInt(tbl, "dev_log_cap_mb",   50);
                int max_files = OptInt(tbl, "dev_log_max_files", 20);
                size_t cap_bytes = (size_t)cap_mb * 1024ull * 1024ull;
                kcdx::dev::SetCapBytes(cap_bytes);
                kcdx::dev::SetMaxFiles(max_files);
                kcdx::dev::SetEnabled(true);
                log::InfoF("dev_mode enabled by %s (cap=%d MB, max_files=%d)",
                           fileLabel.c_str(), cap_mb, max_files);
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

        // Skip hidden / disabled folders + files.
        if (!name.empty() && name[0] == L'.') continue;
        if (name.find(L".disabled") != std::wstring::npos) continue;

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
}

}  // namespace kcdx::config
