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
//   * the PUBLISHED-NAME store  — <author>.<plugin>.<name> -> { resolved disk
//     path, resolved SERVE-VPATH }, written by kcdx.assets.declare; the disk path
//     read by kcdx.assets.get_by_name (own + the §6 cross-plugin form), the
//     serve-vpath read by cross-mod replace (design §5.3). NOT consulted by the
//     resolver hooks — a published name is a code-side reference contract, not an
//     engine open-by-path target. The serve-vpath it carries IS what a cross-mod
//     replace keys the OVERLAY store by (a name resolves to the vpath its asset
//     serves at — the SAME index every shared name uses, naming-namespaces.md).
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
// WRITER SERIALIZATION: the published-name store has TWO writers, serialized
// TEMPORALLY (never concurrent), so the plain read-copy-update + store-release
// is correct without a CAS loop:
//   1. BUILD-TIME — BuildOverlayMap (asset_overlay.cpp) calls PublishName for
//      every sidecar `name` on the plugin-loader WORKER thread, at discovery.
//   2. RUNTIME — register/declare/replace run on the Lua MAIN thread (plugin.lua
//      + every file it require()s execute on the single Lua VM thread; the author
//      verbs only QUEUE during entrypoint parse, then run post-entrypoint).
// These never overlap: worker discovery (writer 1) COMPLETES before the Lua
// entrypoints fire (writer 2) — the lifecycle orders them (plugin_loader runs
// discovery + BuildOverlayMap, THEN the Lua entrypoints). So at most one writer
// is ever live; the read-copy-update needs no write-lock and the swap is a plain
// store-release, not a CAS. (If a build-time and a runtime write could ever
// overlap — e.g. a runtime write fired before discovery completed — the swap
// would need a compare-exchange retry loop; flagged at each writer below.)
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

// ---- The boot-opened-vpath set: the boot-window teaching-warn support --------
//
// The boot-cached-asset problem: the engine opens a boot/menu asset (e.g.
// `Libs/UI/Textures/KCDLogo.dds`) EXACTLY ONCE per session, inside `CSystem::Init`
// — the same phase that creates the engine's Lua VM. The author's
// `kcdx.assets.register`/`replace` runs in `plugin.lua`, which fires AFTER that VM
// exists (`hooks.cpp` first-update-tick `RunAll`) — i.e. AFTER the boot open. So a
// runtime overlay (take-effect="thereafter") is never consulted for a boot asset
// the engine opened once and cached. A Lua runtime replace of such a vpath is a
// SILENT NON-SERVE today (the boot path is the declarative sidecar; a Lua-runtime
// boot serve depends on a Lua VM that comes up before the boot open — a later kcdx
// release). This set lets RegisterRuntimeOverlay detect that case and emit a
// one-time teaching warn instead of a silent no-op (a loud teach, never a silent
// drop).
//
// === Concurrency: the BOUNDED-BOOT-WINDOW temporal-serialization model =========
//
// This is NOT an always-on set — it is WRITTEN only during the boot window (before
// the engine's Lua VM is captured) and READ only after it (the warn-check runs from
// RegisterRuntimeOverlay, which an author calls from plugin.lua — strictly post-VM).
// The set's lifetime is two non-overlapping phases the VM-up flag (`g_vmUp`)
// orders:
//
//   1. WRITE phase (VM NOT up): the resolver (HOOK 1 / HOOK 2, on the engine's
//      file-I/O threads) calls RecordBootOpen(key) for every vpath the engine
//      resolves/opens. Multiple engine I/O threads can open concurrently, so the
//      insert is MUTEX-guarded — but ONLY in this window (the lock cost is paid on
//      a path that runs strictly before the VM/plugins/post-boot resolver storm).
//   2. FREEZE: NotifyVmReady() flips `g_vmUp` to true (RELEASE). After this the set
//      is NEVER written again (RecordBootOpen short-circuits on the atomic BEFORE
//      the lock — see below), so it is effectively IMMUTABLE.
//   3. READ phase (VM up): WasBootOpened(vpath) reads the now-frozen set. The
//      caller (RegisterRuntimeOverlay → author plugin.lua) runs strictly post-VM,
//      so the set is no longer mutating; no read-lock is needed PROVIDED the
//      reader establishes happens-before with the freeze (it acquire-loads
//      `g_vmUp` first — the release/acquire pair on `g_vmUp` is the edge that makes
//      every WRITE-phase insert visible to the READ-phase reader, concurrency.md
//      §"Memory ordering"). NOT relaxed: a relaxed read could observe the frozen
//      flag without the set's inserts being visible — a data race on the set.
//
// This mirrors the runtime-overlay/published-name stores' TEMPORAL serialization
// (writes-then-reads, no concurrent overlap) — here the serialization point is the
// VM-up freeze rather than the build-then-runtime lifecycle. The hot-path constraint
// is the load-bearing one: post-boot the resolver fires CONSTANTLY, so RecordBootOpen
// must early-return on a single relaxed-vs-acquire atomic load with NO lock and NO
// allocation once the VM is up (memory.md hot-path — the set is never touched again).

// Record a vpath the engine opened during the BOOT WINDOW (resolver-side). While
// the VM is NOT up: ASCII-fold the key via the shared NormalizeVPath caller-side
// (the caller passes the already-normalized resolver key) and insert it under the
// boot-window mutex. Once the VM is up: `g_vmUp` short-circuits BEFORE the lock — a
// single ACQUIRE atomic load + branch, no lock, no insert, no allocation (the
// resolver hot path stays allocation-free post-boot, memory.md). `keyNormalized`
// is the resolver's already-computed NormalizeVPath(pName) — no re-normalization on
// the hot path. Safe to call from multiple engine I/O threads during the boot
// window (mutex-guarded); a no-op after the freeze.
void RecordBootOpen(const std::string& keyNormalized);

// Flip the VM-up flag (RELEASE) — FREEZES the boot-opened set (RecordBootOpen is a
// no-op thereafter; the set is never written again). Called once when the engine's
// Lua VM is captured (`hooks.cpp` first-update-tick latch, after `RunAll`). The
// release store is the happens-before publish of every WRITE-phase insert; a reader
// acquire-loading `g_vmUp` in WasBootOpened consumes them. Idempotent + one-shot:
// a redundant call is harmless (the flag stays true; the set stays frozen) — but
// the caller already guards it behind a one-shot latch.
void NotifyVmReady();

// Was `vpath` opened during the boot window? Reads the (frozen) boot-opened set.
// Caller MUST run post-VM (RegisterRuntimeOverlay does — it is reached only from an
// author plugin.lua call, strictly after NotifyVmReady). Acquire-loads `g_vmUp`
// FIRST to establish happens-before with the freeze (so every WRITE-phase insert is
// visible); if the VM is somehow not yet up the set may still be mutating, so this
// returns false (defensive — never a torn read of a live-mutating set). `vpath` is
// raw (un-normalized): normalized HERE via the shared NormalizeVPath so the lookup
// folds identically to the resolver's RecordBootOpen key. No lock on the post-freeze
// read — the freeze established the happens-before, the set is immutable thereafter.
bool WasBootOpened(const std::string& vpath);

// ---- The published-name store: declare WRITES, get_by_name + cross-mod READ ---
//
// declare publishes <author>.<plugin>.<bare> -> { resolved disk path, resolved
// SERVE-VPATH }; get_by_name resolves a published name (own, or the §6 cross-
// plugin form) back to the DISK PATH; cross-mod replace resolves it to the
// SERVE-VPATH (design §5.3). This store is NOT consulted by the resolver hooks —
// it is a code-side reference contract. It uses the SAME RCU-snapshot shape as
// the runtime-overlay store (the writer is the same single Lua main thread; reads
// are from the binder, also main thread today, but the RCU shape keeps it uniform
// + future-thread-safe).

// One published-name slot: the loadable disk path get_by_name returns (US-3,
// unchanged) AND the resolved SERVE-VPATH cross-mod replace keys the overlay
// store by (design §5.3 — a published name resolves to the vpath its asset
// SERVES AT). serveVpath is NORMALIZED via asset_overlay::NormalizeVPath (the
// SAME fold the overlay map + the resolver use — one key contract across all
// stores), so a cross-mod replace keys the overlay store with a key the resolver
// will match when the engine opens that vpath. For a PUBLISH-AND-REPLACE asset
// the serve-vpath is the vanilla vpath it replaces; for a PURE ADD-NEW publish it
// is the asset's own add-new vpath (its path relative to assets/) — the binder /
// build-time path derives it and hands the normalized form here.
struct PublishedNameEntry {
    std::string diskPath;    // loadable disk path (get_by_name — US-3)
    std::string serveVpath;  // normalized serve-vpath (cross-mod replace — §5.3)
};

// The published-name snapshot: packed "<author>.<plugin>.<bare>" -> entry.
using PublishedNameMap =
    std::unordered_map<std::string, PublishedNameEntry>;

// Publish a name (kcdx.assets.declare, the sidecar `name`). Build-new + release-
// swap: copy the current snapshot, insert/overwrite the packed-key -> entry slot,
// publish. `packedName` is the full "<author>.<plugin>.<bare>" the binder built
// from the caller's identity + the bare name. `diskPath` is the resolved loadable
// path of the declared `file` (resolved by ResolveAssetPath in the binder, like
// get_by_path). `serveVpath` is the resolved serve-vpath (already NORMALIZED by
// the caller via asset_overlay::NormalizeVPath — the vanilla vpath the published
// asset replaces, or its own add-new vpath; design §5.3). Single-writer — no
// write-lock.
void PublishName(const std::string& packedName, const std::string& diskPath,
                 const std::string& serveVpath);

// Resolve a published name to its disk path (kcdx.assets.get_by_name + the §6
// cross-plugin form). Loads the published-name snapshot acquire, looks up the
// packed key. Returns true + `outDisk` on HIT, false on MISS (the binder turns a
// MISS into the teaching error). `packedName` is the full
// "<author>.<plugin>.<bare>" the binder built.
bool LookupPublishedName(const std::string& packedName, std::string& outDisk);

// Resolve a published name to its SERVE-VPATH for cross-mod-replace keying
// (design §5.3 — hop 1 of the two-hop cross-mod replace: name -> serve-vpath,
// then key the overlay store by it). Loads the published-name snapshot acquire,
// looks up the packed key, returns the NORMALIZED serve-vpath via `outVpath`.
// Returns true on HIT, false on MISS (the caller turns a MISS into the AP14
// teaching error naming the unresolved name). `packedName` is the full
// "<author>.<plugin>.<bare>" the caller built. Distinct accessor from
// LookupPublishedName (disk path) so a cross-mod replace keys by the serve-vpath
// without also resolving the disk path it does not need.
bool ResolvePublishedVpath(const std::string& packedName, std::string& outVpath);

}  // namespace kcdx::asset_namespace
