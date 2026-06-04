#pragma once

#include <string>
#include <unordered_map>

// === Asset namespace — the RUNTIME asset stores (register/replace + declare/
//     get_by_name), distinct from the build-time overlay map in asset_overlay
// ============================================================================
//
// asset_overlay.cpp owns the BUILD-TIME overlay map (g_overlayMap): built once
// at discovery, never mutated after, read lock-free by the two resolver hooks.
// This unit owns the RUNTIME stores the four store-dependent kcdx.assets.* verbs
// write at author-runtime (design asset-replacement.md §5.1):
//
//   * the RUNTIME-OVERLAY store  — vpath -> winning loose-file on disk, written
//     by kcdx.assets.register / .replace; the resolver consults it ALONGSIDE the
//     build-time map (on a build-time MISS) so a runtime add/replace serves the
//     same way a build-time overlay does. Take-effect = "thereafter" (§3 US-6):
//     a register/replace affects assets opened AFTER the call; no re-resolve.
//   * the PUBLISHED-NAME store  — <author>.<plugin>.<name> -> resolved disk
//     path, written by kcdx.assets.declare; read by kcdx.assets.get_by_name (own
//     + the §6 cross-plugin form). NOT consulted by the resolver — a published
//     name is a code-side reference contract, not an engine open-by-path target.
//
// === Concurrency: lock-free RCU-snapshot reads (design §5.1, concurrency.md) ==
//
// The runtime-overlay store is SHARED MUTABLE STATE reached ACROSS a unit
// boundary: WRITTEN from the Lua main thread (register/replace — a one-off
// author call in plugin.lua), READ by HOOK 1 (AdjustFileNameResolver) + HOOK 2
// (FOpenLooseOverlay) on the engine's file-I/O threads. The sanctioned shape
// (design §5.1 settled it; concurrency.md atomics-first/locks-last) is an
// atomic-pointer (RCU) snapshot:
//
//   * the store is an IMMUTABLE Snapshot behind std::atomic<const Snapshot*>;
//   * a WRITER builds a NEW snapshot (copy the old + apply the one change) and
//     swaps the pointer with memory_order_RELEASE — publishing the new
//     snapshot's fully-constructed contents;
//   * the hot RESOLVER reads load-acquire (memory_order_ACQUIRE) and reads a
//     never-mutated snapshot — wait-free, allocation-free on the read path
//     (memory.md hot-path). The release/acquire pair is the happens-before edge
//     (concurrency.md §"Memory ordering"): the writer publishes, the reader
//     consumes; NOT relaxed.
//
// WRITER SERIALIZATION: register/declare/replace run on the Lua MAIN thread
// (plugin.lua + every file it require()s execute on the single Lua VM thread —
// there is no second writer thread). A single writer needs no write-lock for the
// read-copy-update. The functions here are documented + built for that single-
// writer assumption; a CAS loop is NOT used because no second writer exists to
// race the swap. (If a second writer thread is ever introduced, the swap must
// become a compare-exchange retry loop — flagged at each writer below.)
//
// OLD-SNAPSHOT RECLAMATION — retain-for-session (the bounded, sanctioned choice):
// when the writer swaps in a new snapshot, a resolver thread may still be reading
// the OLD one (it cannot be freed immediately — a reader could be mid-read). The
// reclamation choice here is RETAIN-FOR-SESSION: the superseded snapshot is NOT
// freed; it leaks until process exit. This is bounded and tiny: register/declare/
// replace are RARE one-off author calls (a handful per session at most — the
// programmatic peers of a per-asset sidecar, §4.2), so the leaked snapshots are
// a few small maps held to exit, NOT an unbounded accumulation. An epoch/hazard-
// pointer reclamation scheme would add real complexity to free a few-KB bounded
// leak — over-engineering for this write cadence (design §5.1 names retain-for-
// session as acceptable given the few author writes). The retained snapshots are
// owned by a process-lifetime registry (RetainSnapshot below) so they are not a
// leak the tooling flags as lost — they are deliberately retained.

namespace kcdx::asset_namespace {

// One runtime-overlay slot: the winning loose-file on disk for a vpath, plus the
// plugin that registered/replaced it (for the observability log line + a future
// conflict report). MIRRORS asset_overlay::OverlayEntry's shape so the resolver
// serves a runtime HIT exactly as a build-time HIT (HOOK 1 writes diskPath to
// outBuf; HOOK 2 opens diskPath and returns its own FILE*).
struct RuntimeOverlayEntry {
    std::string owningPlugin;  // the plugin that registered/replaced this vpath
    std::string diskPath;      // absolute disk path of the file to serve
};

// The runtime-overlay snapshot: NORMALIZED-vpath -> winning RuntimeOverlayEntry.
// IMMUTABLE once published behind the atomic pointer — a writer never mutates a
// live snapshot; it builds a new one (copy + change) and swaps the pointer. The
// key is normalized by asset_overlay::NormalizeVPath (the SAME fold the build-
// time map + the resolver use — one key contract across both stores).
using RuntimeOverlayMap =
    std::unordered_map<std::string, RuntimeOverlayEntry>;

// ---- The runtime-overlay store: register / replace WRITE, resolver READS -----

// Register a runtime vpath -> diskPath overlay (kcdx.assets.register, and the
// keyed write behind kcdx.assets.replace). Build-new + atomic release-swap (RCU):
// copy the current snapshot, insert/overwrite the (NormalizeVPath(vpath) ->
// {owningPlugin, diskPath}) slot, publish the new snapshot. Take-effect =
// "thereafter": an asset opened AFTER this call resolves the new overlay; an
// already-open handle is NOT re-resolved (§3 US-6). Single-writer (Lua main
// thread) — no write-lock; the swap is a plain store-release. Returns nothing —
// the verb's own validation (file resolves, vpath non-empty) happens in the
// binder BEFORE this is called; this is the pure store mutation.
//
// `vpath` is the raw (un-normalized) virtual path the author named for register,
// OR the target vpath for replace — normalized HERE via the shared NormalizeVPath
// so a runtime lookup hits regardless of the author's case/separator form.
void RegisterRuntimeOverlay(const std::string& vpath,
                            const std::string& diskPath,
                            const std::string& owningPlugin);

// The resolver's lock-free read: look up a NORMALIZED vpath in the live runtime-
// overlay snapshot. Loads the atomic pointer with memory_order_ACQUIRE (the
// happens-before consume of the writer's release publish), reads the never-
// mutated snapshot, and returns the winning diskPath via `outDisk` (true on HIT,
// false on MISS). Wait-free, allocation-free — safe to call from the hot resolver
// hooks (HOOK 1 / HOOK 2). `keyAlreadyNormalized` must be the resolver's already-
// computed NormalizeVPath(pName) so the hot path does not re-normalize.
// `outOwningPlugin` (optional) receives the winning plugin name for the one-shot
// observability log line; pass nullptr to skip it.
bool LookupRuntimeOverlay(const std::string& keyAlreadyNormalized,
                          std::string& outDisk,
                          std::string* outOwningPlugin);

// ---- The published-name store: declare WRITES, get_by_name READS -------------
//
// declare publishes <author>.<plugin>.<bare> -> a resolved disk path; get_by_name
// resolves a published name (own, or the §6 cross-plugin form) back to that path.
// This store is NOT consulted by the resolver hooks — it is a code-side reference
// contract. It uses the SAME RCU-snapshot shape as the runtime-overlay store (the
// writer is the same single Lua main thread; reads are from the binder, also main
// thread today, but the RCU shape keeps it uniform + future-thread-safe).

// The published-name snapshot: packed "<author>.<plugin>.<bare>" -> disk path.
using PublishedNameMap = std::unordered_map<std::string, std::string>;

// Publish a name (kcdx.assets.declare). Build-new + release-swap: copy the
// current snapshot, insert/overwrite the packed-key -> diskPath slot, publish.
// `packedName` is the full "<author>.<plugin>.<bare>" the binder built from the
// caller's identity + the bare name. `diskPath` is the resolved loadable path of
// the declared `file` (resolved by ResolveAssetPath in the binder, like
// get_by_path). Single-writer — no write-lock.
void PublishName(const std::string& packedName, const std::string& diskPath);

// Resolve a published name to its disk path (kcdx.assets.get_by_name + the §6
// cross-plugin form). Loads the published-name snapshot acquire, looks up the
// packed key. Returns true + `outDisk` on HIT, false on MISS (the binder turns a
// MISS into the teaching error). `packedName` is the full
// "<author>.<plugin>.<bare>" the binder built.
bool LookupPublishedName(const std::string& packedName, std::string& outDisk);

}  // namespace kcdx::asset_namespace
