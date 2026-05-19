#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "patch_engine.h"  // for Pattern, Anchor, AnchorString, etc.

namespace kcdx::hook_engine {

// One [[hook]] entry parsed from a kcdx.toml file. Locator fields reuse the
// patch engine's types (Pattern, Anchor variants) since the
// pattern/context/anchor resolution path is identical.
struct HookEntry {
    std::string sourceFile;       // path of the toml that contributed this hook
    std::string name;
    std::string description;
    int         priority = 100;
    std::string module = "WHGame.dll";

    // Locator — same shape as PatchEntry. Resolves to a function entry point
    // at (pattern_match + offset). `offset` is signed so the author can use
    // an AOB that anchors mid-function and step backward to the entry.
    patch::Pattern             pattern;
    int                        offset = 0;
    std::optional<patch::Pattern> context;
    patch::Anchor              anchor;
    uint32_t                   maxAnchorDistance = 4096;

    // The detour body. Plugin author's raw x86-64. v0.1 doesn't auto-wire a
    // call-original trampoline; plugin must either replace original entirely
    // (last instruction = ret), or bake in their own jump back to the
    // MinHook-managed trampoline using a separate mechanism. Phase 5+ adds
    // typed Lua callbacks that make this more accessible.
    std::vector<uint8_t> bytes;
};

// Engine state.
extern std::vector<HookEntry> g_hooks;

// Apply one hook by index into g_hooks. Reads its resolution from
// conflict_engine::g_resolvedHooks. Logs status. Returns true on
// successful install or no-op skip; false on abort.
//
// Called by the unified apply orchestrator (in hooks.cpp's first-update-tick
// handler) which interleaves patch and hook applies in global load order.
bool ApplyOneHook(size_t hookIdx);

// Apply every loaded hook in g_hooks order. Used by fallback paths only
// (the production orchestration calls ApplyOneHook from a global sorted
// loop instead). Returns the number of hooks successfully installed.
size_t ApplyAll();

}  // namespace kcdx::hook_engine
