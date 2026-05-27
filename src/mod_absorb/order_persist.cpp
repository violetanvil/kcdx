#include "order_persist.h"

#include <algorithm>
#include <fstream>
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "pak_mod_registry.h"
#include "../load_order.h"
#include "../log.h"
#include "../paths.h"
#include "../plugin_loader.h"

namespace fs = std::filesystem;

namespace kcdx::mod_absorb::order_persist {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// Read a whole file into a string. Returns false (and leaves `out` empty) if
// the file is absent or unreadable — an absent load_order.toml / mod_order.txt
// is a normal first-run state, NOT an error (the caller treats it as an empty
// document). A read FAILURE distinct from absence is surfaced via the `existed`
// out-param so the caller can tell "absent" from "present-but-unreadable".
bool ReadFileText(const fs::path& path, std::string& out, bool& existed) {
    out.clear();
    std::error_code ec;
    existed = fs::exists(path, ec);
    if (!existed) return true;  // absent -> empty document, not a failure.
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;       // present but unreadable -> failure.
    std::ostringstream ss;
    ss << f.rdbuf();
    out = ss.str();
    return true;
}

// Write `text` to `path` (binary, truncate). Returns false on failure.
bool WriteFileText(const fs::path& path, const std::string& text) {
    std::ofstream f(path, std::ios::binary | std::ios::trunc);
    if (!f) return false;
    f << text;
    return f.good();
}

// Trim ASCII whitespace from both ends.
std::string Trim(const std::string& s) {
    size_t b = 0, e = s.size();
    while (b < e && (s[b] == ' ' || s[b] == '\t' || s[b] == '\r' || s[b] == '\n')) ++b;
    while (e > b && (s[e - 1] == ' ' || s[e - 1] == '\t' || s[e - 1] == '\r' ||
                     s[e - 1] == '\n')) --e;
    return s.substr(b, e - b);
}

// The load-order sort key for a load-order name: (zone, priority, orderIndex,
// name). MIRRORS the enabled_list_builder + config.cpp pluginKey tuple ordering
// — the SAME key the load_order surface resolves, so the persisted pak-mod
// order matches the order kcdx actually loads in.
std::tuple<int, int, int, std::string> SortKey(const std::string& loadOrderName) {
    const auto& eff = kcdx::load_order::Of(loadOrderName);
    return std::tuple<int, int, int, std::string>{
        static_cast<int>(eff.zone),
        eff.priority,
        eff.orderIndex,
        loadOrderName,
    };
}

}  // namespace

// ----------------------------------------------------------------------------
// Pure string serializers.
// ----------------------------------------------------------------------------

std::vector<std::string> ExistingRowNames(const std::string& text) {
    // Bare line scan: a `name = "x"` line (inside a [[plugin]] table) yields
    // "x". This is intentionally NOT a full TOML parse — toml++ would drop the
    // comments MergeLoadOrderToml must preserve, and all we need here is which
    // names already have a row. A `name` key only appears in a [[plugin]] row in
    // this file's schema (the allowlist is name/zone/priority/enabled), so a
    // bare scan is sufficient + comment-tolerant. A '#'-commented line is
    // skipped so a commented-out row does not count as present.
    std::vector<std::string> names;
    std::istringstream in(text);
    std::string line;
    while (std::getline(in, line)) {
        const std::string t = Trim(line);
        if (t.empty() || t[0] == '#') continue;
        // Match: name = "value"  (whitespace-flexible around '=').
        // Find "name", then '=', then the first double-quoted token.
        if (t.rfind("name", 0) != 0) continue;            // must start with "name"
        size_t eq = t.find('=', 4);
        if (eq == std::string::npos) continue;
        // Between "name" and '=' only whitespace is allowed (so "named" or
        // "namespace" do not match).
        const std::string between = Trim(t.substr(4, eq - 4));
        if (!between.empty()) continue;
        size_t q1 = t.find('"', eq + 1);
        if (q1 == std::string::npos) continue;
        size_t q2 = t.find('"', q1 + 1);
        if (q2 == std::string::npos) continue;
        names.push_back(t.substr(q1 + 1, q2 - q1 - 1));
    }
    return names;
}

std::string MergeLoadOrderToml(const std::string& existingText,
                               const std::vector<ResolvedRow>& rows,
                               std::vector<std::string>* addedOut) {
    if (addedOut) addedOut->clear();

    // Which names already have a row? Existing rows (plugin OR pak-mod,
    // including hand-edits + rows for not-currently-discovered mods) are
    // PRESERVED VERBATIM — the existing text is the merge base, untouched.
    const std::vector<std::string> existing = ExistingRowNames(existingText);
    std::unordered_set<std::string> have(existing.begin(), existing.end());

    // Start from the existing text VERBATIM. Ensure it ends with a newline
    // before we append (so an appended row never glues onto the last line).
    std::string out = existingText;
    bool needsSeparatorBlank = false;
    if (!out.empty()) {
        if (out.back() != '\n') out.push_back('\n');
        needsSeparatorBlank = true;  // one blank line before the first append.
    }

    bool firstAppend = true;
    for (const ResolvedRow& r : rows) {
        if (r.loadOrderName.empty()) continue;     // anonymous / unnamed — no row.
        if (have.count(r.loadOrderName)) continue;  // already present — ADD-ONLY.

        // One blank line before the very first appended row (so it is visually
        // separated from whatever the existing content ended with).
        if (firstAppend && needsSeparatorBlank) {
            out.push_back('\n');
        }
        firstAppend = false;

        out += "[[plugin]]\n";
        out += "name    = \"";
        out += r.loadOrderName;
        out += "\"";
        // Surface the human mod name as a TRAILING comment on a pak-mod row.
        // Read() rejects an unknown KEY (display_name), so the human name can
        // only ride along as a comment — which Read() tolerates (it skips '#'
        // lines and ignores end-of-line comment text on a value line via the
        // TOML parser). Strip any newline from the human name so it stays a
        // single trailing comment.
        if (r.isPakMod && !r.humanName.empty()) {
            std::string human = r.humanName;
            for (char& c : human) {
                if (c == '\n' || c == '\r') c = ' ';
            }
            out += "  # ";
            out += human;
        }
        out += "\n";

        have.insert(r.loadOrderName);
        if (addedOut) addedOut->push_back(r.loadOrderName);
    }

    return out;
}

std::string SerializeModOrderText(const std::vector<std::string>& order) {
    std::string out =
        "# managed by kcdx — order reflects kcdx's resolved load order.\n"
        "# One mod id per line; file order is the load/mount order. kcdx\n"
        "# rewrites this only when the resolved order changes.\n";
    for (const std::string& modId : order) {
        out += modId;
        out += "\n";
    }
    return out;
}

bool ModOrderDiffers(const std::string& existingText,
                     const std::vector<std::string>& order) {
    // Compare only the surviving modid SEQUENCE (comments + blanks ignored), so
    // re-adding the "# managed by kcdx" header to a comment-less vanilla file
    // is not, by itself, counted as a change. ParseModOrderText yields a
    // modid->index map; rebuild the sequence from it ordered by index.
    const std::unordered_map<std::string, int> diskMap =
        ParseModOrderText(existingText);
    std::vector<std::string> diskSeq(diskMap.size());
    bool indexOk = true;
    for (const auto& kv : diskMap) {
        if (kv.second >= 0 && kv.second < static_cast<int>(diskSeq.size())) {
            diskSeq[kv.second] = kv.first;
        } else {
            indexOk = false;  // a malformed index — force "differs", rewrite.
        }
    }
    if (!indexOk) return true;
    return diskSeq != order;
}

// ----------------------------------------------------------------------------
// File writers — read, merge/serialize, WRITE-IF-CHANGED, fail loud.
// ----------------------------------------------------------------------------

void WriteLoadOrderToml(const fs::path& loadOrderPath) {
    // Build the resolved-row set from BOTH SUPERSET sources. load_order.toml is
    // KEYED (not ordered), so resolved order is irrelevant here — only the set
    // of names + each pak mod's human name matter.
    std::vector<ResolvedRow> rows;
    rows.reserve(kcdx::plugins::g_manifests.size() + Registry().size());
    for (const auto& m : kcdx::plugins::g_manifests) {
        if (m.name.empty()) continue;  // anonymous — no toggleable row.
        ResolvedRow r;
        r.loadOrderName = m.name;
        r.isPakMod      = false;
        rows.push_back(std::move(r));
    }
    for (const PakMod& mod : Registry()) {
        ResolvedRow r;
        r.loadOrderName = LoadOrderNameFor(mod.modId);
        r.humanName     = mod.manifest.name;  // the surfaced human name.
        r.isPakMod      = true;
        rows.push_back(std::move(r));
    }

    std::string existing;
    bool existed = false;
    if (!ReadFileText(loadOrderPath, existing, existed)) {
        // Present but unreadable — fail LOUD. We will not blindly overwrite a
        // file we could not read (that would clobber the user's hand-edits with
        // an add-only merge that has no base).
        LOG_ERROR_KV(kCat, "load_order_persist_read_fail",
                     kcdx::log::KV("file", loadOrderPath.string()),
                     kcdx::log::KV("consequence",
                        "load_order.toml exists but could not be read — NOT "
                        "rewritten; a newly-discovered pak mod will have no "
                        "editable row this run"));
        return;
    }

    std::vector<std::string> added;
    const std::string merged = MergeLoadOrderToml(existing, rows, &added);

    if (merged == existing) {
        LOG_DEBUG_KV(kCat, "load_order_persist_skip",
                     kcdx::log::KV("file", loadOrderPath.string()),
                     kcdx::log::KV("reason", "unchanged — no newly-discovered "
                                             "mod to add a row for"));
        return;
    }

    if (!WriteFileText(loadOrderPath, merged)) {
        LOG_ERROR_KV(kCat, "load_order_persist_write_fail",
                     kcdx::log::KV("file", loadOrderPath.string()),
                     kcdx::log::KV("rows_to_add", (uint64_t)added.size()),
                     kcdx::log::KV("consequence",
                        "could not write load_order.toml — the new mod row(s) "
                        "were NOT persisted; the mod is loaded this run but has "
                        "no editable load-order row"));
        return;
    }

    LOG_INFO_KV(kCat, "load_order_persist_write",
                kcdx::log::KV("file", loadOrderPath.string()),
                kcdx::log::KV("rows_added", (uint64_t)added.size()));
    for (const std::string& name : added) {
        LOG_DEBUG_KV(kCat, "load_order_persist_added_row",
                     kcdx::log::KV("name", name));
    }
}

void WriteModOrderTxt(const fs::path& modsDir) {
    // The resolved pak-mod order = every registered pak mod, sorted by the SAME
    // load-order key kcdx loads in. mod_order.txt is the ORDER seed (NOT the
    // enable list — kcdx owns enable/disable via load_order.toml), so a disabled
    // mod keeps its position here; include ALL registered pak mods. Emit bare
    // modids (the vanilla format), not the "mods." load-order key.
    std::vector<std::string> sortedNames;       // "mods.<modid>" keys, for the sort
    sortedNames.reserve(Registry().size());
    for (const PakMod& mod : Registry()) {
        sortedNames.push_back(LoadOrderNameFor(mod.modId));
    }
    std::sort(sortedNames.begin(), sortedNames.end(),
              [](const std::string& a, const std::string& b) {
                  return SortKey(a) < SortKey(b);
              });
    // Map each sorted "mods.<modid>" key back to its bare modid.
    std::vector<std::string> order;
    order.reserve(sortedNames.size());
    for (const std::string& key : sortedNames) {
        // LoadOrderNameFor is "mods." + modId; strip the prefix to recover the
        // bare modid the vanilla file holds.
        constexpr const char* kPrefix = "mods.";
        constexpr size_t kPrefixLen = 5;
        if (key.rfind(kPrefix, 0) == 0) {
            order.push_back(key.substr(kPrefixLen));
        } else {
            order.push_back(key);  // defensive — should always have the prefix.
        }
    }

    const fs::path orderPath = modsDir / "mod_order.txt";

    // No pak mods discovered: do NOT create or rewrite mod_order.txt. Writing an
    // empty (header-only) file over a vanilla file the user has would be a
    // destructive no-content write; an absent file stays absent. This is a
    // genuine no-op, logged at DEBUG so it is visible.
    if (order.empty()) {
        LOG_DEBUG_KV(kCat, "mod_order_persist_skip",
                     kcdx::log::KV("file", orderPath.string()),
                     kcdx::log::KV("reason", "no pak mods discovered — nothing "
                                             "to persist (file left as-is)"));
        return;
    }

    std::string existing;
    bool existed = false;
    if (!ReadFileText(orderPath, existing, existed)) {
        // Present but unreadable. We can still WRITE the resolved order (the
        // write does not depend on the old content the way the toml merge does)
        // — but warn that we could not diff, so the rewrite is unconditional.
        LOG_WARN_KV(kCat, "mod_order_persist_read_fail",
                    kcdx::log::KV("file", orderPath.string()),
                    kcdx::log::KV("consequence",
                       "mod_order.txt exists but could not be read — rewriting "
                       "with kcdx's resolved order unconditionally (could not "
                       "compare to detect an actual change)"));
        existing.clear();  // force a write below (ModOrderDiffers on "" is true).
    }

    if (!ModOrderDiffers(existing, order)) {
        LOG_DEBUG_KV(kCat, "mod_order_persist_skip",
                     kcdx::log::KV("file", orderPath.string()),
                     kcdx::log::KV("reason", "unchanged — resolved order matches "
                                             "the on-disk sequence"));
        return;
    }

    const std::string text = SerializeModOrderText(order);
    if (!WriteFileText(orderPath, text)) {
        LOG_ERROR_KV(kCat, "mod_order_persist_write_fail",
                     kcdx::log::KV("file", orderPath.string()),
                     kcdx::log::KV("mod_count", (uint64_t)order.size()),
                     kcdx::log::KV("consequence",
                        "could not write mod_order.txt — kcdx's resolved pak-mod "
                        "order was NOT persisted to the vanilla file; the next "
                        "vanilla-side read sees the OLD order"));
        return;
    }

    LOG_INFO_KV(kCat, "mod_order_persist_write",
                kcdx::log::KV("file", orderPath.string()),
                kcdx::log::KV("mod_count", (uint64_t)order.size()));
}

void PersistResolvedOrder() {
    // Runs at boot, ctx-B, AFTER load_order::Resolve() + the pak-mod version
    // gate produced the final resolved state. Touches the filesystem (game-root
    // mods/ + kcdx-engine/load_order.toml) — fine at this point (not under the
    // loader lock). Independent of whether the SELECT-detour takeover fires.
    WriteLoadOrderToml(kcdx::paths::EngineDataDirPath() / L"load_order.toml");
    WriteModOrderTxt(kcdx::paths::GameRootDirPath() / L"mods");
}

}  // namespace kcdx::mod_absorb::order_persist
