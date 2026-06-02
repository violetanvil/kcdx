#include "config.h"

#include <windows.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <sstream>
#include <string>
#include <string_view>

#include "address_library.h"  // ValidatePluginName (hard-rejects illegal [plugin].name)

// toml++ is header-only.
#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include "dev.h"
#include "load_order.h"
#include "log.h"
#include "mod_absorb/pak_mod_registry.h"
#include "patch_engine.h"
#include "paths.h"
#include "plugin_loader.h"
#include "target_manifest.h"
#include "test.h"
#include "trampoline_engine.h"
#include "zone_gate.h"

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

// Best-effort read of the "<author>.<plugin>" 2-dot identity key from a doc,
// WITHOUT validation — used only to NAME a parse-reject record so the reject
// is queryable via kcdx.plugin.is_rejected("author.plugin"). Returns the
// composed key when BOTH [plugin].author and [plugin].name are present,
// non-empty strings; returns "" otherwise (the reject is on the identity
// itself, or the identity isn't parsable — the folder-path key stands as the
// always-present internal record). This is a SEPARATE read from
// ParsePluginManifest's validated reads: the four cap-49 reject fixtures
// carry a VALID author+name but reject on a DIFFERENT key/table/type, so the
// strict reject fires BEFORE ParsePluginManifest reaches out.author/out.name
// — this best-effort read recovers the identity for the name key regardless.
std::string BestEffortAuthorPluginKey(const toml::table& doc) {
    auto* pluginNode = doc.get("plugin");
    if (!pluginNode || !pluginNode->is_table()) return {};
    const auto& t = *pluginNode->as_table();
    auto* a = t.get("author");
    auto* n = t.get("name");
    if (!a || !a->is_string() || !n || !n->is_string()) return {};
    std::string author = std::string(*a->value<std::string>());
    std::string name   = std::string(*n->value<std::string>());
    if (author.empty() || name.empty()) return {};
    return author + "." + name;
}

// ===========================================================================
// STRICT manifest validation (fail loud — never silently drop author input)
//
// The TOML manifest readers historically dropped unrecognized, misplaced, or
// wrong-typed keys SILENTLY: a wrong-type value made Opt* fall through to its
// fallback; an unknown key / unknown table was never iterated; a misplaced
// engine-level [kcdx] key only WARNed. Each is a silent-ignore — a
// user's authored intent vanishes with no trace (the 0xC8-bug class: an
// `enabled=` in the wrong file was simply gone).
//
// User-locked posture: STRICT ERROR + REJECT. Every wrong-type / unknown /
// misplaced key ends the parse with `err` set; the caller already rejects the
// plugin on a false return (LoadOneFile → ParsePluginManifest false → plugin
// not registered → does not load). REUSE that existing reject channel — no
// new gate.
//
// The two validators below are allowlist-driven (the recognized-key sets are
// declared as static literals next to ParsePluginManifest, not scattered) and
// are called by the manifest parsers AFTER the known keys are read.
// ===========================================================================

// TOML scalar/array categories the schema cares about. (Tables/arrays are
// validated structurally where they're consumed; these cover the leaf keys.)
enum class TomlKind { String, Integer, Boolean, Array };

const char* TomlKindName(TomlKind k) {
    switch (k) {
        case TomlKind::String:  return "string";
        case TomlKind::Integer: return "integer";
        case TomlKind::Boolean: return "boolean";
        case TomlKind::Array:   return "array";
    }
    return "?";
}

// One recognized key: its name + the TOML kind it must be when present.
struct KeySpec {
    std::string_view key;
    TomlKind         kind;
};

bool NodeMatchesKind(const toml::node& n, TomlKind k) {
    switch (k) {
        case TomlKind::String:  return n.is_string();
        case TomlKind::Integer: return n.is_integer();
        case TomlKind::Boolean: return n.is_boolean();
        case TomlKind::Array:   return n.is_array();
    }
    return false;
}

const char* NodeKindName(const toml::node& n) {
    if (n.is_string())            return "string";
    if (n.is_integer())           return "integer";
    if (n.is_floating_point())    return "float";
    if (n.is_boolean())           return "boolean";
    if (n.is_array())             return "array";
    if (n.is_table())             return "table";
    if (n.is_date() || n.is_time() || n.is_date_time()) return "datetime";
    return "unknown";
}

// Strip leading/trailing ASCII whitespace from a string (used to normalize
// `supports` version-pattern entries so " 1.5* " compares as "1.5*").
std::string TrimWs(std::string s) {
    const size_t b = s.find_first_not_of(" \t\r\n");
    if (b == std::string::npos) return {};
    const size_t e = s.find_last_not_of(" \t\r\n");
    return s.substr(b, e - b + 1);
}

// Validate ONE table against its recognized-key allowlist. Two REJECT classes,
// both setting `err` and returning false (so the caller rejects the plugin):
//   (1) a recognized key present with the WRONG TYPE  (#4)  — the key IS
//       present, just mistyped; absent keeps the fallback (handled by Opt*).
//   (2) a key NOT in the allowlist                    (#5)  — unknown key in a
//       known table; names the key + table so the author can fix it.
// `tableLabel` is the author-facing table name for the error ("[plugin]",
// "[entrypoints]", "[[plugin.dependencies]]", "[load_order]", engine "[kcdx]").
bool ValidateTableKeys(const toml::table& tbl,
                       std::string_view tableLabel,
                       std::initializer_list<KeySpec> allow,
                       std::string& err) {
    for (const auto& [keyNode, valNode] : tbl) {
        std::string_view k = keyNode.str();
        const KeySpec* spec = nullptr;
        for (const auto& s : allow) {
            if (s.key == k) { spec = &s; break; }
        }
        if (!spec) {
            err = std::string(tableLabel) + " unknown key '" + std::string(k) +
                  "' (not a recognized manifest key; remove it or correct the "
                  "spelling)";
            return false;
        }
        if (!NodeMatchesKind(valNode, spec->kind)) {
            err = std::string(tableLabel) + " key '" + std::string(k) +
                  "' has wrong type (is " + NodeKindName(valNode) +
                  ", expected " + TomlKindName(spec->kind) + ")";
            return false;
        }
    }
    return true;
}

// The ENGINE-LEVEL [kcdx] keys valid ONLY in engine.toml (LoadEngineConfig),
// NEVER in a PLUGIN's [kcdx] (where only `test_suite_only` is valid). A
// misplaced engine key in a plugin manifest is a REJECT (was a WARN — flipped
// per the user-locked strict posture: a setting in the wrong file silently
// vanishing is the 0xC8-bug class — a write that misses its target but reports OK).
//
// EXACTLY THREE keys — LoadEngineConfig reads each one. `dev_log_cap_mb` +
// `dev_log_max_files` were DROPPED (Batch B Decision 2): they were allowlisted
// but LoadEngineConfig reads NEITHER (log retention is fixed at
// kLogRetainCount), so a validate-OK-but-does-nothing knob is a
// silent-success (the author sets it, it validates, it has no effect). They
// are NOT wired up (rejected option); if they appear in engine.toml they get a
// "has no effect" WARN there (see LoadEngineConfig), NOT a reject (engine.toml
// is not a plugin) and NOT silent acceptance.
constexpr std::array<std::string_view, 3> kEngineOnlyKcdxKeys = {
    "dev_mode", "dry_run", "dev_categories",
};

// The two RETIRED engine [kcdx] keys (Batch B Decision 2): no longer
// allowlisted, no longer wired. If present in engine.toml, LoadEngineConfig
// WARNs ("no effect; log retention is fixed") rather than rejecting the config
// — distinguishing a known-dead knob (warn, proceed) from a genuine unknown
// key (Error, abort). A genuinely-unknown key in engine.toml still aborts.
constexpr std::array<std::string_view, 2> kRetiredEngineKcdxKeys = {
    "dev_log_cap_mb", "dev_log_max_files",
};

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
    // ---- Recognized-key allowlists (declared next to the parser, not
    // scattered as literals at each check site). Each entry
    // pairs the key with the TOML kind it must carry when present. Verified
    // against every shipped test-plugin manifest + the builtin: none carry an
    // unknown key, so STRICT validation false-rejects no current plugin.
    //
    // Cross-checked against the AS-BUILT reads below (the manifest schema
    // documents the same set): description/url/support_email/version/kcdx_min_version are
    // all genuinely read here (OptString at the lines below) — none is a
    // phantom; none the parser reads is omitted.
    static constexpr std::initializer_list<KeySpec> kPluginKeys = {
        {"name",                     TomlKind::String},
        {"display_name",             TomlKind::String},
        {"author",                   TomlKind::String},
        {"description",              TomlKind::String},
        {"url",                      TomlKind::String},
        {"support_email",            TomlKind::String},
        {"version",                  TomlKind::String},
        {"kcdx_min_version",         TomlKind::String},
        {"log_level",                TomlKind::String},
        {"on_changed_function",      TomlKind::String},
        {"supports",                 TomlKind::Array},
        {"dependencies",             TomlKind::Array},
        {"test_names",               TomlKind::Array},
    };
    static constexpr std::initializer_list<KeySpec> kDependencyKeys = {
        {"name",        TomlKind::String},
        {"min_version", TomlKind::String},
        {"optional",    TomlKind::Boolean},
    };
    static constexpr std::initializer_list<KeySpec> kLoadOrderKeys = {
        {"zone",     TomlKind::String},
        {"priority", TomlKind::Integer},
    };
    // ---- Top-level TABLE allowlist (#6): a plugin kcdx.toml has exactly four
    // valid top-level tables. A stray legacy behavior table
    // (one of the removed TOML behavior primitives) — previously
    // silently unparsed — is now a REJECT naming the table. The [kcdx] table is
    // read by LoadOneFile (not here), but it IS a valid top-level table, so it
    // must be in this allowlist or it would self-reject.
    for (const auto& [keyNode, valNode] : doc) {
        std::string_view tbl = keyNode.str();
        if (tbl != "kcdx" && tbl != "plugin" && tbl != "entrypoints" &&
            tbl != "load_order") {
            err = "unknown top-level table '[" + std::string(tbl) +
                  "]' (valid top-level tables: [kcdx], [plugin], "
                  "[entrypoints], [load_order]; legacy behavior tables "
                  "(the previous TOML schemas) were "
                  "removed — behavior ships in plugin.lua / a DLL)";
            return false;
        }
    }

    auto* pluginTbl = doc.get("plugin");
    if (!pluginTbl || !pluginTbl->is_table()) {
        err = "no [plugin] table";
        return false;
    }
    const auto& t = *pluginTbl->as_table();

    // STRICT [plugin] validation (#4 wrong-type + #5 unknown-key): reject a
    // present-but-wrong-type recognized key or any key not in kPluginKeys.
    // Runs BEFORE the field reads so a mistyped key fails loud here rather than
    // silently falling through Opt* to a fallback.
    if (!ValidateTableKeys(t, "[plugin]", kPluginKeys, err)) return false;

    out.name = OptString(t, "name");
    if (out.name.empty()) {
        err = "[plugin] missing required field 'name'";
        return false;
    }
    // Enforce the [plugin].name shape at discovery — the namespace model
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
    // <author>.<plugin>.<bare> namespace prefix. It is
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

    // Load-order author hints — the per-plugin [load_order] TABLE (zone +
    // priority). Both keys are optional; an absent [load_order] table defaults
    // both. See docs/load-order.md for the full model — zones partition plugins
    // into before_game vs after_game (the game.exe sentinel divides them);
    // priority orders plugins within their zone.
    //
    //   [load_order]
    //     zone = "before_game" | "after_game" | ""
    //       ""  → engine derives from capabilities at load time
    //             (engine builtins default to before_game; user plugins
    //             default to after_game when their entries are flexible).
    //
    //     priority = 0..100  (default 50)
    //       0 = earliest in zone, 100 = latest. Sparse range gives users /
    //       authors room to insert "definitely before X" without renumbering.
    //
    // This is the per-plugin manifest table, parsed here from the plugin's own
    // kcdx.toml doc. It is DISTINCT from the engine-wide override file
    // kcdx-engine/load_order.toml, whose top-level [[plugin]] rows are parsed
    // by a separate parser (load_order.cpp::Read). Different files, different
    // parsers — no collision.
    //
    // HARD rename (zone-rework): the legacy [plugin] keys
    // default_position / default_priority are NO LONGER read. A plugin still
    // carrying them is parsed as if it set neither (the keys are silently
    // ignored, consistent with the prerelease fix-forward no-WARN stance).
    {
        const toml::table* lo = nullptr;
        if (auto* loNode = doc.get("load_order"); loNode && loNode->is_table()) {
            lo = loNode->as_table();
        }
        // STRICT [load_order] validation (#4 + #5): unknown key or wrong-typed
        // zone/priority is a REJECT (a mistyped priority="high" previously fell
        // through OptInt to the 50 default, silently discarding the author's
        // intent).
        if (lo && !ValidateTableKeys(*lo, "[load_order]", kLoadOrderKeys, err)) {
            return false;
        }
        // Empty/absent [load_order]: zone derives, priority 50 — same defaults
        // as before the rename.
        std::string zone = lo ? OptString(*lo, "zone") : std::string();
        if (!zone.empty() && zone != "before_game" && zone != "after_game") {
            err = "[load_order] zone: unknown value '" + zone + "' "
                  "(expected before_game|after_game)";
            return false;
        }
        out.defaultPosition = zone;

        int prio = lo ? OptInt(*lo, "priority", 50) : 50;
        if (prio < 0 || prio > 100) {
            err = "[load_order] priority: out of range (" +
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

    // on_changed_function — the per-plugin survival-check posture. Decides what
    // the apply pass does when a function this plugin targets has changed in the
    // running game binary (the per-version survival check finds the on-disk bytes
    // no longer match the verified content_hash).
    //
    //   "warn_and_try"  → proceed with the binding, emit a warning (DEFAULT).
    //   "refuse_entry"  → skip the affected binding with a teaching error; other
    //                     bindings in the same plugin still apply.
    //
    // Absent → default WarnAndTry. An unknown value is a HARD manifest rejection
    // (fail loud — a silent default-to-warn would discard the author's intent,
    // e.g. a misspelled "refuse" silently weakening to warn-and-proceed).
    {
        std::string oc = OptString(t, "on_changed_function", "warn_and_try");
        if      (oc == "warn_and_try")
            out.onChangedFunction =
                kcdx::plugins::PluginManifest::OnChangedFunction::WarnAndTry;
        else if (oc == "refuse_entry")
            out.onChangedFunction =
                kcdx::plugins::PluginManifest::OnChangedFunction::RefuseEntry;
        else {
            err = "[plugin] on_changed_function: unknown value '" + oc + "' "
                  "(expected warn_and_try|refuse_entry)";
            return false;
        }
    }

    // supports = [ "1.5*", ... ]
    // The UNIFIED <supports> game-version model (shared with pak mods'
    // mod.manifest <supports> — docs/mod-loader-absorb.md "Version gate
    // UNIFICATION"). Each element is a RAW version-pattern string (a trailing
    // '*' is a PREFIX wildcard; no '*' = exact match) matched at load time
    // against g_runtimeGameVersionString by
    // version_compat::DecideGameVersionCompatString — NOT parsed to a packed
    // integer here, since prefix wildcards aren't a packed build number.
    // Strings are trimmed of surrounding whitespace.
    // Absent / empty array → empty vector → "any version" (version-independent
    // by absence).
    // STRICT (#17): a wrong-typed element is a REJECT naming the array + index
    // + actual type (a silent drop would let a mistyped pattern vanish, so the
    // plugin loaded against a game version it never declared compatibility with).
    if (auto* arr = t.get("supports"); arr && arr->is_array()) {
        size_t idx = 0;
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_string()) {
                err = "[plugin] supports[" + std::to_string(idx) +
                      "]: wrong type (is " + NodeKindName(elem) +
                      ", expected string)";
                return false;
            }
            std::string s = TrimWs(std::string(*elem.value<std::string>()));
            out.supports.push_back(std::move(s));
            ++idx;
        }
    }

    // [[plugin.dependencies]] array
    // STRICT (#17 + #5): a non-table element is a REJECT naming the index +
    // type (was `continue`); each dependency table is validated against
    // kDependencyKeys (unknown key / wrong type → REJECT).
    if (auto* arr = t.get("dependencies"); arr && arr->is_array()) {
        size_t idx = 0;
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_table()) {
                err = "[plugin] dependencies[" + std::to_string(idx) +
                      "]: wrong type (is " + NodeKindName(elem) +
                      ", expected table — a [[plugin.dependencies]] entry)";
                return false;
            }
            const auto& dt = *elem.as_table();
            if (!ValidateTableKeys(dt, "[[plugin.dependencies]]",
                                   kDependencyKeys, err)) {
                return false;
            }
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
            ++idx;
        }
    }

    // [entrypoints] section
    if (auto* entryTbl = doc.get("entrypoints"); entryTbl && entryTbl->is_table()) {
        const auto& et = *entryTbl->as_table();

        // STRICT [entrypoints] validation (#5 + #4): reject any unknown key,
        // and any recognized key with the wrong shape. `dll` is string-only;
        // `lua` / `lua_after` accept string OR array-of-string (the
        // single-kind ValidateTableKeys can't express the union, so the check
        // is bespoke here). A mistyped entrypoint silently no-loading the
        // plugin's code is exactly the silent-ignore this guards against.
        for (const auto& [keyNode, valNode] : et) {
            std::string_view k = keyNode.str();
            if (k == "dll") {
                if (!valNode.is_string()) {
                    err = "[entrypoints] key 'dll' has wrong type (is " +
                          std::string(NodeKindName(valNode)) +
                          ", expected string)";
                    return false;
                }
            } else if (k == "lua" || k == "lua_after") {
                if (!valNode.is_string() && !valNode.is_array()) {
                    err = "[entrypoints] key '" + std::string(k) +
                          "' has wrong type (is " +
                          std::string(NodeKindName(valNode)) +
                          ", expected string or array of strings)";
                    return false;
                }
            } else {
                err = "[entrypoints] unknown key '" + std::string(k) +
                      "' (recognized: dll, lua, lua_after)";
                return false;
            }
        }

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
                size_t idx = 0;
                for (const auto& elem : *luaNode->as_array()) {
                    // STRICT (#17): a non-string element is a REJECT naming the
                    // index + type (was a silent `continue` that dropped a
                    // mistyped entrypoint path — the plugin's code never ran,
                    // no signal).
                    if (!elem.is_string()) {
                        err = "[entrypoints] lua[" + std::to_string(idx) +
                              "]: wrong type (is " + NodeKindName(elem) +
                              ", expected string)";
                        return false;
                    }
                    out.luaEntrypointsRel.push_back(
                        std::string(*elem.value<std::string>()));
                    ++idx;
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
                size_t idx = 0;
                for (const auto& elem : *luaAfterNode->as_array()) {
                    // STRICT (#17): non-string element → REJECT (was silent
                    // `continue`).
                    if (!elem.is_string()) {
                        err = "[entrypoints] lua_after[" + std::to_string(idx) +
                              "]: wrong type (is " + NodeKindName(elem) +
                              ", expected string)";
                        return false;
                    }
                    out.luaAfterEntrypointsRel.push_back(
                        std::string(*elem.value<std::string>()));
                    ++idx;
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
        size_t idx = 0;
        for (const auto& elem : *arr->as_array()) {
            // STRICT (#17): non-string element → REJECT naming index + type
            // (was a silent `continue` — a mistyped test name would silently
            // not register its PENDING row, hiding a never-reported test).
            if (!elem.is_string()) {
                err = "[plugin] test_names[" + std::to_string(idx) +
                      "]: wrong type (is " + NodeKindName(elem) +
                      ", expected string)";
                return false;
            }
            out.testNames.push_back(std::string(*elem.value<std::string>()));
            ++idx;
        }
    }

    out.tomlPath   = tomlPath;
    out.folderPath = tomlPath.parent_path();
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

        // STRICT engine-[kcdx] validation (#5 + #17): the THREE engine-only
        // keys are the ONLY valid keys here (test_suite_only is a PLUGIN key,
        // NOT valid in engine.toml). An unknown key → Error + abort the engine
        // config load (dev mode stays off — the production default, matching
        // the existing parse-error / open-failure paths above). A misplaced
        // engine key silently doing nothing is the silent-failure shape this
        // closes on the engine side too.
        //
        // The two RETIRED keys (dev_log_cap_mb / dev_log_max_files) are a
        // MIDDLE case: not allowlisted (LoadEngineConfig reads neither — log
        // retention is fixed at kLogRetainCount), but also not a typo. They
        // get a WARN naming the no-effect, then the load PROCEEDS (engine.toml
        // is not a plugin — a dead-but-recognized knob does not abort the
        // whole engine config the way a genuine unknown key does). This is the
        // honest signal: the author hears their setting has no
        // effect rather than it silently validating-and-doing-nothing.
        for (const auto& [keyNode, valNode] : tbl) {
            (void)valNode;
            std::string_view k = keyNode.str();
            bool known = false;
            for (std::string_view ek : kEngineOnlyKcdxKeys) {
                if (ek == k) { known = true; break; }
            }
            if (known) continue;

            bool retired = false;
            for (std::string_view rk : kRetiredEngineKcdxKeys) {
                if (rk == k) { retired = true; break; }
            }
            if (retired) {
                log::WarnF("%s: [kcdx] %s has no effect; log retention is "
                           "fixed (kLogRetainCount). This setting was retired "
                           "— remove it. Engine config still applied.",
                           fileLabel.c_str(), std::string(k).c_str());
                continue;
            }

            log::ErrorF("%s: [kcdx] unknown key '%s' (valid engine keys: "
                        "dev_mode, dry_run, dev_categories). Ignoring "
                        "engine config.",
                        fileLabel.c_str(), std::string(k).c_str());
            return;
        }

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
                size_t idx = 0;
                for (const auto& elem : *arr->as_array()) {
                    // STRICT (#17): a non-string element is an engine-config
                    // error — log Error + abort the engine config load (was a
                    // silent `continue` that dropped the bad element, so a
                    // typo'd category would silently widen the filter to
                    // include categories the author meant to exclude). Matches
                    // LoadEngineConfig's existing Error-and-return failure
                    // signalling.
                    if (!elem.is_string()) {
                        log::ErrorF("%s: [kcdx] dev_categories[%zu]: wrong type "
                                    "(is %s, expected string). Ignoring engine "
                                    "config.",
                                    fileLabel.c_str(), idx, NodeKindName(elem));
                        return;
                    }
                    cats.push_back(std::string(*elem.value<std::string>()));
                    ++idx;
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

        // Top-level [kcdx] section. The ONLY valid key in a PLUGIN's [kcdx] is
        // `test_suite_only`. The engine-level settings (dev_mode, dry_run,
        // dev_categories — see kEngineOnlyKcdxKeys) live ONLY in
        // <kcdx-engine>/engine.toml. (dev_log_cap_mb / dev_log_max_files were
        // dropped in Batch B Decision 2 — read by nothing; they hit the
        // unknown-key reject branch below if set in a plugin [kcdx].)
        //
        // STRICT posture (flipped from WARN — fail loud, never silent-drop):
        //   - a MISPLACED engine-level key in a plugin's [kcdx]  → REJECT
        //     (was WARN — a `dev_mode` here silently never took effect, the
        //     0xC8-bug class: a setting in the wrong file vanishing).
        //   - any OTHER unknown key in [kcdx]                    → REJECT.
        //   - a wrong-typed `test_suite_only`                    → REJECT
        //     (would have fallen through OptBool to false, silently disabling
        //     the suite-gate the author asked for).
        // A reject here logs Error and returns — the plugin is NOT registered
        // and NOT counted as gated-off (a malformed manifest is neither). This
        // runs BEFORE the gated-off production-quiet early-return so the
        // rejection is never swallowed by the suite gate.
        bool isTestSuiteOnly = false;
        if (auto* top = doc.get("kcdx"); top && top->is_table()) {
            const auto& tbl = *top->as_table();
            for (const auto& [keyNode, valNode] : tbl) {
                std::string_view k = keyNode.str();
                if (k == "test_suite_only") {
                    if (!valNode.is_boolean()) {
                        std::string reason =
                            "[kcdx] test_suite_only has wrong type (is " +
                            std::string(NodeKindName(valNode)) +
                            ", expected boolean)";
                        log::ErrorF("%s: %s; plugin rejected",
                                    fileLabel.c_str(), reason.c_str());
                        zone_gate::RecordParseReject(
                            path.parent_path().string(),
                            BestEffortAuthorPluginKey(doc), reason);
                        return;
                    }
                    continue;
                }
                bool engineOnly = false;
                for (std::string_view ek : kEngineOnlyKcdxKeys) {
                    if (ek == k) { engineOnly = true; break; }
                }
                std::string reason;
                if (engineOnly) {
                    reason = "[kcdx] " + std::string(k) +
                             " is an engine-level setting and cannot be set by "
                             "a plugin — it belongs in "
                             "<kcdx-engine>/engine.toml (move the key or "
                             "remove it)";
                    log::ErrorF("%s: %s. Plugin rejected.",
                                fileLabel.c_str(), reason.c_str());
                } else {
                    reason = "[kcdx] unknown key '" + std::string(k) +
                             "' (the only key valid in a plugin's [kcdx] is "
                             "test_suite_only)";
                    log::ErrorF("%s: %s. Plugin rejected.",
                                fileLabel.c_str(), reason.c_str());
                }
                zone_gate::RecordParseReject(
                    path.parent_path().string(),
                    BestEffortAuthorPluginKey(doc), reason);
                return;
            }
            isTestSuiteOnly = OptBool(tbl, "test_suite_only", false);
        }

        // test_suite_only + dev_mode OFF: production-quiet path.
        // We still want to COUNT the plugin so the user sees
        // "N test_suite_only plugin(s) gated off (dev mode disabled; ...)"
        // at boot. Bump the
        // counter and skip everything else (no legacy TOML behavior
        // entries register, plugin DLLs early-return at Plugin_Load).
        if (isTestSuiteOnly && !kcdx::dev::IsEnabled()) {
            kcdx::test::IncrementGatedOffCount();
            return;
        }

        // [plugin] + [entrypoints] sections — plugin identity. Optional;
        // a config-only kcdx.toml with no [plugin] table is still valid (it
        // just doesn't register a plugin in g_plugins / g_manifests). We
        // capture the plugin name + author here for manifest registration and
        // for the load-order sort to resolve the owning plugin's effective
        // zone + priority. (Behavior is no longer declared in TOML — the
        // legacy behavior-table parsers were removed; behavior ships in
        // plugin.lua / kcdxPlugin_Load.)
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
                // "no [plugin] table" is fine — file is config-only (NOT a
                // reject: the file legitimately declares no plugin). Every
                // OTHER mErr is a genuine manifest REJECT — the plugin will
                // not load. Severity matches consequence:
                // a manifest that stops a plugin loading is Error, not Warn (a
                // Warn here would scroll past the author who needs to fix it).
                // Record the reject so kcdx.plugin.is_rejected sees it: keyed by
                // folder path always + by "<author>.<plugin>" when a valid
                // identity was parsable (the four cap-49 reject classes —
                // unknown-key, wrong-type, stray-table, [load_order] errors —
                // carry a valid author+name and reject on a different key, so
                // they ARE name-queryable).
                if (mErr != "no [plugin] table") {
                    log::ErrorF("%s: %s (plugin rejected — not registered)",
                                fileLabel.c_str(), mErr.c_str());
                    zone_gate::RecordParseReject(
                        path.parent_path().string(),
                        BestEffortAuthorPluginKey(doc), mErr);
                }
            }
        }

        // Plugin BEHAVIOR (patches, hooks, mid-hooks, trampolines, scans)
        // no longer lives in TOML — it ships in code (kcdx.bytes / kcdx.hook
        // in plugin.lua, kcdxHookInterface in C++ plugin DLLs). The legacy
        // behavior tables were retired in an earlier version (every plugin
        // migrated off them, then their parsers were deleted). LoadOneFile is now
        // manifest-only: it parses [kcdx] + [plugin] + [entrypoints] above and
        // nothing else. A stray legacy behavior table in a kcdx.toml is
        // silently ignored (kcdx is prerelease — no external author tomls to
        // stay compatible with).
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
    //   1. kcdx.dll DllMain (synchronously, so before_game patches can
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

    // Pak-mod discovery (mod-loader absorb, step 3). A SEPARATE pass from the
    // plugin walker above (WalkForTomls is untouched): mod_absorb::Discover
    // owns the mod.manifest marker-file classification. It scans BOTH roots —
    // kcdx-plugins/ (a folder with kcdx.toml there is a kcdx plugin, already
    // claimed above, and Discover SKIPS it) and <game-root>/mods/ (vanilla pak
    // mods). A folder with mod.manifest + no kcdx.toml registers as a PakMod.
    // Discovery is UNCONDITIONAL of game version — the <supports> gate runs
    // later, once the runtime version string is known (mod_absorb::ApplyVersionGate,
    // from the worker thread after VersionDetected). The pak mods are folded
    // into the load_order model by Resolve() below.
    kcdx::mod_absorb::ClearRegistry();
    fs::path modsDir = kcdx::paths::GameRootDirPath() / L"mods";
    if (fs::exists(modsDir) && fs::is_directory(modsDir)) {
        kcdx::mod_absorb::Discover(modsDir, /*fromModsDir=*/true);
    } else {
        log::InfoF("mods/ not found at %s — no vanilla pak mods to absorb",
                   modsDir.string().c_str());
    }
    // A kcdx plugin works dropped in EITHER dir — scan kcdx-plugins/ for pak
    // mods too (a mod.manifest-only folder there is also a pak mod). kcdx.toml
    // folders there are SKIPPED by Discover (the plugin walker owns them).
    kcdx::mod_absorb::Discover(fs::path(pluginsDir), /*fromModsDir=*/false);

    // Steam Workshop walk — the THIRD pak-mod source kcdx absorbs (the original
    // game's SELECT call walks the same workshop content dir; kcdx-pak-mod-
    // registry covers what SELECT discovered so the init-cycle takeover can
    // skip SELECT entirely). paths::WorkshopContentDir() resolves the Steam
    // install dir from the registry and composes
    // <Steam>/steamapps/workshop/content/1771300/. An empty return = Steam not
    // installed OR no KCD2 Workshop subscriptions — DiscoverWorkshop handles
    // empty/absent paths as an info-log skip, never an error.
    {
        std::wstring workshopDirW = kcdx::paths::WorkshopContentDir();
        fs::path workshopDir(workshopDirW);
        kcdx::mod_absorb::DiscoverWorkshop(workshopDir);
    }

    // Populate each registered pak mod's mod_order.txt line index (the
    // vanilla baseline ordering seed, used as the secondary sort key in the
    // load_order fold). mod_order.txt lives in <game-root>/mods/.
    {
        auto orderMap = kcdx::mod_absorb::ReadModOrder(modsDir);
        size_t fromMods = 0, fromPlugins = 0, fromWorkshop = 0;
        for (auto& mod : kcdx::mod_absorb::Registry()) {
            if (auto it = orderMap.find(mod.modId); it != orderMap.end()) {
                mod.modOrderIndex = it->second;
            }
            if (mod.fromWorkshop)      ++fromWorkshop;
            else if (mod.fromModsDir)  ++fromMods;
            else                       ++fromPlugins;
        }
        log::InfoF("pak-mod discovery: %zu pak mod(s) — %zu from mods/, "
                   "%zu from kcdx-plugins/, %zu from Steam Workshop "
                   "(version gate runs later, once the runtime game version "
                   "is known)",
                   kcdx::mod_absorb::Registry().size(),
                   fromMods, fromPlugins, fromWorkshop);
    }

    // Load-order resolution. Discovery is done; entry vectors are
    // populated with their pluginName stamps. Read the user's
    // load_order.toml (if any), then Resolve() to compute each
    // plugin's effective (zone, priority, enabled) — applying
    // capability gating where the user's request is impossible
    // given the plugin's declared entries. Resolve() ALSO folds the pak-mod
    // registry into the same Effective map (keyed "mods.<modid>").
    kcdx::load_order::Read(
        kcdx::paths::EngineDataDirPath() / L"load_order.toml");
    kcdx::load_order::Resolve();

    // Capability/zone evaluation. Three reasons this runs here, and
    // ONLY here:
    //
    //   (a) load_order::Resolve has populated every plugin's resolved
    //       zone — zone_gate's per-plugin Check reads Of(name).zone, so
    //       Resolve must precede it.
    //   (b) No plugin-init path has run yet — neither C++ DLL
    //       Preload/Load nor Lua plugin.lua execution. A rejection
    //       therefore prevents any registration from happening; no
    //       half-loaded plugin state has to be torn down.
    //   (c) Flipping Effective.engineAccepted = false on a rejected
    //       plugin makes the existing IsPluginEnabled(name) predicate
    //       (already gated at all 5 plugin-init sites + the 2 runtime
    //       readers in ldr_notify and lua_registry) naturally skip the
    //       plugin everywhere — no new gate to thread through the
    //       engine.
    //
    // The init-site skip-logs consult zone_gate::RejectReason(name) to
    // distinguish "engine rejected this plugin" from "user disabled
    // this plugin" when emitting their skip lines.
    kcdx::zone_gate::EvaluateAllPlugins();

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
    // orderIndex sits between priority and plugin_name: it is INT_MAX for every
    // plugin (a no-op among them — they tie on it and break on name as before),
    // and a finite mod_order.txt line index only for folded pak-mod rows, so
    // the pak-mod block keeps its vanilla relative order. Plugin ordering is
    // provably unchanged.
    auto pluginKey = [](const std::string& pluginName, int entrySource,
                        int entryPriority, const std::string& entryName) {
        const auto& eff = kcdx::load_order::Of(pluginName);
        return std::tuple<int, int, int, std::string, int, int, std::string>{
            static_cast<int>(eff.zone),
            eff.priority,
            eff.orderIndex,
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
    auto trampLess = [&](const kcdx::trampoline_engine::TrampolineEntry& a,
                         const kcdx::trampoline_engine::TrampolineEntry& b) {
        return pluginKey(a.pluginName, static_cast<int>(a.source),
                         a.priority, a.name) <
               pluginKey(b.pluginName, static_cast<int>(b.source),
                         b.priority, b.name);
    };
    std::sort(kcdx::patch::g_patches.begin(), kcdx::patch::g_patches.end(),
              patchLess);
    std::sort(kcdx::trampoline_engine::g_trampolines.begin(),
              kcdx::trampoline_engine::g_trampolines.end(),
              trampLess);

    size_t totalFolders = engFolders + usrFolders;
    size_t totalFiles   = engFiles   + usrFiles;
    log::InfoF("Discovered %zu patch(es), %zu trampoline(s) from "
               "%zu config file(s) across %zu plugin folder(s) "
               "(%zu engine + %zu user)",
               kcdx::patch::g_patches.size(),
               kcdx::trampoline_engine::g_trampolines.size(),
               totalFiles, totalFolders,
               engFolders, usrFolders);

    // Production-quiet: tell the user about gated-off test plugins even
    // when dev mode is off. No-op when count == 0.
    kcdx::test::EmitGatedOffSummary();
}

}  // namespace kcdx::config
