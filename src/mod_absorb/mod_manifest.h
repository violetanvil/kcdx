#pragma once

#include <filesystem>
#include <string>
#include <vector>

#include "../version_compat.h"

// mod.manifest reader — parse a pak mod's mod.manifest into the fields a
// synthesized I_Mod record needs (feeds record_synth's ModRecordInput) plus a
// game-version verdict.
//
// mod.manifest is a SIMPLE FLAT XML file (real example):
//
//   <?xml version="1.0" encoding="utf-8"?>
//   <kcd_mod>
//     <info>
//       <name>cheat</name>
//       <description>KCD2 Cheat Mod ...</description>
//       <author>Othiden</author>
//       <version>2.21</version>            <!-- the MOD's own version, NOT a
//                                                game-version restriction -->
//       <created_on>Fri Mar 14 16:06:23 EDT 2025</created_on>
//       <dependencies></dependencies>
//     </info>
//   </kcd_mod>
//
// We deliberately do NOT vendor an XML library: one flat known-shape file does
// not justify a new dependency. The extractor below is a minimal hand-rolled
// tag reader for THIS shape only (see mod_manifest.cpp).

namespace kcdx::mod_absorb {

// Minimal tag extractor for the known-flat mod.manifest shape — NOT a general
// XML parser. Finds the FIRST <tag> ... </tag> and returns its trimmed,
// entity-decoded inner text. Absent tag -> empty string. Exposed in the header
// so the self-test can exercise it directly against a literal XML string.
std::string ExtractTag(const std::string& xml, const char* tag);

// Decode the five common XML entities (&amp; &lt; &gt; &quot; &apos;) in `s`.
// Exposed for the self-test's entity-decode assertion. No numeric/other
// entities — the mod.manifest shape only carries these five.
std::string DecodeEntities(const std::string& s);

// Parse the <supports> game-version restriction list from a mod.manifest's XML.
// Returns the list of <version> patterns found INSIDE the <supports> block
// only — NOT the <info><version> mod's-own-version (the two-context
// discriminator). <supports> absent (or malformed) -> empty list (no
// restriction). Exposed so the self-test can exercise the two-context parse
// directly against a literal XML string.
std::vector<std::string> ParseSupports(const std::string& xml);

struct ModManifest {
    bool ok = false;          // false if the file couldn't be read/parsed.
    std::string name;         // <name>        -> I_Mod displayName (+0x28)
    std::string description;  // <description> -> +0x30
    std::string author;       // <author>      -> +0x38
    std::string version;      // <version>     -> +0x40 (the MOD's own version)
    std::string createdOn;    // <created_on>  -> +0x48
    // modId: the mod's id. mod.manifest MAY carry it as a <modid> element; if
    // absent, the caller supplies the folder name (the native loader uses the
    // folder name as the id — docs/mod-loader-absorb.md, record +0x10).
    std::string modId;
    // supports: the <supports> game-version restriction patterns — the list of
    // <version> entries inside the <supports> block (NOT the <info><version>
    // mod's-own-version above). Each is a wh_sys_version pattern with a trailing
    // '*' prefix wildcard. EMPTY = <supports> absent = NO restriction = enabled
    // (the vanilla meaning). See docs/mod-loader-absorb.md "GAME-VERSION
    // RESTRICTION".
    std::vector<std::string> supports;
};

// Read + parse <modManifestPath>. Returns ok=false + logs WARN naming the file
// if it can't be opened or has no <kcd_mod>/<info> root.
ModManifest ReadModManifest(const std::filesystem::path& modManifestPath);

// Decide a pak mod's game-version compatibility.
//
// Delegates to the UNIFIED string-prefix-wildcard gate
// (version_compat::DecideGameVersionCompatString): the mod's parsed <supports>
// patterns (m.supports) are matched against the runtime wh_sys_version string.
// Empty m.supports = <supports> absent = no restriction = Compatible (the
// vanilla meaning). This is the SAME gate kcdx plugins use, so the two paths
// cannot drift (docs/mod-loader-absorb.md "Version gate UNIFICATION").
//
//   runtimeVersionString : the detected wh_sys_version string
//                          (g_runtimeGameVersionString; empty/undetected ->
//                          UnknownGameVersion for a non-empty restriction).
version_compat::CompatResult DecideModCompat(const ModManifest& m,
                                             const std::string& runtimeVersionString);

}  // namespace kcdx::mod_absorb
