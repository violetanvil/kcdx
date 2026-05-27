#pragma once

#include <filesystem>
#include <string>

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
};

// Read + parse <modManifestPath>. Returns ok=false + logs WARN naming the file
// if it can't be opened or has no <kcd_mod>/<info> root.
ModManifest ReadModManifest(const std::filesystem::path& modManifestPath);

// Decide a pak mod's game-version compatibility.
//
// RE-PENDING (docs/mod-loader-absorb.md "GAME-VERSION RESTRICTION"): the
// element a mod.manifest uses to RESTRICT to a game version is not yet known
// (absent in the wiki + every installed mod). So for now this returns
// Compatible (no restriction present -> enabled, matching native behavior).
//
// `modIdForLog` names the mod in the defensive WARN below (the folder name /
// resolved id), and `rawXml` is the manifest text scanned for a candidate
// restriction element. If the XML carries any element whose name CONTAINS
// "version" OTHER than the known <version> (the mod's own version), this logs a
// ONE-TIME WARN naming the mod + the unrecognized element, stating kcdx cannot
// yet enforce a game-version restriction and is enabling the mod (NOT a silent
// pass — AP14): the RE-pending gap is LOUD if a real restricted mod appears.
version_compat::CompatResult DecideModCompat(const ModManifest& m,
                                             const std::string& rawXml,
                                             const std::string& modIdForLog,
                                             uint32_t runtimeGameVersion);

}  // namespace kcdx::mod_absorb
