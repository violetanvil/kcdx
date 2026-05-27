#include "version_compat.h"

namespace kcdx::version_compat {

CompatResult DecideGameVersionCompat(const std::vector<uint32_t>& compatibleGameVersions,
                                     bool versionIndependent,
                                     uint32_t runtimeGameVersion) {
    // Rule 1: graceful-degradation. If we couldn't determine the running build
    // AND the mod is not version-independent, don't reject over our own
    // detection failure. (Guarded by !versionIndependent so a
    // version-independent mod still resolves via rule 2 even when undetected —
    // mirrors ValidateManifest's `g_runtimeGameVersion == 0 &&
    // !m.versionIndependent` early-return guard.)
    if (runtimeGameVersion == 0 && !versionIndependent) {
        return CompatResult::UnknownGameVersion;
    }

    // Rule 2: version-independent mods load on any build.
    if (versionIndependent) {
        return CompatResult::Compatible;
    }

    // Rule 3: any declared version matches the running build.
    for (uint32_t gv : compatibleGameVersions) {
        if (gv == runtimeGameVersion) {
            return CompatResult::Compatible;
        }
    }

    // Rule 4: a known build, nothing declared matches it.
    return CompatResult::Incompatible;
}

namespace {

// One <supports> pattern vs the runtime version string. A trailing '*' is a
// PREFIX wildcard: "1.5*" matches iff `runtime` begins with "1.5" (the
// pattern minus the '*'). No '*' = exact string equality. ("1.5" without a '*'
// does NOT match "1.5.1164953" — that is the wildcard-vs-exact discriminator.)
bool PatternMatches(const std::string& pattern, const std::string& runtime) {
    if (!pattern.empty() && pattern.back() == '*') {
        const std::string prefix = pattern.substr(0, pattern.size() - 1);
        // rfind(prefix, 0) == 0 is "runtime starts with prefix". An empty
        // prefix (bare "*") matches anything, which is correct for "*".
        return runtime.rfind(prefix, 0) == 0;
    }
    return pattern == runtime;
}

}  // namespace

CompatResult DecideGameVersionCompatString(const std::vector<std::string>& supports,
                                           const std::string& runtimeVersionString) {
    // Rule 1: an empty supports list = no restriction declared = Compatible,
    // REGARDLESS of whether we know the runtime version (the vanilla "absent
    // <supports> -> enabled" meaning). This is checked BEFORE the runtime-
    // string-empty check so a no-restriction mod loads even when system.cfg
    // gave us nothing.
    if (supports.empty()) {
        return CompatResult::Compatible;
    }

    // Rule 2: a NON-empty restriction we cannot evaluate because we don't know
    // the runtime version string -> UnknownGameVersion (caller WARNs + loads
    // anyway; graceful-degrade over our own detection failure, never a reject).
    if (runtimeVersionString.empty()) {
        return CompatResult::UnknownGameVersion;
    }

    // Rule 3: the list is compatible iff ANY declared pattern matches.
    for (const std::string& pattern : supports) {
        if (PatternMatches(pattern, runtimeVersionString)) {
            return CompatResult::Compatible;
        }
    }

    // Rule 4: a known runtime version, no declared pattern matches it.
    return CompatResult::Incompatible;
}

}  // namespace kcdx::version_compat
