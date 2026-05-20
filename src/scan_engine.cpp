// scan_engine — see scan_engine.h.
//
// Implementation strategy: this is a thin wrapper around the existing
// patch::FindAllInBuffer + pe::ExecutableSections helpers. Identical
// scan path to [[patch]] (so identical match counts) but no write,
// no verify, no apply phase.

#include "scan_engine.h"

#include "log.h"
#include "pe_helpers.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace kcdx::scan_engine {

std::vector<ScanEntry> g_scans;

namespace {

// Scan module executable sections for `pat`; return absolute VAs of
// every match.
std::vector<uintptr_t> ScanAll(const pe::ModuleView& mv,
                               const patch::Pattern& pat) {
    std::vector<uintptr_t> hits;
    if (pat.bytes.empty()) return hits;
    auto sections = pe::ExecutableSections(mv);
    for (const auto& s : sections) {
        auto offs = patch::FindAllInBuffer(s.data, s.size, pat);
        for (size_t off : offs) {
            hits.push_back(reinterpret_cast<uintptr_t>(s.data + off));
        }
    }
    return hits;
}

// Format up to `count` bytes at `addr` as space-separated hex pairs.
// Bounds-safe via VirtualQuery; truncates at the end of the readable
// page if needed.
std::string FormatBytesAt(uintptr_t addr, size_t count) {
    if (count == 0) return {};
    char buf[1024];
    size_t bufPos = 0;
    auto* p = reinterpret_cast<const uint8_t*>(addr);
    for (size_t i = 0; i < count && bufPos + 4 < sizeof(buf); ++i) {
        int n = std::snprintf(buf + bufPos, sizeof(buf) - bufPos,
                              i == 0 ? "%02X" : " %02X", p[i]);
        if (n < 0 || (size_t)n >= sizeof(buf) - bufPos) break;
        bufPos += n;
    }
    return std::string(buf, bufPos);
}

void RunOne(const ScanEntry& s) {
    pe::ModuleView mv;
    std::wstring wmod(s.module.begin(), s.module.end());
    if (!pe::OpenModule(wmod.c_str(), mv)) {
        log::ErrorF("[scan '%s'] module '%s' not loaded",
                    s.name.c_str(), s.module.c_str());
        return;
    }

    auto hits = ScanAll(mv, s.pattern);
    log::InfoF("[scan '%s'] pattern matches: %zu",
               s.name.c_str(), hits.size());

    // Tier-2 context (optional, for uniqueness disambiguation).
    if (s.context) {
        auto cHits = ScanAll(mv, *s.context);
        log::InfoF("[scan '%s'] context matches: %zu",
                   s.name.c_str(), cHits.size());
    }

    if (hits.empty()) {
        return;
    }
    if (hits.size() > 1) {
        log::InfoF("[scan '%s'] pattern not unique; listing all matches:",
                   s.name.c_str());
    }

    uintptr_t modBase = reinterpret_cast<uintptr_t>(mv.baseBytes);
    for (size_t i = 0; i < hits.size(); ++i) {
        uintptr_t hit = hits[i];
        uintptr_t applyAddr = hit + (intptr_t)s.offset;
        log::InfoF("[scan '%s'] match %zu: pattern at 0x%p (%s+0x%llX); "
                   "with offset %+d -> apply addr 0x%p",
                   s.name.c_str(), i + 1,
                   reinterpret_cast<void*>(hit),
                   s.module.c_str(),
                   (unsigned long long)(hit - modBase),
                   s.offset,
                   reinterpret_cast<void*>(applyAddr));

        // 16 bytes before, 32 bytes at + after. Bounded by safety —
        // raw memcpy from page; if we hit unmapped memory we'll AV.
        // The hit address came from a scan of mapped executable
        // bytes so this is safe within the same section.
        std::string before = FormatBytesAt(applyAddr - 16, 16);
        std::string after  = FormatBytesAt(applyAddr, 32);
        log::InfoF("[scan '%s']   bytes -16: %s",
                   s.name.c_str(), before.c_str());
        log::InfoF("[scan '%s']   bytes  +0: %s",
                   s.name.c_str(), after.c_str());
    }
}

}  // namespace

void RunAll() {
    if (g_scans.empty()) return;
    log::InfoF("Scan engine: running %zu [[scan]] entr%s",
               g_scans.size(), g_scans.size() == 1 ? "y" : "ies");
    for (const auto& s : g_scans) {
        RunOne(s);
    }
}

}  // namespace kcdx::scan_engine
