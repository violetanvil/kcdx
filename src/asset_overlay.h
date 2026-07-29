#pragma once

#include <windows.h>  // HANDLE — the overlay-ready cross-thread gate

#include <string>
#include <unordered_map>

// === Asset overlay — the two-hook asset-resolution seam (HOOK 1 + HOOK 2) ===
//
// kcdx absorbs pak mods. The seam is TWO coordinated engine hooks, both
// installed by Install() through the conflict engine (hook_chain::AddCEngine,
// Around mode — engine-stamped, the chain owns the MinHook detour), NOT raw
// MinHook: a production hook is engine-owned and must go through the chain so
// two installs on one site can't silently clobber each other (hook-engine.md).
// Each target is resolved by NAME — the engine carries the address AND the
// verified ABI; no RVA literal, no new seed row.
//
// HOOK 1 — the resolution DECISION. Replaces CCryPak::AdjustFileName (vtable
// slot 1, id 152) so kcdx owns which file a virtual path resolves to — for
// every asset class and both byte-lanes, above the sys_pakPriority existence
// gate (the pak/mount lane, replace-vanilla). Body: normalize the requested
// vpath -> overlay-map lookup. On a HIT, write the overlay's concrete disk path
// into the caller's outBuf (bounded to 2048, the engine's universal path cap)
// and return a char* to it. On a MISS, call through (stock resolution
// byte-identical; the original itself falls to the pak/disk/root leaves).
//
// HOOK 2 — the loose OPEN. An Around hook on CCryPak::FOpen (vtable slot 36,
// id 131, the engine-wide open-by-path that mints the file handle). On a HIT
// for a loose overlay, kcdx opens the file ITSELF (CRT _wfopen on the plugin's
// assets/ disk path) and returns that raw CRT FILE* as the open result —
// bypassing the engine's open + loose-search entirely. The engine's UNMODIFIED
// read family then serves kcdx's bytes (FRead routes any real heap FILE* to its
// OS arm — handle−1 ≫ pak-count). This serves the loose lane (add-new assets +
// the loose side of replace) without depending on the engine's loose-search
// (the layer the v1 path-redirect failed at). On a MISS, call through (the
// engine opens stock content normally). On a HIT whose open fails, fail loud
// and fall through to the original (never a silent broken handle). The two
// hooks key off the SAME overlay map.

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

// Build the overlay map from the discovered plugins IN UNIFIED LOAD ORDER,
// keyed by DECLARED TARGET (design asset-replacement.md §4.1: existence ≠
// replacement). For each plugin in load order, parses its co-located
// `replaces.toml` sidecars (asset_sidecar::LoadDeclarationsFor) and keys the
// map by each VANILLA-PATH declaration's declared target (normalized via
// NormalizeVPath, so the resolver's runtime lookup matches). A file present in
// assets/ with NO sidecar declaration replaces NOTHING — it does not enter this
// map (it is referenceable via a later phase's index, not an auto-applied
// overlay). Declarations are processed in load order, so the LOAD-ORDER WINNER
// occupies a target's slot and a later plugin declaring the SAME target is
// reported as suppressed (the established winner/suppressed conflict-report
// line, §4.4). A cross-mod / published-name target (a US-3/US-4 reference —
// `replaces` naming another mod's published name, or the
// `replaces_plugin`+`replaces_path` pair) is RESOLVED here (design §5.3):
// TWO-PASS — PASS 1 keys the vanilla-path declarations and builds a
// PublisherIndex of every published asset's serve-vpath; PASS 2 resolves each
// cross-mod target against that complete index to the vpath its asset serves at
// and keys the map by THAT vpath, with the same §4.4 load-order conflict. A
// cross-mod target resolving to no published asset is a LOUD report (AP14),
// never a silent drop. Emits one discovery summary at LOG_DEBUG so the build is
// observable in kcdx-dev.log.
//
// Call AFTER load-order resolution (g_plugins populated, load_order::Resolve
// run) and BEFORE the resolver hook would first read the map. Reuses
// load_order::Of for the order — does NOT re-derive it. Idempotent: rebuilds
// the map from scratch each call.
void BuildOverlayMap();

// Read-only access to the built map (for the resolver hook + the test-bar
// dev-log observation). Empty until BuildOverlayMap runs.
const OverlayMap& GetOverlayMap();

// === Overlay-ready cross-thread gate (worker → game thread) =================
//
// A dedicated happens-before edge between the WORKER (which builds the overlay
// map) and the GAME THREAD's filesystem seat (which builds the asset index off
// GetOverlayMap()). The seat-time asset-index build MUST NOT read an empty
// overlay map: the worker is fully independent of the main thread (parallel by
// default, one explicit wait point), so the seat could otherwise reach the
// CCryPak construct-store site and build the index BEFORE the worker finished
// BuildOverlayMap. This event is the gate that orders them — release on the
// worker's signal (overlay map fully built), acquire on the seat's wait. It is
// a SIBLING of mod_absorb's g_kcdxReadyEvent (the ctor-bracket gate), NOT a
// reuse: the seat gates on EXACTLY its dependency (the overlay map), so it
// unblocks as early as correctness allows rather than coupling to the later
// enabled-list build. Same mechanism — a manual-reset Win32 event, an
// atomic<HANDLE> with release/acquire ordering — never a timing margin. A
// cross-thread dependency is an explicit gate (signal + wait), never a
// wall-clock assumption about who finishes first.
//
// The event is OWNED by the asset_overlay unit (the producer that signals it),
// end-to-end: created here, signaled here right after BuildOverlayMap, and read
// (waited on) by the consumer through GetOverlayReadyEventHandle.

// Create the overlay-ready event. MUST be called once on the worker thread
// BEFORE InstallSeatingHook — the seat goes live the moment the seating hook is
// installed, and the game thread can reach the construct-store site shortly
// after; the handle must already exist so the seat's wait gate observes a
// non-null handle (the same discipline mod_absorb::CreateReadyEvent uses ahead
// of InstallCtorBracket). Idempotent — a second call returns immediately.
// Manual-reset, initially unsignaled; SignalOverlayReady signals it after
// BuildOverlayMap completes. Logs LOUD on CreateEventW failure and leaves the
// handle null (the seat's wait gate then falls back to the defensive path).
void CreateOverlayReadyEvent();

// SetEvent the overlay-ready event — the RELEASE edge. Called on the worker
// IMMEDIATELY after BuildOverlayMap() returns (the overlay map is fully built
// before this fires), so a consumer that acquired through the wait observes the
// complete map. Manual-reset: once signaled it stays signaled, so a seat that
// arrives after the signal returns from its wait immediately. Logs LOUD on a
// SetEvent failure or a null handle (CreateOverlayReadyEvent did not run /
// failed) — never silent. Idempotent in effect (manual-reset).
void SignalOverlayReady();

// Accessor for the overlay-ready event handle the seat waits on (the ACQUIRE
// side). Returns the handle CreateOverlayReadyEvent built, or null if it has
// not run / its CreateEventW failed (already logged loud). Crosses the
// worker→game boundary; the underlying storage is atomic<HANDLE> with
// release/acquire ordering.
HANDLE GetOverlayReadyEventHandle();

}  // namespace kcdx::asset_overlay
