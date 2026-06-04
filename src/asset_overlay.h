#pragma once

#include <string>
#include <unordered_map>

// === Asset overlay — production HOOK 1 on the game's resolution decision ====
//
// kcdx absorbs pak mods. HOOK 1 of the two-hook asset-resolution seam: kcdx
// REPLACES CCryPak::AdjustFileName (vtable slot 1, the resolution-DECISION
// root) so kcdx owns which file a virtual path resolves to — for every asset
// class and both byte-lanes, above the sys_pakPriority existence gate.
//
// The hook installs through the conflict engine (hook_chain::AddCEngine, Around
// mode — engine-stamped, the chain owns the MinHook detour), NOT raw MinHook: a
// production hook is engine-owned and must go through the chain so two installs
// on one site can't silently clobber each other (hook-engine.md). The target is
// resolved by NAME ("CCryPak_AdjustFileName", id 152) — the engine carries the
// address AND the verified ABI; no RVA literal, no new seed row.
//
// The body: normalize the requested vpath -> overlay-map lookup. On a HIT, write
// the overlay's concrete disk path into the caller's outBuf (bounded to 2048,
// the engine's universal path cap) and return a char* to it — kcdx decides its
// overlay wins. On a MISS, call through to the original (stock resolution
// byte-identical; the original itself falls to the pak/disk/root leaves). HOOK 2
// (the own-FILE* loose open) is a later step; HOOK 1 ships the DECISION + write.

namespace kcdx::asset_overlay {

// Install the production CCryPak::AdjustFileName resolver hook on the conflict
// engine's chain (hook_chain::AddCEngine, Around). Resolves the target by
// canonical name; must run AFTER RefdbOpened (the name resolution reads the
// cache built in refdb::Open()). Idempotent. Returns true on success.
bool Install();

// === Overlay map — virtual-asset-path -> winning loose-file on disk =========
//
// The lookup the resolver hook consults: the engine asks AdjustFileName to
// resolve a virtual asset path (pName), and the map says whether a loose plugin
// file overrides the pak-resident asset, and which file.
//
// The KEY is a NORMALIZED virtual asset path: case-insensitive (ASCII-
// lowercased) and slash-insensitive ('\\' -> '/'), so the resolver's lookup
// (which normalizes the engine's pName the same way via NormalizeVPath) hits the
// map. The engine's open-by-path argument is OBSERVED to arrive mixed-case with
// backslashes; that this exact fold matches its runtime form is a checkable
// unknown HOOK 1's live acceptance confirms (assumed here, verified by the
// overlay-HIT log line firing on an overlaid path).

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
// is the single definition of the map's key normalization — the resolver reuses
// it, never re-derives it.
std::string NormalizeVPath(const std::string& vpath);

// Build the overlay map from the discovered plugins IN UNIFIED LOAD ORDER.
// Walks each plugin's assetsEntrypointRel dir (relative to its folderPath);
// each loose file's path-relative-to-the-assets-root is its virtual asset
// path. Insertions happen in load order, so the LOAD-ORDER WINNER occupies the
// slot and any later plugin overlaying the same vpath is reported as suppressed
// (a conflict-report log line, winner/suppressed shape). A vpath escaping the
// assets root ('..' traversal) is logged + skipped (that file only, not the
// plugin). Emits one discovery summary (entry count + per-entry vpath ->
// winner) at LOG_DEBUG so the build is observable in kcdx-dev.log.
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
