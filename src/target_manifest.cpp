#include "target_manifest.h"

#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>

// toml++ is header-only. Mirror config.cpp's exception mode + include.
#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include "address_library.h"
#include "log.h"

namespace fs = std::filesystem;

namespace kcdx::config {

// Bring KV into scope for the LOG_*_KV macro call sites below (the macros
// expand to `{ KV(...), ... }` initializer lists needing an unqualified KV).
using KV = ::kcdx::log::KV;

namespace {

// Local mirrors of config.cpp's table accessors — kept private to this TU so
// the sidecar parser stays self-contained (the originals live in config.cpp's
// anonymous namespace and aren't exported). Same semantics.
std::string OptString(const toml::table& tbl, std::string_view key,
                      std::string_view fallback = "") {
    if (auto* v = tbl.get(key); v && v->is_string()) {
        return std::string(*v->value<std::string>());
    }
    return std::string(fallback);
}

// One [[target]] row → a RegisterAuthorTarget call. Returns true when the row
// registered; on a shape error fills `err` (the caller logs + skips). Mirrors
// ParseOnePatch's locator-exclusivity check.
bool RegisterOneTarget(const toml::table& t,
                       const std::string& pluginName,
                       std::string& err) {
    std::string bareName = OptString(t, "name");
    if (bareName.empty()) {
        err = "missing required field 'name' (the bare target name; the engine "
              "derives the <pluginname> prefix from [plugin].name)";
        return false;
    }

    // Locator: exactly one of 'pattern' / 'rva' / 'address_id' / 'target_symbol'.
    // pattern + target_symbol are strings; rva + address_id are integers.
    std::string patternStr      = OptString(t, "pattern");
    std::string targetSymbolStr = OptString(t, "target_symbol");

    bool hasRva       = false;
    bool hasAddressId = false;
    uint64_t rvaVal       = 0;
    uint64_t addressIdVal = 0;
    if (auto* v = t.get("rva"); v && v->is_integer()) {
        rvaVal = static_cast<uint64_t>(*v->value<int64_t>());
        hasRva = true;
    }
    if (auto* v = t.get("address_id"); v && v->is_integer()) {
        addressIdVal = static_cast<uint64_t>(*v->value<int64_t>());
        hasAddressId = true;
    }

    int locatorCount = (!patternStr.empty() ? 1 : 0)
                     + (!targetSymbolStr.empty() ? 1 : 0)
                     + (hasRva ? 1 : 0)
                     + (hasAddressId ? 1 : 0);
    if (locatorCount == 0) {
        err = "missing locator: declare exactly one of 'pattern', 'rva', "
              "'address_id', or 'target_symbol'";
        return false;
    }
    if (locatorCount > 1) {
        err = "conflicting locators: declare exactly one of 'pattern', 'rva', "
              "'address_id', or 'target_symbol'";
        return false;
    }

    address_library::AuthorLocatorKind kind;
    std::string locatorStr;
    uint64_t    locatorNum = 0;
    if (!patternStr.empty()) {
        kind = address_library::AuthorLocatorKind::Pattern;
        locatorStr = patternStr;
    } else if (!targetSymbolStr.empty()) {
        kind = address_library::AuthorLocatorKind::TargetSymbol;
        locatorStr = targetSymbolStr;
    } else if (hasRva) {
        kind = address_library::AuthorLocatorKind::Rva;
        locatorNum = rvaVal;
    } else {
        kind = address_library::AuthorLocatorKind::AddressId;
        locatorNum = addressIdVal;
    }

    // signature: optional structured ABI in the kcdx.hook DSL. A pattern/rva
    // target SHOULD carry one (no name to carry the ABI — the disassembler
    // test), but registration allows "" since we never invent an ABI (AP2).
    std::string signature = OptString(t, "signature");

    std::string regErr;
    if (!address_library::RegisterAuthorTarget(
            pluginName.c_str(), bareName.c_str(), kind,
            locatorStr.c_str(), locatorNum, signature.c_str(), regErr)) {
        err = regErr;  // ValidatePluginName / bare-name charset rejection.
        return false;
    }
    return true;
}

}  // namespace

void LoadTargetsFor(const std::string& pluginFolder,
                    const std::string& pluginName) {
    fs::path path = fs::path(pluginFolder) / "targets.toml";

    // Optional sidecar: absent → no-op (most plugins ship none).
    std::error_code ec;
    if (!fs::exists(path, ec)) return;

    std::string fileLabel = path.string();
    try {
        std::ifstream in(path);
        if (!in) {
            LOG_ERROR("TARGETS", "Failed to open %s", fileLabel.c_str());
            return;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        toml::table doc = toml::parse(ss.str(), path.string());

        auto* arr = doc.get("target");
        if (!arr || !arr->is_array()) {
            LOG_WARN("TARGETS",
                     "%s: no [[target]] tables; ignoring file",
                     fileLabel.c_str());
            return;
        }

        size_t registered = 0;
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_table()) continue;
            std::string err;
            if (RegisterOneTarget(*elem.as_table(), pluginName, err)) {
                ++registered;
            } else {
                // Teaching line: plugin + the bad field/rule. One bad row does
                // not kill the others.
                LOG_WARN_KV("TARGETS", "rejected",
                    KV("plugin", pluginName),
                    KV("file",   fileLabel),
                    KV("reason", err));
            }
        }

        LOG_INFO_KV("TARGETS", "loaded",
            KV("plugin", pluginName),
            KV("file",   fileLabel),
            KV("registered", (unsigned long long)registered));
    } catch (const toml::parse_error& e) {
        LOG_ERROR("TARGETS", "TOML parse error in %s: %s",
                  fileLabel.c_str(), e.what());
    } catch (const std::exception& e) {
        LOG_ERROR("TARGETS", "Unexpected error reading %s: %s",
                  fileLabel.c_str(), e.what());
    }
}

}  // namespace kcdx::config
