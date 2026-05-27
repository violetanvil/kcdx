#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Shared game-version compatibility decision.
//
// kcdx runs ONE version-compat policy for BOTH authoring paths:
//   - kcdx plugins (kcdx.toml [plugin] supports = […]), decided in
//     plugin_loader.cpp::ValidateManifest.
//   - pak mods (mod.manifest <supports>), decided in mod_absorb's pak-mod gate
//     (DecideModCompat).
// Both match the mod/plugin's declared `supports` version-pattern list against
// the running build's version STRING (kcdx::plugins::g_runtimeGameVersionString,
// from wh_sys_version) with the SAME prefix-wildcard + graceful-degradation
// rule. This module is the single source of that decision so the two paths
// cannot drift (the design's "ONE kcdx-owned version policy for BOTH plugins and
// pak mods" — docs/mod-loader-absorb.md "Version gate UNIFICATION").
//
// This module returns ONLY the verdict; each caller keeps its own logging
// (the log lines differ: plugin reject lines name the [plugin] table; pak-mod
// lines name the mod folder + mod.manifest). The caller maps the verdict to its
// accept/reject/load-anyway behavior + log line.

namespace kcdx::version_compat {

enum class CompatResult {
    Compatible,           // load it (matched, or version-independent)
    Incompatible,         // reject: a known game version, none declared matches
    UnknownGameVersion,   // engine couldn't detect the running build; caller
                          // logs WARN + loads anyway (graceful-degradation)
};

// Decide compat under the vanilla <supports> model. `supports` is the list of
// version-pattern strings the mod/plugin declares (a mod.manifest <supports>
// list, or a plugin's migrated `supports` key). Each pattern is matched against
// `runtimeVersionString` (wh_sys_version): a trailing '*' is a PREFIX wildcard
// ("1.5*" matches "1.5", "1.5.1164953", "1.5anything"); no '*' = exact string
// match. Empty `supports` list = NO restriction = Compatible (version-
// independent by absence, matching the vanilla meaning). Empty/unknown
// runtimeVersionString = UnknownGameVersion (caller loads anyway + WARNs).
//
// This is the UNIFIED gate BOTH pak mods and kcdx plugins use: plugins via
// ValidateManifest, pak mods via mod_absorb's DecideModCompat. (The earlier
// integer exact-match model is removed — both paths now compare the <supports>
// string list against wh_sys_version.) See docs/mod-loader-absorb.md "Version
// gate UNIFICATION".
//
// Decision order (the ordering note in the design spec — empty-supports check
// FIRST so "no restriction" is Compatible regardless of whether we know the
// runtime version; only a NON-empty list we can't evaluate yields Unknown):
//   1. supports is empty            -> Compatible (no restriction declared).
//   2. runtimeVersionString empty   -> UnknownGameVersion (can't evaluate a
//                                       restriction without the runtime string).
//   3. any pattern matches          -> Compatible.
//   4. else                         -> Incompatible.
CompatResult DecideGameVersionCompatString(const std::vector<std::string>& supports,
                                           const std::string& runtimeVersionString);

}  // namespace kcdx::version_compat
