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

    // Locator — same shape as PatchEntry. Exactly one of pattern /
    // targetSymbol / addressId must be set. Resolves to (resolved_va +
    // offset). `offset` is signed so the author can use an AOB that
    // anchors mid-function and step backward to the entry.
    patch::Pattern             pattern;
    std::string                targetSymbol;   // cross-plugin symbol-table lookup
    uint64_t                   addressId = 0;  // kcdx Address Library id (Phase 7)
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

    // Set true by ApplyOneHook if MinHook install + enable succeeded.
    // Read by GetConflictReport so test plugins can verify outcomes.
    bool                     appliedOK = false;
};

// Phase 5g: [[mid_hook]] entries from kcdx.toml. Distinct from HookEntry
// because the install path is different (runtime_func_t::make_jit_midfunc
// instead of make_jit_func) and the schema fields are different (captures
// + stack_restore_offset instead of return_type).
//
// Mid-hooks land at an arbitrary offset inside a function — typically
// pointing at a specific instruction whose effect the plugin wants to
// override. The Lua callback receives a table keyed by capture name
// (e.g. {r14b = <value_wrapper>}). The callback mutates wrapper values
// via :set(...); on return, the trampoline writes the modified values
// back to the named registers (or memory expressions) and resumes
// execution at (pattern_match + offset + stack_restore_offset) — past
// the captured instruction.
//
// param_captures syntax (subset of RoM's, expanded as plugins demand):
//   "rax", "rbx", "r14b", etc. — bare GP register name
//   "xmm0", "xmm1"             — XMM register name
//   "[rcx+0x10]"               — memory at register + displacement
//                                (square-brackets mark a memory expr)
//
// stack_restore_offset is the byte length of the captured instruction.
// For a 3-byte `mov r14b, al` that's 3; resume executes the instruction
// after the mov. Set to 0 to re-execute the captured instruction itself
// after the callback (rare; only useful when the capture is a no-op
// observability tap rather than an override).
struct MidHookEntry {
    std::string sourceFile;
    std::string name;
    std::string description;
    int         priority = 100;
    std::string module = "WHGame.dll";

    // Locator — same as HookEntry / PatchEntry. Exactly one of pattern
    // or addressId must be set. (Mid-hooks don't currently consume
    // target_symbol.)
    patch::Pattern             pattern;
    uint64_t                   addressId = 0;
    int                        offset = 0;
    std::optional<patch::Pattern> context;
    patch::Anchor              anchor;
    uint32_t                   maxAnchorDistance = 4096;

    // Mid-hook-specific
    std::vector<std::string> param_types;       // per-capture type, "i8" / "i32" / "ptr" / etc.
    std::vector<std::string> param_captures;    // per-capture source, "r14b" / "[rcx+0x10]" / etc.
    int                      stack_restore_offset = 0;
    std::string              lua_callback;      // dotted Lua function name; required
};

// Engine state.
extern std::vector<HookEntry>     g_hooks;
extern std::vector<MidHookEntry>  g_mid_hooks;

// Apply one hook by index into g_hooks. Reads its resolution from
// conflict_engine::g_resolvedHooks. Logs status. Returns true on
// successful install or no-op skip; false on abort.
//
// Called by the unified apply orchestrator (in hooks.cpp's first-update-tick
// handler) which interleaves patch and hook applies in global load order.
bool ApplyOneHook(size_t hookIdx);

// Phase 5g: apply one mid-hook by index into g_mid_hooks. Resolves the
// locator inline (mid-hooks don't currently participate in
// conflict_engine pre-flight — they're an additive layer with their
// own first-wins semantics via hook_engine::InstallRuntime). Returns
// true on success, false on abort.
bool ApplyOneMidHook(size_t midHookIdx);

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
