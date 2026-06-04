#pragma once

#include <string>
#include <vector>

#include "plugin_loader.h"  // plugins::LoadedPlugin (the plugin being scanned)

// === Asset sidecar — the no-code declarative replacement surface ============
//
// A mod author replaces a vanilla (or another mod's) asset by DECLARING it,
// never by a path coincidence (design asset-replacement.md §4.1: existence ≠
// replacement). The declaration lives in an opt-in metadata sidecar TOML
// co-located with the asset under the plugin's assets/ dir — the same sidecar
// idiom as the code-target `targets.toml` (docs/lua/targets.md), but for ASSET
// replacement and co-located with the asset (it may sit beside the file or
// higher in the assets/ tree; its SCOPE is its placement, §4.2).
//
// SIDECAR FILENAME + SHAPE (the placement-read shape, stated for the reader —
// §4.2 names "the targets.toml idiom" but leaves the asset-sidecar filename to
// placement-reading since the asset declaration is a distinct concern from a
// code target): the sidecar is a file named `replaces.toml`, co-located with
// the asset(s) it speaks to, carrying `[[asset]]` rows. A `replaces.toml`
// beside one file scopes to that directory; one higher in the tree covers its
// subtree. The more it abstracts (the higher it sits), the more each row must
// specify — a row in a sidecar sharing a directory with exactly one asset needs
// no `file` key; a row in a directory-level sidecar covering several files MUST
// name its `file` (a path relative to the sidecar's own directory, §4.2
// scope-vs-specificity). The name `replaces.toml` (not `targets.toml`) keeps
// the asset-replacement concern distinct from the code-target sidecar at the
// same idiom — a directory may legitimately carry both.
//
// Each `[[asset]]` row declares, per asset:
//   replaces        = "Libs/UI/Textures/KCDLogo.dds"  # ONE string: a vanilla
//                                                      # vpath OR another mod's
//                                                      # published name
//                                                      # ("redmoon.outfit.belt")
//     -- XOR (unnamed cross-mod by path) --
//   replaces_plugin = "redmoon.outfit"   # the other plugin's <author>.<plugin>
//   replaces_path   = "male/shirt.dds"   # its asset's path within ITS assets/
//
//   file            = "kcdlogo.dds"      # OPTIONAL at file-scope (sidecar shares
//                                        # a dir with one asset); REQUIRED at
//                                        # directory-scope (names which file the
//                                        # row speaks to, relative to the sidecar
//                                        # dir).
//   name            = "logo"             # OPTIONAL: publish as
//                                        # <author>.<plugin>.<name> (US-5). The
//                                        # asset-namespace store is a LATER phase
//                                        # — this step PARSES + reports `name`
//                                        # but does not publish it (see .cpp).
//
// FAIL LOUD (AP14, logging.md structured KV) — a malformed declaration is a
// teaching error naming the bad row + the sidecar + the plugin, never a silent
// orphan, and one bad row never kills its siblings:
//   - an ambiguous/both-forms row (both `replaces` AND the pair) is REJECTED;
//   - a `replaces` (or pair) target that does NOT exist is REJECTED (the
//     missing-target teach — a mistyped vpath / a published name resolving to
//     nothing is the exact §4.1 + AP14 failure to catch).
//
// This file is the PARSE + VALIDATION step only. It produces a list of resolved
// declarations; BuildOverlayMap (asset_overlay.cpp) keys the overlay map by the
// declared TARGET and reuses the existing load-order winner/suppressed conflict
// report (§4.4). The sidecar parser does NOT touch the overlay map, the resolver
// hook, or load order — it returns data the map-build consumes.

namespace kcdx::asset_sidecar {

// What a single resolved declaration replaces — exactly one form.
enum class TargetKind {
    VanillaPath,    // `replaces = "<vanilla vpath>"` — keys the overlay map by
                    // that vpath; the resolver/FOpen lookup finds it (this step).
    PublishedName,  // `replaces = "<author>.<plugin>.<bare>"` — a cross-mod
                    // reference resolved by US-3 reference resolution, a LATER
                    // phase. NOT a vpath the engine requests, so a name-target
                    // overlay is NOT consumed by the FOpen/AdjustFileName lookup.
    PluginPathPair, // `replaces_plugin` + `replaces_path` — an unnamed cross-mod
                    // asset by path; resolves to the named plugin's asset's own
                    // vpath. Same cross-mod / later-phase consumption as a
                    // PublishedName when the target is another mod (see .cpp).
};

// One resolved sidecar declaration (a valid `[[asset]]` row whose target was
// confirmed to exist). `diskPath` is the absolute path of the declaring
// plugin's loose file that wins the target; `overlayKey` is the normalized
// vpath the overlay map is keyed by, present ONLY for a VanillaPath target this
// step routes into the resolver/FOpen lookup. A PublishedName / cross-mod
// PluginPathPair target carries no `overlayKey` (it is out of this step's
// resolver path — see .cpp scope-out) and is reported, not mapped.
struct Declaration {
    std::string owningPlugin;   // the declaring plugin's [plugin].name
    std::string owningAuthor;   // the declaring plugin's [plugin].author
    std::string diskPath;       // absolute disk path of the declaring file
    std::string sidecarPath;    // absolute path of the replaces.toml (for teach)
    TargetKind  kind;
    std::string target;         // the raw `replaces` string OR the published
                                // name; empty for a PluginPathPair
    std::string replacesPlugin; // PluginPathPair: the <author>.<plugin>
    std::string replacesPath;   // PluginPathPair: the path within that plugin
    std::string publishName;    // optional `name` (empty = not published)
    bool        routesToOverlay; // true ⇔ a VanillaPath target this step keys
                                 // into the overlay map; false ⇔ a cross-mod
                                 // reference scoped out of THIS step's lookup.
    std::string overlayKey;      // normalized vpath key (set ⇔ routesToOverlay)
};

// Scan one plugin's assets/ tree for `replaces.toml` sidecars, parse every
// `[[asset]]` row, validate each (loud-reject a malformed/ambiguous/missing-
// target row, naming the row + sidecar + plugin), and append every VALID
// resolved declaration to `out`. `vanillaExists` answers "is this a real
// vanilla vpath?" for the missing-target check (BuildOverlayMap supplies it —
// it owns the resolver leaves that know the vanilla file set; null disables the
// vanilla-existence check, used before the leaves are wired). Returns the count
// of valid declarations appended; rejected rows are logged + skipped, not
// counted. No-op (appends nothing) when the plugin ships no sidecar.
//
// PublishedName-existence: a published name's target resolves against the
// asset-namespace store, which is a LATER phase. Until it exists, a
// PublishedName / cross-mod PluginPathPair target's existence is NOT checkable
// here — such a row is parsed, reported as scoped-out, and NOT rejected for
// "missing target" (it would false-reject every valid cross-mod declaration).
// The vanilla-path existence check (the dominant US-1 case) IS enforced now.
size_t LoadDeclarationsFor(const plugins::LoadedPlugin& plugin,
                           bool (*vanillaExists)(const std::string& normVPath),
                           std::vector<Declaration>& out);

}  // namespace kcdx::asset_sidecar
