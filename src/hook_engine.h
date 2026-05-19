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
    //
    // EXACTLY ONE of `bytes` or `lua_callback` must be non-empty. If both
    // are set or both are empty, the parser rejects the entry.
    std::vector<uint8_t> bytes;

    // --- Phase 5f: TOML-driven Lua callback ----------------------------
    //
    // When lua_callback is non-empty, ApplyOneHook builds a runtime_func_t
    // trampoline (the same one kcdx.memory.dynamic_hook uses), JIT'd with
    // make_jit_func against the declared return_type / param_types, and
    // installs it via hook_engine::InstallRuntime. The lua_callback name
    // (e.g. "OutfitGate.Decide") is resolved lazily at first dispatch
    // by walking _G[<dotted-path>]; if it doesn't exist when the hook
    // fires, kcdx logs a warn and lets the original run.
    //
    // lua_post_callback is optional; same lookup semantics but post-fires
    // after the original returns.
    std::string              return_type;       // "void", "i32", "ptr", etc. — same vocabulary as kcdx.memory.dynamic_hook
    std::vector<std::string> param_types;       // empty for a no-arg target
    std::string              lua_callback;      // dotted Lua function name; empty for raw-bytes hooks
    std::string              lua_post_callback; // optional; same lookup
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

// --- Phase 5c.7b.2: runtime hook installation -----------------------------
//
// Parallel to ApplyOneHook but for hooks resolved at runtime (not from
// TOML pre-flight). Used by kcdx.memory.dynamic_hook in pak Lua and by
// (Phase 5+) future plugin-DLL APIs that install hooks programmatically.
//
// Caller responsibility:
//   - resolve target_addr to an absolute VA
//   - JIT/build/copy the detour code somewhere within ±2 GB of target_addr
//     (typically via trampoline::AllocateBranch + runtime_func_t::make_jit_func)
//
// hook_engine responsibility:
//   - first-wins collision check against g_installed
//   - MH_CreateHook + MH_EnableHook
//   - bookkeep into g_installed so future hooks see this one
//
// Returns true on successful install, false on collision/MinHook failure.
// Logs the outcome regardless. The `name` is used in log messages and as
// the first-wins-collision report; pick something the plugin author will
// recognize.
struct RuntimeInstallResult {
    bool        ok = false;
    std::string reason;            // populated when !ok, for the caller
                                   // to surface to its own Lua/log channel
    void*       pOriginal = nullptr;  // MinHook's trampoline-to-original
                                      // pointer (Phase 5+: call-original
                                      // support; v0.1 ignores). void* not
                                      // LPVOID to avoid pulling windows.h
                                      // into every consumer of this header.
};

RuntimeInstallResult InstallRuntime(const std::string& name,
                                    uintptr_t          target_addr,
                                    void*              detour_addr);

}  // namespace kcdx::hook_engine
