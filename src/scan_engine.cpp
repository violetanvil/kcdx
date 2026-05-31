// scan_engine — see scan_engine.h.
//
// Implementation strategy: this is a thin wrapper around the existing
// patch::FindAllInBuffer + pe::ExecutableSections helpers. Identical
// scan path to byte patches (so identical match counts) but no write,
// no verify, no apply phase.

#include "scan_engine.h"

#include <windows.h>

#include "load_order.h"
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

// Is the FULL byte window [addr, addr+len) committed + readable? Mirrors
// save_load_hooks.cpp SafeReadByte/SafeReadPtr (VirtualQuery → MEM_COMMIT →
// not PAGE_NOACCESS), extended so the WHOLE range is covered, not just the
// start byte: a region edge mid-window (the next page uncommitted, or the
// window crossing into an adjacent region whose protection differs) must fail.
// Fail-state (Batch F #16): the context byte-dump reads applyAddr-16 .. with no
// guard; near a section/region edge (or if applyAddr-16 underflows below the
// region base) that lands on unmapped memory and AVs inside a diagnostic meant
// to be SAFE for a newbie validating an AOB. Returns false → caller skips the
// dump and warns, instead of crashing.
bool WindowReadable(uintptr_t addr, size_t len) {
    if (len == 0) return false;
    // Pointer-arithmetic underflow / overflow guard: applyAddr-16 can wrap below
    // 0, and addr+len can wrap past the top of the address space.
    if (addr + len < addr) return false;

    uintptr_t cur = addr;
    const uintptr_t end = addr + len;  // exclusive
    while (cur < end) {
        MEMORY_BASIC_INFORMATION mbi{};
        if (VirtualQuery(reinterpret_cast<const void*>(cur), &mbi,
                         sizeof(mbi)) == 0) {
            return false;
        }
        if (mbi.State != MEM_COMMIT) return false;
        if (mbi.Protect & (PAGE_NOACCESS | PAGE_GUARD)) return false;
        // Advance to the end of THIS region; loop re-queries the next one, so a
        // window spanning two regions only passes if EVERY region is committed
        // + readable.
        uintptr_t regionEnd =
            reinterpret_cast<uintptr_t>(mbi.BaseAddress) + mbi.RegionSize;
        if (regionEnd <= cur) return false;  // no forward progress → bail
        cur = regionEnd;
    }
    return true;
}

// Format `count` bytes at `addr` as space-separated hex pairs. The caller is
// responsible for confirming the full window is readable (WindowReadable) —
// this no longer performs a raw read of unguarded memory.
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

}  // namespace

ScanResult ResolveScan(const ScanEntry& s) {
    ScanResult result;

    pe::ModuleView mv;
    std::wstring wmod(s.module.begin(), s.module.end());
    if (!pe::OpenModule(wmod.c_str(), mv)) {
        // Module not loaded: leave moduleLoaded = false, no matches.
        // Never fabricate a VA when the resolve cannot run.
        return result;
    }
    result.moduleLoaded = true;

    auto hits = ScanAll(mv, s.pattern);
    result.patternMatches = hits.size();

    // Tier-2 context (optional, for uniqueness disambiguation). Recorded
    // as a count only — context is a uniqueness signal, not an apply site.
    if (s.context) {
        auto cHits = ScanAll(mv, *s.context);
        result.contextMatches = cHits.size();
    }

    // Attribute every hit to its owning module + module-relative offset.
    // Single-module scan today: every match's module is the entry's
    // module and relOffset = applyAddr - this module's base. Each match
    // still carries its own copy so a future multi-module scan attributes
    // per hit without a per-scan single-module assumption.
    uintptr_t modBase = reinterpret_cast<uintptr_t>(mv.baseBytes);
    result.matches.reserve(hits.size());
    for (uintptr_t hit : hits) {
        ScanMatch m;
        m.va = hit;
        m.applyAddr = hit + (intptr_t)s.offset;
        m.module = s.module;
        m.relOffset = (uint64_t)(m.applyAddr - modBase);
        result.matches.push_back(std::move(m));
    }
    return result;
}

ScanResult RunScan(const ScanEntry& s) {
    ScanResult result = ResolveScan(s);

    // --- Concise diagnostic log (the workbench feedback). Does NOT
    //     duplicate FormatBytesAt — the full byte-dump was the legacy
    //     [[scan]] TOML path's, now dormant. ---
    if (!result.moduleLoaded) {
        log::ErrorF("[scan '%s'] module '%s' not loaded (0 matches)",
                    s.name.c_str(), s.module.c_str());
    } else {
        log::InfoF("[scan '%s'] pattern matches: %zu",
                   s.name.c_str(), result.patternMatches);
        if (result.contextMatches) {
            log::InfoF("[scan '%s'] context matches: %zu",
                       s.name.c_str(), *result.contextMatches);
        }
        for (size_t i = 0; i < result.matches.size(); ++i) {
            const ScanMatch& m = result.matches[i];
            log::InfoF("[scan '%s'] match %zu: %s+0x%llX -> apply addr 0x%p",
                       s.name.c_str(), i + 1, m.module.c_str(),
                       (unsigned long long)m.relOffset,
                       reinterpret_cast<void*>(m.applyAddr));
        }
    }

    return result;
}

namespace {

// Thin diagnostic logger over ResolveScan: emits the scan diagnostic log
// lines (the legacy [[scan]] path's output, now dormant — g_scans has no
// populator anymore). Output here stays byte-for-byte identical to
// the documented scan-demo output — ResolveScan changes what is RETURNED,
// never what is LOGGED.
void RunOne(const ScanEntry& s) {
    ScanResult result = ResolveScan(s);

    if (!result.moduleLoaded) {
        log::ErrorF("[scan '%s'] module '%s' not loaded",
                    s.name.c_str(), s.module.c_str());
        return;
    }

    log::InfoF("[scan '%s'] pattern matches: %zu",
               s.name.c_str(), result.patternMatches);

    // Tier-2 context (optional, for uniqueness disambiguation).
    if (result.contextMatches) {
        log::InfoF("[scan '%s'] context matches: %zu",
                   s.name.c_str(), *result.contextMatches);
    }

    if (result.matches.empty()) {
        return;
    }
    if (result.matches.size() > 1) {
        log::InfoF("[scan '%s'] pattern not unique; listing all matches:",
                   s.name.c_str());
    }

    for (size_t i = 0; i < result.matches.size(); ++i) {
        const ScanMatch& m = result.matches[i];
        // The log prints the HIT's module offset (not the apply-addr's).
        // relOffset is applyAddr-relative, so recover the module base
        // (applyAddr - relOffset, exact by construction) and offset the
        // raw va against it — identical to the old `hit - modBase`.
        uintptr_t modBase = m.applyAddr - m.relOffset;
        log::InfoF("[scan '%s'] match %zu: pattern at 0x%p (%s+0x%llX); "
                   "with offset %+d -> apply addr 0x%p",
                   s.name.c_str(), i + 1,
                   reinterpret_cast<void*>(m.va),
                   m.module.c_str(),
                   (unsigned long long)(m.va - modBase),
                   s.offset,
                   reinterpret_cast<void*>(m.applyAddr));

        // 16 bytes before, 32 bytes at + after. Guard each window with
        // WindowReadable BEFORE reading (Batch F #16): near a section/region
        // edge applyAddr-16 can land on (or underflow into) unmapped memory,
        // and the +0 window can run off the end of the last committed region —
        // either AVs inside a diagnostic that must be SAFE for a newbie
        // validating an AOB. On an unreadable / partially-unmapped window we
        // SKIP that dump and Warn, instead of crashing. The scan still reports
        // its match above; only the optional context bytes are lost.
        if (WindowReadable(m.applyAddr - 16, 16)) {
            std::string before = FormatBytesAt(m.applyAddr - 16, 16);
            log::InfoF("[scan '%s']   bytes -16: %s",
                       s.name.c_str(), before.c_str());
        } else {
            log::WarnF("[scan '%s']   bytes -16: context byte-dump near "
                       "section edge skipped (region unreadable)",
                       s.name.c_str());
        }
        if (WindowReadable(m.applyAddr, 32)) {
            std::string after = FormatBytesAt(m.applyAddr, 32);
            log::InfoF("[scan '%s']   bytes  +0: %s",
                       s.name.c_str(), after.c_str());
        } else {
            log::WarnF("[scan '%s']   bytes  +0: context byte-dump near "
                       "section edge skipped (region unreadable)",
                       s.name.c_str());
        }
    }
}

}  // namespace

void RunAll() {
    if (g_scans.empty()) return;
    log::InfoF("Scan engine: running %zu scan diagnostic entr%s",
               g_scans.size(), g_scans.size() == 1 ? "y" : "ies");
    for (const auto& s : g_scans) {
        if (!load_order::IsPluginEnabled(s.pluginName)) {
            log::InfoF("[%s] skipping scan '%s' (plugin disabled via load_order.toml)",
                       s.pluginName.c_str(), s.name.c_str());
            continue;
        }
        RunOne(s);
    }
}

}  // namespace kcdx::scan_engine
