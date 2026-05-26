// kcdx::modification_inventory — see modification_inventory.h for WHY this
// module reads the LIVE modification sources (hook_chain::g_chains + the
// RegisterModification'd fixed installs) and NOT the dead hook_engine legacy
// vectors.

#include "modification_inventory.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf (cached summary)
#include <mutex>
#include <vector>

#include "hook_chain.h"  // GetAllChainTargets() — live plugin-hook targets

namespace kcdx::modification_inventory {

namespace {

// One registered fixed install (lifecycle / engine / probe). hook_chain
// entries do NOT live here — they are enumerated live from g_chains.
struct Entry {
    uintptr_t   va;
    Category    category;
    const char* name;  // string literal; process-lifetime
};

// The registry of fixed installs. Grows once per successful install at boot /
// arm time; never shrinks (hooks live for the session). Guarded by g_mu.
std::vector<Entry> g_registered;
std::mutex         g_mu;

// Cached, pre-formatted inventory summary string. Fixed-size static buffer so
// the SEH crash handler can dump it verbatim with zero allocation, no lock, no
// iteration. Refreshed by LogInventory() (boot + each save-load start, both
// allocation-safe contexts). The crash guard reads it via LastInventorySummary;
// the load-time Info line and the crash-time line therefore share content.
char   g_lastSummary[256] = "(inventory not yet captured)";
size_t g_lastTotal        = 0;

// Order-independent fingerprint of a set of target VAs. XOR-fold with a
// per-value mix so two runs with the same set (in any order) produce the same
// value, and a changed/added/removed target flips it. XOR is commutative →
// order-independent; the mix de-symmetrizes so identical-low-bits VAs don't
// collapse and a*2 can't cancel.
inline uint64_t FoldTargetVa(uint64_t acc, uintptr_t va) {
    uint64_t v = static_cast<uint64_t>(va);
    v ^= (v >> 17);
    v *= 0x9E3779B97F4A7C15ull;  // fibonacci hashing constant
    return acc ^ v;
}

const char* CategoryToken(Category c) {
    switch (c) {
        case Category::PluginHook: return "plugin_hook";
        case Category::Engine:     return "engine";
        case Category::Lifecycle:  return "lifecycle";
        case Category::Probe:      return "probe";
    }
    return "?";
}

// --- Fire breadcrumb ring (see modification_inventory.h) -------------------
//
// Fixed-size, process-lifetime storage. The hot path writes; the crash guard
// (and the cap-47 self-test) read. NO lock guards this: RecordFire is a
// relaxed atomic bump + four plain stores; LastFires tolerates a torn read in
// the crash path (worst case one slightly-stale entry). The slot fields are
// plain (non-atomic) — a concurrent dispatcher and a crash-time read on
// another thread can race a single slot, but the dump is best-effort
// diagnostics, not a synchronized handoff. Keeping the stores plain is what
// makes the record path zero-overhead (no per-store atomic).
FireRecord            g_fireRing[kFireRingSize];
std::atomic<uint64_t> g_fireSeq{0};  // monotonic; next-slot = seq % kFireRingSize

}  // namespace

void RegisterModification(uintptr_t targetVa, Category category,
                          const char* name) {
    std::lock_guard<std::mutex> lock(g_mu);
    // Idempotent per (va, category): probes are retried / idempotent; a
    // double registration of the same target+category must not double-count.
    for (const auto& e : g_registered) {
        if (e.va == targetVa && e.category == category) return;
    }
    g_registered.push_back({targetVa, category, name ? name : "?"});
}

void LogInventory(log::Level summaryLevel) {
    // Pull the live plugin-hook targets (hook_chain::g_chains). Snapshot under
    // hook_chain's own mutex — GetAllChainTargets() returns by value. Each
    // record now carries the owning plugin + hook name (not just the VA) so
    // the per-target DETAIL names WHO owns the chain.
    std::vector<hook_chain::ChainTarget> chainTargets =
        hook_chain::GetAllChainTargets();

    // Snapshot the registered fixed installs under our mutex (copy out so the
    // log/iterate work below doesn't hold the lock).
    std::vector<Entry> registered;
    {
        std::lock_guard<std::mutex> lock(g_mu);
        registered = g_registered;
    }

    // Per-category counts. plugin_hook = the chain targets; the rest come from
    // the registered set tagged by its install site.
    size_t nPluginHook = chainTargets.size();
    size_t nEngine     = 0;
    size_t nLifecycle  = 0;
    size_t nProbe      = 0;
    for (const auto& e : registered) {
        switch (e.category) {
            case Category::PluginHook: ++nPluginHook; break;  // (none expected here)
            case Category::Engine:     ++nEngine;     break;
            case Category::Lifecycle:  ++nLifecycle;  break;
            case Category::Probe:      ++nProbe;      break;
        }
    }
    const size_t total = nPluginHook + nEngine + nLifecycle + nProbe;

    // Order-independent fingerprint over ALL target VAs (both sources).
    uint64_t fp = 0;
    for (const auto& t : chainTargets) { if (t.va) fp = FoldTargetVa(fp, t.va); }
    for (const auto& e : registered)   { if (e.va) fp = FoldTargetVa(fp, e.va); }

    // Refresh the cached pre-formatted string FIRST (so even if the log calls
    // below were skipped, the crash buffer is current). Fixed buffer, snprintf
    // — no allocation. Stable + greppable.
    std::snprintf(g_lastSummary, sizeof(g_lastSummary),
                  "total=%zu plugin_hook=%zu engine=%zu lifecycle=%zu "
                  "probe=%zu fingerprint=0x%016llX",
                  total, nPluginHook, nEngine, nLifecycle, nProbe,
                  static_cast<unsigned long long>(fp));
    g_lastTotal = total;

    // SUMMARY at the caller-chosen severity (Info at boot/load → always-on,
    // diffable across builds by eye).
    log::EmitEngineKV(summaryLevel, "INVENTORY", "summary",
        { log::KV("total",       (unsigned long long)total),
          log::KV("plugin_hook", (unsigned long long)nPluginHook),
          log::KV("engine",      (unsigned long long)nEngine),
          log::KV("lifecycle",   (unsigned long long)nLifecycle),
          log::KV("probe",       (unsigned long long)nProbe),
          log::KV("fingerprint", (unsigned long long)fp) });

    // Per-target DETAIL at Debug (dev-only firehose): each VA + category +
    // OWNING plugin + hook name. Names WHO owns the chain (plugin + hook),
    // not the generic surface name "kcdx.hook" — the attribution gap PROBE I
    // flagged (docs/known-issues/save-load crash 0xC8 ...). An empty chain
    // (no live owner) reports "" for plugin/hook; the VA is still emitted.
    for (const auto& t : chainTargets) {
        if (!t.va) continue;
        LOG_DEBUG_KV("INVENTORY", "target",
            log::KV::BareStr("category", CategoryToken(Category::PluginHook)),
            log::KV("va",     (void*)t.va),
            log::KV::BareStr("plugin", (t.pluginName && t.pluginName[0])
                                           ? t.pluginName : "(none)"),
            log::KV::BareStr("hook",   (t.hookName && t.hookName[0])
                                           ? t.hookName : "(none)"));
    }
    for (const auto& e : registered) {
        if (!e.va) continue;
        LOG_DEBUG_KV("INVENTORY", "target",
            log::KV::BareStr("category", CategoryToken(e.category)),
            log::KV("va",   (void*)e.va),
            log::KV::BareStr("name", e.name));
    }
}

const char* LastInventorySummary() {
    return g_lastSummary;
}

size_t LastTotalModifications() {
    return g_lastTotal;
}

void RecordFire(uintptr_t targetVa, const char* pluginName,
                const char* hookName) {
    // Reserve this fire's sequence number. fetch_add returns the PRE-increment
    // value, so seq 0 is the very first fire — and an untouched slot keeps its
    // zero-initialized seq==0. To keep "seq==0 means empty" unambiguous, the
    // recorded seq is 1-based (the first fire stores seq=1). relaxed: no
    // ordering needed against other state; this is a self-contained counter.
    const uint64_t n = g_fireSeq.fetch_add(1, std::memory_order_relaxed) + 1;
    FireRecord& slot = g_fireRing[n % kFireRingSize];
    // Plain stores — zero allocation, no lock, no log. Order: write the
    // payload, then publish seq last so a crash-time reader that sees the new
    // seq also sees the matching payload (best-effort; not a strict barrier).
    slot.targetVa   = targetVa;
    slot.pluginName = pluginName;
    slot.hookName   = hookName;
    slot.seq        = n;
}

unsigned LastFires(FireRecord* out, unsigned cap) {
    if (!out || cap == 0) return 0;
    // Snapshot the counter. Anything written after this point may or may not
    // be seen — fine for a crash-time / boot-time diagnostic.
    const uint64_t total = g_fireSeq.load(std::memory_order_relaxed);
    if (total == 0) return 0;  // nothing recorded yet
    // Walk newest-first. Recorded seq values are 1-based (see RecordFire), so
    // the newest fire's seq is `total`. Step down, skipping empty/garbage
    // slots, until we've filled `cap` or exhausted the ring window.
    unsigned written = 0;
    const uint64_t lowest = (total > kFireRingSize) ? (total - kFireRingSize + 1)
                                                    : 1;
    for (uint64_t s = total; s >= lowest && written < cap; --s) {
        const FireRecord& slot = g_fireRing[s % kFireRingSize];
        // The slot at this index must still hold THIS sequence number — if a
        // newer fire already overwrote it (ring wrapped mid-read), its seq
        // won't match `s`; skip it rather than report a stale/torn entry.
        if (slot.seq != s) continue;
        out[written++] = slot;
        if (s == 1) break;  // uint64 underflow guard for the s>=lowest loop
    }
    return written;
}

}  // namespace kcdx::modification_inventory
