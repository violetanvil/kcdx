#pragma once

#include <cstdint>
#include <vector>

// Shared game-version compatibility decision.
//
// kcdx runs ONE version-compat policy for BOTH authoring paths:
//   - kcdx plugins (kcdx.toml [plugin] compatible_game_versions /
//     version_independent), decided in plugin_loader.cpp::ValidateManifest.
//   - pak mods (mod.manifest), decided in mod_absorb's pak-mod gate.
// Both compare the mod/plugin's declared compatible game versions against the
// running build (kcdx::plugins::g_runtimeGameVersion) with the SAME
// graceful-degradation rule. This module is the single source of that decision
// so the two paths cannot drift (the design's "ONE kcdx-owned version policy
// for BOTH plugins and pak mods" — docs/mod-loader-absorb.md).
//
// This module returns ONLY the verdict; each caller keeps its own logging
// (the log lines differ: plugin reject lines name compatible_game_versions and
// the [plugin] table; pak-mod lines name the mod folder + mod.manifest). The
// caller maps the verdict to its accept/reject/load-anyway behavior + log line.

namespace kcdx::version_compat {

enum class CompatResult {
    Compatible,           // load it (matched, or version-independent)
    Incompatible,         // reject: a known game version, none declared matches
    UnknownGameVersion,   // engine couldn't detect the running build; caller
                          // logs WARN + loads anyway (graceful-degradation)
};

// Decide game-version compatibility for a mod/plugin.
//
//   compatibleGameVersions : the build numbers it declares compatibility with
//                            (empty = none declared).
//   versionIndependent     : the author set "works on any build".
//   runtimeGameVersion     : the detected running build (0 = undetected).
//
// Decision order (matches the pre-extraction ValidateManifest behavior exactly):
//   1. runtimeGameVersion == 0 AND NOT versionIndependent -> UnknownGameVersion
//      (we couldn't detect the build; don't reject over our own detection
//       failure — caller WARNs + loads anyway).
//   2. versionIndependent == true -> Compatible.
//   3. any declared version == runtimeGameVersion -> Compatible.
//   4. else -> Incompatible.
//
// NOTE the ordering of (1) vs (2): when the build is undetected, a
// version-independent mod is still Compatible (rule 2 wins for it), because
// rule 1 only fires for NON-version-independent mods. This preserves
// ValidateManifest's exact pre-extraction control flow, where the
// graceful-degradation early-return is itself guarded by `!versionIndependent`.
CompatResult DecideGameVersionCompat(const std::vector<uint32_t>& compatibleGameVersions,
                                     bool versionIndependent,
                                     uint32_t runtimeGameVersion);

}  // namespace kcdx::version_compat
