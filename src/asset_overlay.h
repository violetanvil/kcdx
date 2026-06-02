#pragma once

#include <string>
#include <unordered_map>

// === Asset overlay — production hook on the game's pak resolver ======
//
// Phase 8.5: kcdx absorbs pak mods. The engine hooks CCryPak::FOpen (the
// engine-wide open-by-path resolver) so a virtual-path open can be redirected
// to a loose overlay file before the pak-resident asset is read.
//
// This module lands the production hook SITE through the conflict engine
// (hook_chain::AddCEngine — engine-stamped, the chain owns the MinHook detour),
// NOT raw MinHook: the diagnostic FOPEN probe used MinHook directly because it
// was throwaway; a production hook is engine-owned and must go through the
// chain so two installs on one site can't silently clobber each other
// (hook-engine.md). The runtime unknowns this hook rests on were resolved by
// the now-removed FOPEN probe: CCryPak::FOpen fires for asset READs, and a
// pName rewrite in a body detour OVERRIDES a pak-resident asset end-to-end
// (_research/probe-archive/fopen-override.md).
//
// The redirect DECISION (overlay-map lookup + pName rewrite) is a later step;
// this step ships a PASS-THROUGH body (call original unchanged) so the hook
// site is in place and boot-safe.
//
// Target resolved by NAME: "CCryPak_FOpen" (kcdx_id 131) — the common named-
// target path; the engine carries the address AND the verified ABI. No RVA, no
// new seed row.

namespace kcdx::asset_overlay {

// Install the production CCryPak::FOpen overlay hook on the conflict engine's
// chain (hook_chain::AddCEngine). Resolves the target by canonical name; must
// run AFTER RefdbOpened (the name resolution reads the cache built in
// refdb::Open()). Idempotent. Returns true on success.
bool Install();

// === Overlay map — virtual-asset-path -> winning loose-file on disk =========
//
// The lookup the resolver hook (a later step) consults: the engine asks for a
// virtual asset path (CCryPak::FOpen's pName), and the map says whether a
// loose plugin file overrides the pak-resident asset, and which file.
//
// The KEY is a NORMALIZED virtual asset path: case-insensitive (ASCII-
// lowercased) and slash-insensitive ('\\' -> '/'), so the resolver's lookup
// (which normalizes the engine's open-by-path argument the same way via
// NormalizeVPath) hits the map. The engine's open-by-path argument is OBSERVED
// to arrive mixed-case with backslashes; that this exact fold matches its
// runtime form is a checkable unknown the resolver step owes a probe to
// confirm before trusting the lookup (assumed here, not yet verified live).

// One overlay slot: the load-order WINNER for a given virtual path.
struct OverlayEntry {
    std::string owningPlugin;  // [plugin].name of the plugin that owns this slot
    std::string diskPath;      // absolute path of the loose file on disk
};

// Virtual-asset-path (NORMALIZED — see NormalizeVPath) -> winning OverlayEntry.
using OverlayMap = std::unordered_map<std::string, OverlayEntry>;

// Normalize a virtual asset path for use as / lookup into the overlay map:
// lowercase + backslashes-to-forward-slashes. The resolver hook normalizes the
// engine's pName the SAME way so its lookup matches a key inserted here. This
// is the single definition of the map's key normalization — the resolver step
// reuses it, never re-derives it.
std::string NormalizeVPath(const std::string& vpath);

// Build the overlay map from the discovered plugins IN UNIFIED LOAD ORDER.
// Walks each plugin's assetsEntrypointRel dir (relative to its folderPath);
// each loose file's path-relative-to-the-assets-root is its virtual asset
// path. Insertions happen in load order, so the LOAD-ORDER WINNER occupies the
// slot and any later plugin overlaying the same vpath is reported as suppressed
// (a conflict-report log line, winner/suppressed shape). A vpath escaping the
// assets root ('..' traversal) is logged + skipped (that file only, not the
// plugin). Emits one discovery summary (entry count + per-entry vpath ->
// winner) at LOG_DEBUG so the step is observable in kcdx-dev.log.
//
// Call AFTER load-order resolution (g_plugins populated, load_order::Resolve
// run) and BEFORE the resolver hook would first read the map. Reuses
// load_order::Of for the order — does NOT re-derive it. Idempotent: rebuilds
// the map from scratch each call.
void BuildOverlayMap();

// Read-only access to the built map (for the resolver hook + the test-bar
// dev-log observation). Empty until BuildOverlayMap runs.
const OverlayMap& GetOverlayMap();

}  // namespace kcdx::asset_overlay
