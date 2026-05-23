// scan_engine — `[[scan]]` TOML entry type.
//
// Pure-diagnostic locator resolver. Identical schema shape to [[patch]]'s
// locator block (pattern + offset + optional context + optional anchor)
// but writes nothing and applies nothing — just logs match count,
// resolved address(es), and surrounding raw bytes.
//
// Why it exists: new modders writing their first AOB have no
// discoverable way to validate "did my pattern resolve, and where"
// without committing a destructive write through [[patch]] or [[hook]].
// `[[scan]]` removes the cliff.
//
// Output to kcdx.log on a successful resolve:
//
//   [scan 'find_outfit_swap'] pattern matches: 1
//   [scan 'find_outfit_swap'] context matches: 1
//   [scan 'find_outfit_swap'] resolved to 0x00007FFCF9051745 (WHGame.dll+0x1971745)
//   [scan 'find_outfit_swap']   bytes -16: 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B
//   [scan 'find_outfit_swap']   bytes  +0: 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0 44 89 [...]

#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

#include "patch_engine.h"

namespace kcdx::scan_engine {

struct ScanEntry {
    std::string sourceFile;
    // Plugin name this scan belongs to. Stamped by LoadOneFile so the
    // scan apply path can honor load_order.toml's enabled = false.
    std::string pluginName;
    std::string name;
    std::string module = "WHGame.dll";

    patch::Pattern             pattern;
    int                        offset = 0;
    std::optional<patch::Pattern> context;
    patch::Anchor              anchor;
    uint32_t                   maxAnchorDistance = 4096;
};

extern std::vector<ScanEntry> g_scans;

// One resolved pattern hit, attributed to its owning module. A single
// scan can in principle span several modules in the future; each match
// therefore carries its OWN module + module-relative offset rather than
// inheriting a single per-scan module, even though today every match
// from one ScanEntry resolves within the entry's one module.
struct ScanMatch {
    uintptr_t   va = 0;          // absolute VA of the pattern hit.
    uintptr_t   applyAddr = 0;   // va + entry.offset (the apply addr the log prints).
    std::string module;          // owning module name, e.g. "WHGame.dll".
    uint64_t    relOffset = 0;   // applyAddr - module base (the module-relative offset).
};

// Result of resolving one ScanEntry, with no logging performed. RunOne
// turns this into the diagnostic log lines; the Lua binder consumes the
// structured fields directly.
struct ScanResult {
    bool   moduleLoaded = false;            // false if pe::OpenModule failed (AP2: no fabricated VA).
    size_t patternMatches = 0;              // hits.size().
    std::optional<size_t> contextMatches;   // set iff the entry carried a context pattern.
    std::vector<ScanMatch> matches;         // one per pattern hit, attributed.
};

// Resolve a scan entry to its matches WITHOUT logging: open the module,
// scan the executable sections for the pattern (and context if present),
// and attribute every hit (applyAddr, module, module-relative offset).
// On a module that is not loaded, returns moduleLoaded = false with no
// matches — never a fabricated VA (AP2). Shared by RunOne (diagnostic
// logging) and the kcdx.scan Lua binder.
ScanResult ResolveScan(const ScanEntry& s);

// Run every loaded scan: resolve via the same locator pipeline used by
// [[patch]] / [[hook]], log the outcome. Safe to call on the first
// update tick (same point [[patch]] applies happen).
void RunAll();

}  // namespace kcdx::scan_engine
