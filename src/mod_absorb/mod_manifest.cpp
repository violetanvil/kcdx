#include "mod_manifest.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <unordered_set>

#include "../log.h"

// mod.manifest reader. The XML handling here is a DELIBERATELY-MINIMAL tag
// extractor for the KNOWN-FLAT mod.manifest shape (see the header's example) —
// it is NOT a general XML parser. It handles only: a single first-occurrence
// <tag>...</tag>, leading/trailing whitespace trimming, and the five common
// entities. It does NOT handle attributes, namespaces, nested same-name tags,
// CDATA, comments, or numeric entities — the mod.manifest shape needs none of
// those. If mod.manifest ever grows a richer shape, this is the place to swap
// in a real parser (still no vendored dep without cause).

namespace kcdx::mod_absorb {

namespace {

// Trim ASCII whitespace from both ends of [begin, end). Returns the trimmed
// substring of `s`.
std::string Trim(const std::string& s) {
    size_t b = 0;
    size_t e = s.size();
    while (b < e && std::isspace(static_cast<unsigned char>(s[b]))) ++b;
    while (e > b && std::isspace(static_cast<unsigned char>(s[e - 1]))) --e;
    return s.substr(b, e - b);
}

}  // namespace

std::string DecodeEntities(const std::string& s) {
    // Decode the five common XML entities in a single left-to-right pass,
    // never re-scanning emitted output — so "&amp;lt;" decodes to the literal
    // "&lt;" (XML's one-pass semantics), not "<".
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size();) {
        if (s[i] == '&') {
            if (s.compare(i, 5, "&amp;") == 0)  { out.push_back('&');  i += 5; continue; }
            if (s.compare(i, 4, "&lt;") == 0)   { out.push_back('<');  i += 4; continue; }
            if (s.compare(i, 4, "&gt;") == 0)   { out.push_back('>');  i += 4; continue; }
            if (s.compare(i, 6, "&quot;") == 0) { out.push_back('"');  i += 6; continue; }
            if (s.compare(i, 6, "&apos;") == 0) { out.push_back('\''); i += 6; continue; }
        }
        out.push_back(s[i]);
        ++i;
    }
    return out;
}

std::string ExtractTag(const std::string& xml, const char* tag) {
    // Build "<tag" and "</tag>". We match "<tag" then require the next char to
    // be '>' (no attributes in this shape) so we don't false-match a longer
    // tag name (e.g. <version> must not match a <versionrestriction> open).
    const std::string open = std::string("<") + tag;
    const std::string close = std::string("</") + tag + ">";

    size_t searchFrom = 0;
    for (;;) {
        const size_t openPos = xml.find(open, searchFrom);
        if (openPos == std::string::npos) return {};
        const size_t afterName = openPos + open.size();
        if (afterName >= xml.size()) return {};
        const char c = xml[afterName];
        // Require the char right after the tag name to end the name: '>' (no
        // attributes), whitespace (attributes we ignore), or '/' (self-close).
        if (c == '>') {
            const size_t inner = afterName + 1;
            const size_t closePos = xml.find(close, inner);
            if (closePos == std::string::npos) return {};
            return Trim(DecodeEntities(xml.substr(inner, closePos - inner)));
        }
        if (c == '/' && afterName + 1 < xml.size() && xml[afterName + 1] == '>') {
            // Self-closing <tag/> — empty content.
            return {};
        }
        if (std::isspace(static_cast<unsigned char>(c))) {
            // <tag attr=...> — find the closing '>' of the open tag, then the
            // inner text up to </tag>. (mod.manifest has no attributes today,
            // but tolerate them rather than mis-extract.)
            const size_t openEnd = xml.find('>', afterName);
            if (openEnd == std::string::npos) return {};
            const size_t inner = openEnd + 1;
            const size_t closePos = xml.find(close, inner);
            if (closePos == std::string::npos) return {};
            return Trim(DecodeEntities(xml.substr(inner, closePos - inner)));
        }
        // Not our tag (e.g. matched "<version" inside "<versionrestriction") —
        // keep searching.
        searchFrom = afterName;
    }
}

ModManifest ReadModManifest(const std::filesystem::path& modManifestPath) {
    ModManifest m;

    std::ifstream f(modManifestPath, std::ios::binary);
    if (!f) {
        LOG_WARN("MOD_ABSORB",
            "mod.manifest unreadable: could not open '%s' — the mod will not "
            "be enabled (no manifest, no record)",
            modManifestPath.string().c_str());
        return m;  // ok == false
    }

    std::ostringstream ss;
    ss << f.rdbuf();
    const std::string xml = ss.str();

    // Require the known root + info container — if absent, this is not a
    // mod.manifest we recognize (fail LOUD, not a silent empty record).
    if (xml.find("<kcd_mod") == std::string::npos ||
        xml.find("<info") == std::string::npos) {
        LOG_WARN("MOD_ABSORB",
            "mod.manifest malformed: '%s' has no <kcd_mod>/<info> root — the "
            "mod will not be enabled",
            modManifestPath.string().c_str());
        return m;  // ok == false
    }

    m.name        = ExtractTag(xml, "name");
    m.description = ExtractTag(xml, "description");
    m.author      = ExtractTag(xml, "author");
    m.version     = ExtractTag(xml, "version");
    m.createdOn   = ExtractTag(xml, "created_on");
    m.modId       = ExtractTag(xml, "modid");  // optional; caller falls back to folder name
    m.ok = true;
    return m;
}

namespace {

// One-time-per-mod WARN tracking for the RE-pending restriction gap. Keyed by
// modId so each distinct mod warns at most once across the process lifetime.
std::unordered_set<std::string> g_restrictionWarned;

// Does the raw XML contain an element whose tag-name CONTAINS "version" but is
// NOT the known <version> element? Returns the offending tag name (without
// brackets) or empty if none. Case-insensitive on the substring match; the
// scan walks '<' ... name-end and tests each opening tag.
std::string FindUnknownVersionElement(const std::string& xml) {
    for (size_t i = 0; i + 1 < xml.size(); ++i) {
        if (xml[i] != '<') continue;
        const char next = xml[i + 1];
        // Skip closing tags, declarations/PIs (<?xml), comments (<!--).
        if (next == '/' || next == '?' || next == '!') continue;
        // Read the tag name: letters/digits/_/- until whitespace, '>', or '/'.
        size_t j = i + 1;
        std::string name;
        while (j < xml.size()) {
            const char c = xml[j];
            if (std::isspace(static_cast<unsigned char>(c)) || c == '>' || c == '/') break;
            name.push_back(c);
            ++j;
        }
        if (name.empty()) continue;
        // Lower-case the name for the substring + equality tests.
        std::string lower = name;
        for (char& c : lower) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        if (lower == "version") continue;  // the mod's OWN version — known.
        if (lower.find("version") != std::string::npos) {
            return name;  // candidate restriction element we don't yet parse.
        }
    }
    return {};
}

}  // namespace

version_compat::CompatResult DecideModCompat(const ModManifest& m,
                                             const std::string& rawXml,
                                             const std::string& modIdForLog,
                                             uint32_t runtimeGameVersion) {
    (void)m;                   // no parsed restriction field yet (RE-pending).
    (void)runtimeGameVersion;  // nothing to compare against until the element is RE'd.

    // Defensive: if a candidate restriction element IS present, make the
    // RE-pending gap LOUD (one-time per mod) — kcdx enables the mod but says so
    // (AP14: a silent ignore of a real restriction would be a fail-state hidden
    // as success).
    const std::string unknownEl = FindUnknownVersionElement(rawXml);
    if (!unknownEl.empty()) {
        if (g_restrictionWarned.insert(modIdForLog).second) {
            LOG_WARN("MOD_ABSORB",
                "mod '%s' declares a possible game-version restriction element "
                "<%s> that kcdx cannot yet enforce (the restriction element name "
                "is RE-pending — see docs/mod-loader-absorb.md). Enabling the mod "
                "anyway; run /research-disassembly on the native version-gate to "
                "pin the element, then complete the present-field branch.",
                modIdForLog.c_str(), unknownEl.c_str());
        }
    }

    // No restriction parsed -> Compatible (matches native + every installed mod).
    return version_compat::CompatResult::Compatible;
}

}  // namespace kcdx::mod_absorb
