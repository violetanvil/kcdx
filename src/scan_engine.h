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

// Run every loaded scan: resolve via the same locator pipeline used by
// [[patch]] / [[hook]], log the outcome. Safe to call on the first
// update tick (same point [[patch]] applies happen).
void RunAll();

}  // namespace kcdx::scan_engine
