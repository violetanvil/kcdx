#include "asset_sidecar.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <string>
#include <string_view>

// toml++ is header-only. Mirror config.cpp / target_manifest.cpp's exception
// mode + include.
#define TOML_EXCEPTIONS 1
#include "toml.hpp"

#include "asset_overlay.h"  // NormalizeVPath — the SINGLE key-normalization fold
                            // the overlay map + resolver share; reuse, never
                            // re-derive (asset_overlay.h INVARIANT).
#include "log.h"

namespace fs = std::filesystem;

namespace kcdx::asset_sidecar {

// Bring KV into scope for the LOG_*_KV macro call sites (the macros expand to
// `{ KV(...), ... }` initializer lists needing an unqualified KV — see
// logging.md: bare KV in the macro, qualified elsewhere).
using KV = ::kcdx::log::KV;

namespace {

// Stable log category for the sidecar parse + the teaching rejects (greppable
// in kcdx-dev.log). The agent reads these for cap-73's falsifiable rows.
constexpr const char* kCat = "ASSET_SIDECAR";

// The sidecar filename (see asset_sidecar.h for the placement-read choice).
constexpr const char* kSidecarName = "replaces.toml";

// --- toml accessors (local mirror of config.cpp's, kept private to this TU,
// same semantics — target_manifest.cpp does the same) -----------------------

std::string OptString(const toml::table& tbl, std::string_view key,
                      std::string_view fallback = "") {
    if (auto* v = tbl.get(key); v && v->is_string()) {
        return std::string(*v->value<std::string>());
    }
    return std::string(fallback);
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

// Every recognized key in an `[[asset]]` row is a string. A present-but-wrong-
// type key OR an unknown key is a REJECT (never a silent drop — AP14: a mistyped
// `replaces` would otherwise fall through OptString to "" and silently overlay
// nothing). Mirrors target_manifest.cpp's ValidateTargetRowKeys.
bool ValidateAssetRowKeys(const toml::table& t, std::string& err) {
    static constexpr std::array<std::string_view, 5> kAllow = {{
        "replaces", "replaces_plugin", "replaces_path", "file", "name",
    }};
    for (const auto& [keyNode, valNode] : t) {
        std::string_view k = keyNode.str();
        bool known = false;
        for (const auto& a : kAllow) {
            if (a == k) { known = true; break; }
        }
        if (!known) {
            err = "unknown key '" + std::string(k) + "' in [[asset]] row "
                  "(recognized: replaces, replaces_plugin, replaces_path, "
                  "file, name)";
            return false;
        }
        if (!valNode.is_string()) {
            err = "key '" + std::string(k) + "' has wrong type (is " +
                  std::string(NodeKindName(valNode)) + ", expected string)";
            return false;
        }
    }
    return true;
}

// A published-name target is a <author>.<plugin>.<bare> reference (≥ 2 dots, no
// path separator) — distinct from a vanilla vpath (a slashed path). The split
// mirrors the §4.2 / §6 resolvers: ONE string is a name OR a vanilla path; a
// slash means a path. A bare-but-unslashed token with < 2 dots is treated as a
// vanilla path (the engine's existence check is the arbiter — a non-path that
// is not a real vanilla file fails the missing-target teach).
bool LooksLikePublishedName(const std::string& s) {
    if (s.find('/') != std::string::npos ||
        s.find('\\') != std::string::npos) {
        return false;  // has a separator → a path, not a name
    }
    // a published name is <author>.<plugin>.<bare> → at least two dots
    size_t dots = 0;
    for (char c : s) if (c == '.') ++dots;
    return dots >= 2;
}

// Resolve which file in the plugin's assets/ tree a row speaks to.
//   - file-scope: the sidecar's own directory holds exactly the asset(s) it
//     declares; a row with no `file` key binds to the SINGLE sibling regular
//     file in the sidecar's directory (the §4.2 "needs no file key" case). If
//     that directory holds 0 or >1 sibling files, the row MUST name `file`.
//   - directory-scope: a row with `file = "<rel>"` binds to that path relative
//     to the sidecar's directory (the §4.2 "must specify which file" case).
// Fills `outDisk` (absolute) on success; fills `err` on failure.
bool ResolveDeclaringFile(const fs::path& sidecarDir, const toml::table& row,
                          std::string& outDisk, std::string& err) {
    const std::string fileRel = OptString(row, "file");
    std::error_code ec;

    if (!fileRel.empty()) {
        // Directory-scope: bind to the named file, relative to the sidecar dir.
        // Path safety (input-validation.md §Paths): reject a '..' segment so a
        // row cannot point outside the assets subtree it sits in.
        const fs::path relP = fs::path(fileRel).lexically_normal();
        for (const auto& seg : relP) {
            if (seg == "..") {
                err = "file '" + fileRel + "' escapes the sidecar directory "
                      "('..' traversal is rejected)";
                return false;
            }
        }
        const fs::path disk = sidecarDir / relP;
        if (!fs::is_regular_file(disk, ec)) {
            err = "file '" + fileRel + "' named by this row does not exist "
                  "(looked for " + disk.string() + ")";
            return false;
        }
        outDisk = disk.string();
        return true;
    }

    // File-scope: no `file` key → bind to the single sibling regular file in
    // the sidecar's own directory (NOT recursing — a deeper file needs `file`).
    fs::path onlyFile;
    size_t siblingFiles = 0;
    for (auto it = fs::directory_iterator(
             sidecarDir, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        if (it->path().filename() == kSidecarName) continue;  // skip the sidecar
        onlyFile = it->path();
        ++siblingFiles;
    }
    if (siblingFiles == 0) {
        err = "file-scope row (no `file` key) but the sidecar's directory holds "
              "no asset file beside it — name the asset with a `file` key";
        return false;
    }
    if (siblingFiles > 1) {
        err = "file-scope row (no `file` key) but the sidecar's directory holds "
              + std::to_string(siblingFiles) + " asset files — a row covering "
              "more than one file MUST name its `file` (§4.2 scope-vs-"
              "specificity)";
        return false;
    }
    outDisk = onlyFile.string();
    return true;
}

// One `[[asset]]` row → a resolved Declaration appended to `out`. Returns true
// when the row was valid + its target confirmed; on any shape/ambiguity/missing-
// target error fills `err` (the caller logs the teach + skips the row). The
// missing-target check uses `vanillaExists` for a VanillaPath target (the
// dominant US-1 case); a cross-mod/published-name target is parsed but NOT
// existence-checked in this per-plugin pass (it names ANOTHER plugin's published
// asset, invisible here) — its target is resolved in BuildOverlayMap PASS 2
// against the cross-plugin PublisherIndex (§5.3; a loud AP14 report on no-match).
bool ResolveOneRow(const toml::table& row, const fs::path& sidecarDir,
                   const std::string& sidecarPath,
                   const plugins::LoadedPlugin& plugin,
                   bool (*vanillaExists)(const std::string&),
                   std::vector<Declaration>& out, std::string& err) {
    if (!ValidateAssetRowKeys(row, err)) return false;

    const std::string replaces       = OptString(row, "replaces");
    const std::string replacesPlugin = OptString(row, "replaces_plugin");
    const std::string replacesPath   = OptString(row, "replaces_path");
    const std::string publishName    = OptString(row, "name");

    const bool hasReplaces = !replaces.empty();
    const bool hasPair     = !replacesPlugin.empty() || !replacesPath.empty();

    // AP14 — ambiguous/both-forms: `replaces` (the one-string form) AND the
    // pair on one row is contradictory. Reject loud, never silently pick one.
    if (hasReplaces && hasPair) {
        err = "ambiguous declaration: a row sets BOTH `replaces` and the "
              "`replaces_plugin`/`replaces_path` pair — declare exactly one "
              "form (one string for a vanilla path or a published name, OR the "
              "pair for an unnamed cross-mod asset by path)";
        return false;
    }
    if (!hasReplaces && !hasPair) {
        err = "missing target: a row must declare `replaces` (one string) OR "
              "the `replaces_plugin` + `replaces_path` pair — a row that "
              "replaces nothing is not a declaration";
        return false;
    }
    // The pair is all-or-nothing: one half without the other is incomplete.
    if (hasPair && (replacesPlugin.empty() || replacesPath.empty())) {
        err = "incomplete pair: the cross-mod-by-path form needs BOTH "
              "`replaces_plugin` and `replaces_path` (got "
              "replaces_plugin='" + replacesPlugin + "', replaces_path='"
              + replacesPath + "')";
        return false;
    }

    // Which loose file in THIS plugin's assets/ tree wins the target.
    std::string diskPath;
    if (!ResolveDeclaringFile(sidecarDir, row, diskPath, err)) return false;

    Declaration d;
    d.owningPlugin   = plugin.manifest.name;
    d.owningAuthor   = plugin.manifest.author;
    d.diskPath       = diskPath;
    d.sidecarPath    = sidecarPath;
    d.publishName    = publishName;
    d.routesToOverlay = false;

    if (hasPair) {
        // Unnamed cross-mod by path — a reference to ANOTHER plugin's asset (its
        // own vpath is not knowable in this per-plugin pass). Parsed here; the
        // raw replaces_plugin+replaces_path is resolved to the publisher's
        // serve-vpath in BuildOverlayMap PASS 2 (§5.3 / US-3/US-4) and keyed
        // there — not keyed in this pass.
        d.kind           = TargetKind::PluginPathPair;
        d.replacesPlugin = replacesPlugin;
        d.replacesPath   = replacesPath;
        out.push_back(std::move(d));
        return true;
    }

    // One-string form: a published name OR a vanilla path (§4.2 / §6).
    if (LooksLikePublishedName(replaces)) {
        // A cross-mod published-name target — same PASS-2 resolution as the pair:
        // parsed here, resolved to the publisher's serve-vpath in BuildOverlayMap
        // PASS 2 (§5.3) and keyed there. NOT existence-checked in this per-plugin
        // pass (the publisher's name is invisible here; a check now would
        // false-reject every valid cross-mod reference — PASS 2 does the loud
        // AP14 report on a genuine no-match against the complete PublisherIndex).
        d.kind   = TargetKind::PublishedName;
        d.target = replaces;
        out.push_back(std::move(d));
        return true;
    }

    // A vanilla-path target — THE dominant US-1 case, and the one this step
    // keys into the overlay map. Normalize to the overlay key (the SAME fold
    // the resolver applies to the engine's requested vpath, so the lookup
    // matches — asset_overlay::NormalizeVPath, reused never re-derived).
    const std::string key = asset_overlay::NormalizeVPath(replaces);

    // Missing-target teach (AP14): a `replaces` vanilla vpath that is not a
    // real vanilla asset is a typo, NOT a silent orphan. Reject loud, naming
    // the bad target. (When vanillaExists is null — before the resolver leaves
    // are wired — the check is skipped; the row registers and the resolver's
    // own miss path is the backstop.)
    if (vanillaExists && !vanillaExists(key)) {
        err = "missing target: `replaces = \"" + replaces + "\"` names a "
              "vanilla asset path that does not exist in the game "
              "(normalized: '" + key + "') — check the path against the game's "
              "real asset layout; a typo here is a loud error, never a silent "
              "no-op";
        return false;
    }

    d.kind            = TargetKind::VanillaPath;
    d.target          = replaces;
    d.routesToOverlay = true;
    d.overlayKey      = key;
    out.push_back(std::move(d));
    return true;
}

// Parse one `replaces.toml`, append every valid declaration. Logs a teach +
// skips each rejected row (one bad row never kills its siblings). Returns the
// count appended.
size_t LoadOneSidecar(const fs::path& sidecarPath,
                      const plugins::LoadedPlugin& plugin,
                      bool (*vanillaExists)(const std::string&),
                      std::vector<Declaration>& out) {
    const std::string fileLabel = sidecarPath.string();
    const fs::path sidecarDir = sidecarPath.parent_path();

    try {
        std::ifstream in(sidecarPath);
        if (!in) {
            LOG_ERROR(kCat, "Failed to open %s", fileLabel.c_str());
            return 0;
        }
        std::ostringstream ss;
        ss << in.rdbuf();
        toml::table doc = toml::parse(ss.str(), fileLabel);

        auto* arr = doc.get("asset");
        if (!arr || !arr->is_array()) {
            LOG_WARN_KV(kCat, "rejected",
                KV("plugin", plugin.manifest.name),
                KV("file",   fileLabel),
                KV("reason", std::string("no [[asset]] tables; a replaces.toml "
                                         "with no [[asset]] row declares nothing")));
            return 0;
        }

        size_t appended = 0;
        size_t idx = 0;
        for (const auto& elem : *arr->as_array()) {
            if (!elem.is_table()) {
                LOG_WARN_KV(kCat, "rejected",
                    KV("plugin", plugin.manifest.name),
                    KV("file",   fileLabel),
                    KV("reason", "[[asset]] entry at index " +
                                 std::to_string(idx) + " is not a table"));
                ++idx;
                continue;
            }
            std::string err;
            if (ResolveOneRow(*elem.as_table(), sidecarDir, fileLabel, plugin,
                              vanillaExists, out, err)) {
                ++appended;
            } else {
                // Teaching reject: plugin + sidecar + the bad row's reason. The
                // missing-target / ambiguous-form errors land here — the
                // falsifiable signal cap-73 reads (FAILS if a bad target is
                // silently dropped, AP15).
                LOG_WARN_KV(kCat, "rejected",
                    KV("plugin", plugin.manifest.name),
                    KV("file",   fileLabel),
                    KV("row",    (unsigned long long)idx),
                    KV("reason", err));
            }
            ++idx;
        }
        return appended;
    } catch (const toml::parse_error& e) {
        LOG_ERROR(kCat, "TOML parse error in %s: %s", fileLabel.c_str(),
                  e.what());
        return 0;
    } catch (const std::exception& e) {
        LOG_ERROR(kCat, "Unexpected error reading %s: %s", fileLabel.c_str(),
                  e.what());
        return 0;
    }
}

}  // namespace

size_t LoadDeclarationsFor(const plugins::LoadedPlugin& plugin,
                           bool (*vanillaExists)(const std::string&),
                           std::vector<Declaration>& out) {
    const plugins::PluginManifest& m = plugin.manifest;
    if (m.assetsEntrypointRel.empty()) return 0;  // no assets entrypoint

    const fs::path assetsRoot = m.folderPath / m.assetsEntrypointRel;
    std::error_code ec;
    if (!fs::exists(assetsRoot, ec) || !fs::is_directory(assetsRoot, ec)) {
        return 0;  // BuildOverlayMap already WARNs a missing assets dir
    }

    // Walk the assets/ tree for every `replaces.toml` (a sidecar may sit at any
    // depth — §4.2 placement). Each found sidecar is parsed; its rows bind
    // relative to the sidecar's OWN directory (file-scope) or by `file`
    // (directory-scope).
    size_t total = 0;
    for (auto it = fs::recursive_directory_iterator(
             assetsRoot, fs::directory_options::skip_permission_denied, ec);
         !ec && it != fs::recursive_directory_iterator(); it.increment(ec)) {
        std::error_code fec;
        if (!it->is_regular_file(fec)) continue;
        if (it->path().filename() != kSidecarName) continue;
        total += LoadOneSidecar(it->path(), plugin, vanillaExists, out);
    }
    if (ec) {
        LOG_WARN_KV(kCat, "walk_error",
            KV("plugin", m.name),
            KV("root",   assetsRoot.string()),
            KV("reason", ec.message()));
    }
    return total;
}

}  // namespace kcdx::asset_sidecar
