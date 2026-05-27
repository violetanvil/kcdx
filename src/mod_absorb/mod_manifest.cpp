#include "mod_manifest.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

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

// Parse the <supports> game-version restriction list out of a mod.manifest.
//
// TWO-CONTEXT DISCRIMINATOR: a mod.manifest carries TWO <version> contexts —
// <info><version> (the mod's OWN version) and <supports><version> (the
// restriction patterns). We must scan <version> ONLY within the <supports>
// block, never the whole manifest, or the mod's own version would be captured
// as a (bogus) restriction. So: locate the <supports>...</supports> inner text
// FIRST (empty when <supports> is absent), THEN walk <version> entries within
// THAT substring.
std::vector<std::string> ParseSupports(const std::string& xml) {
    std::vector<std::string> out;
    // Locate the <supports>...</supports> inner text directly (NOT via ExtractTag
    // — its decode would mangle the nested <version> tags). We want the RAW inner
    // text so the <version> walk below sees the tags intact, decoding only each
    // entry's text content.
    const std::string openTag = "<supports";
    const size_t openPos = xml.find(openTag);
    if (openPos == std::string::npos) {
        return out;  // <supports> absent -> no restriction.
    }
    const size_t openEnd = xml.find('>', openPos);
    if (openEnd == std::string::npos) {
        return out;
    }
    const size_t inner = openEnd + 1;
    const size_t closePos = xml.find("</supports>", inner);
    if (closePos == std::string::npos) {
        return out;  // malformed (no close) -> treat as no restriction.
    }
    const std::string block = xml.substr(inner, closePos - inner);

    // Walk every <version>...</version> entry within the supports block only.
    const std::string vOpen = "<version>";
    const std::string vClose = "</version>";
    size_t from = 0;
    for (;;) {
        const size_t vo = block.find(vOpen, from);
        if (vo == std::string::npos) break;
        const size_t vstart = vo + vOpen.size();
        const size_t vc = block.find(vClose, vstart);
        if (vc == std::string::npos) break;
        out.push_back(Trim(DecodeEntities(block.substr(vstart, vc - vstart))));
        from = vc + vClose.size();
    }
    return out;
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
    m.supports    = ParseSupports(xml);        // <supports><version>… restriction list; empty = none
    m.ok = true;
    return m;
}

version_compat::CompatResult DecideModCompat(const ModManifest& m,
                                             const std::string& runtimeVersionString) {
    // The UNIFIED gate: the mod's parsed <supports> patterns vs the runtime
    // wh_sys_version string. Empty m.supports = <supports> absent = no
    // restriction = Compatible. Same gate kcdx plugins use, so the two paths
    // cannot drift (docs/mod-loader-absorb.md "Version gate UNIFICATION").
    return version_compat::DecideGameVersionCompatString(m.supports, runtimeVersionString);
}

}  // namespace kcdx::mod_absorb
