#include "kcdx_trampoline_registry.h"

#include <mutex>
#include <vector>

#include "log.h"

namespace kcdx::kcdx_trampoline_registry {

namespace {

struct Range {
    uintptr_t base = 0;
    size_t    size = 0;
    Kind      kind = Kind::BranchPool;
};

// Append-only for the session (kcdx never unhooks). g_mu serializes installs
// (Register) against each other and against a concurrent Classify (Contains);
// reads take the same lock — a foreign-hook classification is a cold
// install-time path, never a hot loop, so the lock cost is irrelevant.
std::mutex          g_mu;
std::vector<Range>  g_ranges;

const char* KindName(Kind k) {
    switch (k) {
    case Kind::SafetyhookInline: return "safetyhook-inline";
    case Kind::SafetyhookMid:    return "safetyhook-mid";
    case Kind::BranchPool:       return "branch-pool";
    }
    return "?";
}

}  // namespace

void Register(uintptr_t base, size_t size, Kind kind) {
    if (base == 0 || size == 0) return;  // a failed/empty allocation owns nothing
    std::lock_guard<std::mutex> lock(g_mu);
    // De-dupe an exact repeat (the branch-pool registers a whole reservation on
    // first allocation from it; a second hook in the same reservation would
    // re-register the identical range — harmless, but skip it to keep the list
    // tight). Only an EXACT (base,size) match is a dupe; an overlapping-but-
    // different range is a distinct registration and kept.
    for (const auto& r : g_ranges) {
        if (r.base == base && r.size == size) return;
    }
    g_ranges.push_back({base, size, kind});
    log::DebugF("kcdx_trampoline_registry: registered %s range [0x%p, 0x%p) "
                "(%zu bytes; %zu total)",
                KindName(kind), reinterpret_cast<void*>(base),
                reinterpret_cast<void*>(base + size), size, g_ranges.size());
}

bool Contains(uintptr_t addr) {
    if (addr == 0) return false;
    std::lock_guard<std::mutex> lock(g_mu);
    for (const auto& r : g_ranges) {
        if (addr >= r.base && addr < r.base + r.size) return true;
    }
    return false;
}

size_t Count() {
    std::lock_guard<std::mutex> lock(g_mu);
    return g_ranges.size();
}

}  // namespace kcdx::kcdx_trampoline_registry
