// kcdx::modification_inventory — see modification_inventory.h for WHY this
// module reads the LIVE modification sources (hook_chain::g_chains + the
// RegisterModification'd fixed installs) and NOT the dead hook_engine legacy
// vectors.

#include "modification_inventory.h"

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
    // hook_chain's own mutex — GetAllChainTargets() returns by value.
    std::vector<uintptr_t> chainTargets = hook_chain::GetAllChainTargets();

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
    for (uintptr_t va : chainTargets) { if (va) fp = FoldTargetVa(fp, va); }
    for (const auto& e : registered)  { if (e.va) fp = FoldTargetVa(fp, e.va); }

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

    // Per-target DETAIL at Debug (dev-only firehose): each VA + category + name.
    for (uintptr_t va : chainTargets) {
        if (!va) continue;
        LOG_DEBUG_KV("INVENTORY", "target",
            log::KV::BareStr("category", CategoryToken(Category::PluginHook)),
            log::KV("va",   (void*)va),
            log::KV::BareStr("name", "kcdx.hook"));
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

}  // namespace kcdx::modification_inventory
