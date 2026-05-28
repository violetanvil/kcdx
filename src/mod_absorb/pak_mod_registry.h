#pragma once

#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

#include "mod_manifest.h"

// Pak-mod registry + unified discovery — STEP 3 of the mod-loader absorb.
//
// kcdx IS the mod loader: it owns WHICH mods load and in what ORDER, and a
// kcdx plugin works dropped in EITHER kcdx-plugins/ OR the vanilla mods/
// directory. STEP 2 built the per-mod machinery (record synthesis +
// mod.manifest parse + the version gate). This module adds the DISCOVERY pass
// that finds vanilla "pak mods" (a folder with a mod.manifest marker, no
// kcdx.toml), registers each one, reads the vanilla mod_order.txt baseline
// ordering, and runs the kcdx-owned version gate at the point the runtime
// game version is known.
//
// SEPARATION FROM THE PLUGIN WALKER (design-determined): the plugin discovery
// walk (config.cpp WalkForTomls) is UNTOUCHED. Discover() is a SEPARATE pass
// that owns the mod.manifest marker-file classification. A folder with a
// kcdx.toml is SKIPPED here — the plugin walker owns it (a kcdx plugin dropped
// in mods/ is still a kcdx plugin, not a pak mod). A folder with a mod.manifest
// and NO kcdx.toml is registered as a PakMod. Neither marker -> recurse (it is
// a container) or skip. Both passes run over both roots.
//
// The registered pak mods are FOLDED into the load_order model: each becomes
// an Effective row keyed "mods.<modId>", zone=after_game, priority=0 (an early
// after_game block), with the mod_order.txt line index as a secondary ordering
// key so the vanilla relative order is preserved. A user load_order.toml row
// keyed "mods.<modid>" overrides priority/zone/enabled.

namespace kcdx::mod_absorb {

// One discovered vanilla pak mod (a mods/-style folder with a mod.manifest and
// no kcdx.toml). Registered by Discover(), folded into load_order by
// load_order::Resolve(), version-gated later by ApplyVersionGate().
struct PakMod {
    std::string modId;            // the mod id (manifest <modid>, else folder name)
    std::string rootPathSlash;    // root dir WITH trailing '/'    (record +0x08)
    std::string rootPathNoSlash;  // root dir WITHOUT trailing '/' (record +0x20)
    ModManifest manifest;         // the parsed mod.manifest (fields + <supports>)
    bool        fromModsDir = false;  // true: found under <game-root>/mods/;
                                      // false: found under kcdx-plugins/ OR
                                      // Steam Workshop (distinguish via
                                      // fromWorkshop).
    // Steam Workshop origin marker. true: this mod was registered by
    // DiscoverWorkshop() from a <Steam>/steamapps/workshop/content/1771300/<id>/
    // subdirectory. The folder NAME is the Steam Workshop file ID. Mutually
    // exclusive with fromModsDir = true. Used by the discovery-funnel log
    // breakdown so the user sees "N from mods/, M from kcdx-plugins/, K from
    // Steam Workshop" at boot.
    bool        fromWorkshop = false;
    // The mod's position in <modsDir>/mod_order.txt (0-based file line order).
    // -1 = NOT listed in mod_order.txt (sorts AFTER the listed ones; see the
    // load_order fold). Populated by Discover via the mod_order.txt index map.
    int         modOrderIndex = -1;
};

// The process-lifetime registry of discovered pak mods. Populated by Discover,
// consumed by load_order::Resolve (the fold) + ApplyVersionGate. The synthesized
// load-order name for element i is "mods." + Registry()[i].modId.
std::vector<PakMod>& Registry();

// Clear the registry. Discovery is once-per-session, but tests register
// synthetic rows and must reset between cases.
void ClearRegistry();

// Read <modsDir>/mod_order.txt into an ordered modid->line-index map. The
// vanilla file is ONE mod id per line, file order == load/mount order
// (ModManager_ReadModOrder semantics). '#'-prefixed lines and blank lines are
// stripped; surrounding whitespace is trimmed. An absent file yields an empty
// map + an INFO log (no mod_order.txt is a normal first-run state, not an
// error). Exposed in the header so the self-test can exercise the parse against
// a literal string via ParseModOrderText below.
std::unordered_map<std::string, int> ReadModOrder(const std::filesystem::path& modsDir);

// Pure parse of mod_order.txt TEXT (the file body as one string) into an
// ordered modid->index map (index = 0-based position among the surviving,
// non-comment, non-blank lines). '#' comments + blanks stripped, entries
// trimmed. Unit-testable from a literal string (no file I/O) — ReadModOrder
// reads the file and calls this.
std::unordered_map<std::string, int> ParseModOrderText(const std::string& text);

// Walk `root` for pak-mod folders and register each into Registry().
//
// Classification (marker-file, mirroring config.cpp's plugin-walk idiom):
//   - folder has kcdx.toml            -> SKIP (the plugin walker owns it;
//                                         double-registering would conflict).
//   - folder has mod.manifest (no
//     kcdx.toml)                      -> register a PakMod.
//   - neither                         -> recurse into it (a container).
//
// `fromModsDir` stamps each registered PakMod (true for <game-root>/mods/,
// false for kcdx-plugins/), used only for the discovery-funnel log breakdown.
// modOrderIndex is left at -1 here; the caller populates it from the
// mod_order.txt map after the walk (Discover does not read mod_order.txt
// itself — its caller owns the modsDir).
//
// Registration is UNCONDITIONAL of game-version compatibility: the <supports>
// gate runs LATER (ApplyVersionGate), at the point the runtime version string
// is known. A malformed/unreadable mod.manifest (ReadModManifest ok==false)
// is logged LOUD by ReadModManifest and the folder is NOT registered.
void Discover(const std::filesystem::path& root, bool fromModsDir);

// Walk a Steam Workshop content root for KCD2 (appid 1771300) and register each
// subscribed Workshop mod into Registry().
//
// `workshopRoot` is <Steam>/steamapps/workshop/content/1771300/ (composed by
// paths::WorkshopContentDir()). Each IMMEDIATE subdirectory of workshopRoot is
// a Steam Workshop item — its NAME is the Workshop file ID (e.g. "3728570527"),
// and it contains a vanilla pak mod (mod.manifest + Data/*.pak). The walk is
// ONE-LEVEL: workshop items live exactly one directory down from workshopRoot,
// never nested in container folders the way kcdx-plugins/ folders can be.
//
// Classification per subdirectory:
//   - has kcdx.toml                        -> SKIP (a kcdx plugin somehow
//                                              dropped here; the plugin walker
//                                              would own it, double-registration
//                                              is a conflict).
//   - has mod.manifest (no kcdx.toml)      -> register a PakMod with
//                                              fromWorkshop = true.
//   - neither                              -> REJECT loud (a Workshop subscription
//                                              with no mod.manifest is malformed —
//                                              never a silent skip).
//
// The modId resolves the same way as Discover(): manifest <modid> if present,
// else the folder name (the Workshop file ID). Registration is UNCONDITIONAL of
// game-version compatibility — the <supports> gate (ApplyVersionGate) runs over
// the registry later, after the runtime version string is detected.
//
// `workshopRoot` empty / absent / not a directory is NOT an error (a player
// without Steam, or without any KCD2 Workshop subscriptions, is a valid state):
// the function returns silently with an info log naming the reason. The caller
// gates via paths::WorkshopContentDir() returning empty for the no-Steam case,
// so the typical path is "no Workshop → no call" rather than "call → skip".
void DiscoverWorkshop(const std::filesystem::path& workshopRoot);

// Run the kcdx-owned version gate over every registered pak mod, at the point
// `runtimeVersionString` (g_runtimeGameVersionString, from wh_sys_version) is
// known. For each registered mod, DecideModCompat(m.manifest, runtime):
//   - Incompatible        -> load_order::SetEngineAccepted("mods.<modid>", false)
//                            + a LOUD INFO line ("not compatible with game X —
//                            disabled"). IsPluginEnabled("mods.<modid>") then
//                            returns false and every downstream reader honors it.
//   - Compatible / Unknown -> stay enabled (Unknown also logs a WARN — we
//                            couldn't evaluate a declared restriction).
//
// MUST run AFTER load_order::Resolve has folded the registry into g_effective
// (so the "mods.<modid>" rows exist for SetEngineAccepted to flip) AND after
// the runtime version string is detected (dllmain VersionDetected). Returns the
// number of mods it disabled (for the funnel log). The gate is the SAME
// mechanism the plugin path + zone_gate use — one kcdx-owned version policy.
size_t ApplyVersionGate(const std::string& runtimeVersionString);

// The synthesized load-order name for a pak mod with id `modId`: "mods." + modId.
// Used by the load_order fold + ApplyVersionGate so the key is composed in ONE
// place. A pak mod's modId charset is the vanilla folder-name charset, which
// can include characters a kcdx [plugin].name forbids ('.', '-', uppercase) —
// the "mods." prefix keeps these in a namespace disjoint from kcdx plugin names
// (whose charset [a-z0-9_] can never begin with "mods.").
std::string LoadOrderNameFor(const std::string& modId);

}  // namespace kcdx::mod_absorb
