#include "target_manifest.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

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
// Recognized keys in a targets.toml [[target]] row, paired with the TOML kind
// each must carry when present. A row may declare exactly one locator
// (pattern/target_symbol are strings, rva/address_id are integers); `name` is
// required; `signature` is the optional ABI. Validation here mirrors the
// manifest sweep (config.cpp): a present-but-wrong-type key or an unknown key
// is a REJECT (was a silent drop — pattern/target_symbol/signature fell
// through OptString to "" on the wrong type, and rva/address_id fell through
// the is_integer() guard, so a mistyped locator silently registered no target
// or the wrong one; AP14). The reject is surfaced via the same `err` channel
// RegisterOneTarget already uses — the caller logs the rejected row and skips
// it (one bad row does not kill the others).
enum class TKind { String, Integer };

bool TKindMatches(const toml::node& n, TKind k) {
    return k == TKind::String ? n.is_string() : n.is_integer();
}

const char* TKindName(TKind k) {
    return k == TKind::String ? "string" : "integer";
}

const char* NodeKindName(const toml::node& n) {
    if (n.is_string())  return "string";
    if (n.is_integer()) return "integer";
    if (n.is_floating_point()) return "float";
    if (n.is_boolean()) return "boolean";
    if (n.is_array())   return "array";
    if (n.is_table())   return "table";
    return "unknown";
}

bool ValidateTargetRowKeys(const toml::table& t, std::string& err) {
    struct Spec { std::string_view key; TKind kind; };
    static constexpr std::array<Spec, 6> kAllow = {{
        {"name",          TKind::String},
        {"pattern",       TKind::String},
        {"target_symbol", TKind::String},
        {"rva",           TKind::Integer},
        {"address_id",    TKind::Integer},
        {"signature",     TKind::String},
    }};
    for (const auto& [keyNode, valNode] : t) {
        std::string_view k = keyNode.str();
        const Spec* spec = nullptr;
        for (const auto& s : kAllow) {
            if (s.key == k) { spec = &s; break; }
        }
        if (!spec) {
            err = "unknown key '" + std::string(k) + "' in [[target]] row "
                  "(recognized: name, pattern, target_symbol, rva, "
                  "address_id, signature)";
            return false;
        }
        if (!TKindMatches(valNode, spec->kind)) {
            err = "key '" + std::string(k) + "' has wrong type (is " +
                  std::string(NodeKindName(valNode)) + ", expected " +
                  TKindName(spec->kind) + ")";
            return false;
        }
    }
    return true;
}

bool RegisterOneTarget(const toml::table& t,
                       const std::string& pluginAuthor,
                       const std::string& pluginName,
                       std::string& err) {
    if (!ValidateTargetRowKeys(t, err)) {
        return false;
    }

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
    // Step 4 of the 2-dot namespace refactor: the caller threads the
    // manifest's (author, plugin) pair through; the row now registers
    // under the full 2-dot key when both are non-empty. When
    // [plugin].author is still empty (the corpus state before step 6)
    // the row registers under (empty author, pluginName, bareName) and
    // the resolver walks the legacy 1-dot tier — identical observable
    // behavior to today's bare-name resolution.
    if (!address_library::RegisterAuthorTarget(
            pluginAuthor.c_str(), pluginName.c_str(), bareName.c_str(), kind,
            locatorStr.c_str(), locatorNum, signature.c_str(), regErr)) {
        err = regErr;  // ValidatePluginName / bare-name charset rejection.
        return false;
    }
    return true;
}

}  // namespace

void LoadTargetsFor(const std::string& pluginFolder,
                    const std::string& pluginAuthor,
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
        size_t idx = 0;
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_table()) {
                // Loud reject (was a silent `continue` that dropped a malformed
                // [[target]] entry with no signal — AP14). Same teaching shape
                // as a shape-error row below.
                LOG_WARN_KV("TARGETS", "rejected",
                    KV("plugin", pluginName),
                    KV("file",   fileLabel),
                    KV("reason", "[[target]] entry at index " +
                                 std::to_string(idx) + " is not a table"));
                ++idx;
                continue;
            }
            std::string err;
            if (RegisterOneTarget(*elem.as_table(), pluginAuthor,
                                  pluginName, err)) {
                ++registered;
            } else {
                // Teaching line: plugin + the bad field/rule. One bad row does
                // not kill the others.
                LOG_WARN_KV("TARGETS", "rejected",
                    KV("plugin", pluginName),
                    KV("file",   fileLabel),
                    KV("reason", err));
            }
            ++idx;
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
