// === Asset namespace — the runtime asset stores (RCU-snapshot, lock-free read)
//
// See asset_namespace.h for the full framing + the concurrency contract. This
// implements the two runtime stores behind atomic-pointer (RCU) snapshots:
//   * g_runtimeOverlay  — vpath -> winning file (register/replace write; the two
//     resolver hooks read lock-free, ALONGSIDE the build-time g_overlayMap);
//   * g_publishedNames  — packed <author>.<plugin>.<bare> -> { disk path, serve-
//     vpath } (declare writes; get_by_name reads the disk path, cross-mod replace
//     reads the serve-vpath — design §5.3).
//
// Memory ordering (concurrency.md §"Memory ordering"): the writer's pointer swap
// is store-RELEASE (publishes the new snapshot's fully-built contents); every
// reader's load is ACQUIRE (consumes them) — the happens-before edge a reader-
// consumes-writer-published store requires. NOT relaxed: a relaxed load could
// observe the new pointer before the snapshot's map contents are visible, a data
// race on the snapshot. Writer serialization (no write-lock, plain store-release,
// no CAS): the runtime-overlay store has ONE writer (register/replace, the Lua
// main thread). The published-name store has TWO writers — build-time PublishName
// (BuildOverlayMap, the worker thread) and runtime declare (Lua main thread) —
// serialized TEMPORALLY (worker discovery completes before the Lua entrypoints
// fire; see the header), so at most one is ever live and no two writes overlap.
// A CAS retry becomes required only if any two writes could overlap.

#include "asset_namespace.h"

#include <atomic>

#include "asset_overlay.h"  // NormalizeVPath — the SHARED key fold (one contract)
#include "log.h"

namespace kcdx::asset_namespace {

namespace {

// Stable log category for the runtime-store writes (greppable in kcdx-dev.log).
constexpr const char* kCat = "ASSET_RUNTIME";

// === The runtime-overlay store ============================================
//
// RCU: g_runtimeOverlay points at the CURRENT immutable snapshot. A writer
// builds a new snapshot and store-releases the pointer; readers load-acquire.
// Seeded with an empty snapshot at static-init so the resolver's first read
// (which can fire before any register/replace) is a clean MISS, never a null
// deref. The empty seed is retained for session like any superseded snapshot.
const RuntimeOverlayMap* MakeEmptyRuntimeOverlay() {
    return new RuntimeOverlayMap();  // retained for session (see header)
}
std::atomic<const RuntimeOverlayMap*> g_runtimeOverlay{MakeEmptyRuntimeOverlay()};

// === The published-name store =============================================
const PublishedNameMap* MakeEmptyPublishedNames() {
    return new PublishedNameMap();  // retained for session
}
std::atomic<const PublishedNameMap*> g_publishedNames{MakeEmptyPublishedNames()};

}  // namespace

// ---- Runtime overlay -------------------------------------------------------

void RegisterRuntimeOverlay(const std::string& vpath,
                            const std::string& diskPath,
                            const std::string& owningPlugin) {
    // SINGLE-WRITER read-copy-update (Lua main thread). Load the current snapshot
    // (acquire — this writer also reads the prior published contents to copy
    // them), copy it, apply the one change, then store-release the new pointer.
    // No compare-exchange loop: no second writer thread exists to race the swap
    // (if one is ever added, this becomes a CAS retry — see the header).
    const RuntimeOverlayMap* cur =
        g_runtimeOverlay.load(std::memory_order_acquire);

    // Build the NEW snapshot = copy-of-current + the one mutation. The current
    // snapshot is never mutated (a resolver thread may be reading it) — copy-on-
    // write. Normalize the vpath through the SHARED fold so a runtime lookup hits
    // regardless of the author's case/separator form (one key contract with the
    // build-time map + the resolver).
    RuntimeOverlayMap* next = new RuntimeOverlayMap(*cur);  // retained for session
    const std::string key = asset_overlay::NormalizeVPath(vpath);
    (*next)[key] = RuntimeOverlayEntry{owningPlugin, diskPath};

    // Publish: store-RELEASE so a resolver loading-acquire sees the new pointer
    // ONLY after the snapshot's contents are fully visible (the happens-before
    // edge). The superseded `cur` is retained for session (not freed — a reader
    // may still hold it; the leak is bounded by the few author writes).
    g_runtimeOverlay.store(next, std::memory_order_release);

    // One write event logged (NOT a hot path — register/replace are rare author
    // calls, so logging here is unrestricted; logging.md lifecycle-event line).
    LOG_DEBUG_KV(kCat, "runtime_overlay_registered",
                 kcdx::log::KV("vpath", key),
                 kcdx::log::KV("plugin", owningPlugin),
                 kcdx::log::KV("disk", diskPath),
                 kcdx::log::KV("entries",
                     static_cast<unsigned long long>(next->size())));
}

bool LookupRuntimeOverlay(const std::string& keyAlreadyNormalized,
                          std::string& outDisk,
                          std::string* outOwningPlugin) {
    // Lock-free, wait-free, allocation-free read on the hot resolver path. Load-
    // ACQUIRE the live snapshot pointer (consumes the writer's release publish),
    // read the never-mutated snapshot. The key is the resolver's already-computed
    // NormalizeVPath(pName) — no re-normalization on the hot path (memory.md).
    const RuntimeOverlayMap* snap =
        g_runtimeOverlay.load(std::memory_order_acquire);
    auto found = snap->find(keyAlreadyNormalized);
    if (found == snap->end()) return false;
    outDisk = found->second.diskPath;
    if (outOwningPlugin) *outOwningPlugin = found->second.owningPlugin;
    return true;
}

// ---- Published names -------------------------------------------------------

void PublishName(const std::string& packedName, const std::string& diskPath,
                 const std::string& serveVpath) {
    // TWO-WRITER (temporally serialized) read-copy-update, same RCU shape as
    // RegisterRuntimeOverlay. PublishName is called by BOTH build-time
    // BuildOverlayMap (the worker thread, at discovery) AND runtime declare (the
    // Lua main thread) — but never concurrently: worker discovery completes
    // before the Lua entrypoints fire (see the header). So the plain
    // load-copy-store-release is correct without a CAS (no two writes overlap).
    const PublishedNameMap* cur =
        g_publishedNames.load(std::memory_order_acquire);
    PublishedNameMap* next = new PublishedNameMap(*cur);  // retained for session
    (*next)[packedName] = PublishedNameEntry{diskPath, serveVpath};
    g_publishedNames.store(next, std::memory_order_release);

    LOG_DEBUG_KV(kCat, "published_name_declared",
                 kcdx::log::KV("name", packedName),
                 kcdx::log::KV("disk", diskPath),
                 kcdx::log::KV("serve_vpath", serveVpath),
                 kcdx::log::KV("entries",
                     static_cast<unsigned long long>(next->size())));
}

bool LookupPublishedName(const std::string& packedName, std::string& outDisk) {
    const PublishedNameMap* snap =
        g_publishedNames.load(std::memory_order_acquire);
    auto found = snap->find(packedName);
    if (found == snap->end()) return false;
    outDisk = found->second.diskPath;
    return true;
}

bool ResolvePublishedVpath(const std::string& packedName,
                           std::string& outVpath) {
    const PublishedNameMap* snap =
        g_publishedNames.load(std::memory_order_acquire);
    auto found = snap->find(packedName);
    if (found == snap->end()) return false;
    outVpath = found->second.serveVpath;
    return true;
}

}  // namespace kcdx::asset_namespace
