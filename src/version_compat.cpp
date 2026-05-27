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

}  // namespace kcdx::version_compat
