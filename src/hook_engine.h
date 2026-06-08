#pragma once
#include <cstdint>
#include <optional>
#include <string>
#include <variant>
#include <vector>

#include "patch_engine.h"  // for Pattern, Anchor, AnchorString, etc.
#include "log.h"           // log facilities used across hook_engine

namespace kcdx::hook_engine {

// One hook entry (the in-memory shape kcdx.hook builds). Locator fields reuse the
// patch engine's types (Pattern, Anchor variants) since the
// pattern/context/anchor resolution path is identical.
struct HookEntry {
    std::string sourceFile;       // path of the toml that contributed this hook
    // Plugin name this entry belongs to (the [plugin].name from the
    // owning kcdx.toml). Stamped by LoadOneFile alongside `source`.
    // Used by the load-order sort to look up the plugin's effective
    // zone + priority.
    std::string pluginName;
    kcdx::config::Source source = kcdx::config::Source::User;
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
    uint64_t                   addressId = 0;  // kcdx Address Library id
    int                        offset = 0;
    std::optional<patch::Pattern> context;
    patch::Anchor              anchor;
    uint32_t                   maxAnchorDistance = 4096;

    // The detour body. Plugin author's raw x86-64. v0.1 doesn't auto-wire a
    // call-original trampoline; plugin must either replace original entirely
    // (last instruction = ret), or bake in their own jump back to the
    // MinHook-managed trampoline using a separate mechanism. Typed Lua
    // callbacks make this more accessible.
    //
    // EXACTLY ONE of `bytes` or `lua_callback` must be non-empty. If both
    // are set or both are empty, the parser rejects the entry.
    std::vector<uint8_t> bytes;

    // --- TOML-driven Lua callback --------------------------------------
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

// Mid-function hook entries (kcdx.hook mode=mid). Distinct from HookEntry
// because the install path is different (a safetyhook::MidHook adapter —
// src/safetyhook_midhook.{cpp,h} — instead of make_jit_func) and the schema
// fields are different (captures + stack_restore_offset instead of return_type).
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
// call_original mode for mid-function hooks (kcdx.hook mode=mid) — decides whether the captured
// instruction at the hook site runs after the Lua callback returns.
//
//   True   — default; original instruction always runs (compile-time
//            decision). Used when the hook only observes / mutates
//            register values that the original then consumes.
//   False  — original instruction NEVER runs (compile-time decision).
//            JIT pushes precomputed (target + stack_restore_offset)
//            instead of MinHook's trampoline_ptr. Used to fully
//            replace the captured instruction's effect.
//   Auto   — Lua callback decides at runtime by setting
//            `args._skip = true` on the captures table. Dispatcher
//            reads the flag post-pcall and signals JIT via a global
//            atomic; JIT consults the flag in its post-callback path
//            and swaps the stack-top from trampoline_ptr to resume_addr
//            if set. Used when the decision is data-dependent.
enum class CallOriginalMode : uint8_t {
    True  = 0,   // default; original runs
    False = 1,   // codegen-time skip
    Auto  = 2,   // runtime decision via args._skip
};

struct MidHookEntry {
    std::string sourceFile;
    // Plugin name this entry belongs to (the [plugin].name from the
    // owning kcdx.toml). Stamped by LoadOneFile alongside `source`.
    // Used by the load-order sort to look up the plugin's effective
    // zone + priority.
    std::string pluginName;
    kcdx::config::Source source = kcdx::config::Source::User;
    std::string name;
    std::string description;
    int         priority = 100;
    std::string module = "WHGame.dll";

    // Locator — same as HookEntry / PatchEntry. Exactly one of pattern,
    // targetSymbol, or addressId must be set.
    patch::Pattern             pattern;
    std::string                targetSymbol;   // cross-plugin symbol-table lookup
    uint64_t                   addressId = 0;
    int                        offset = 0;
    std::optional<patch::Pattern> context;
    patch::Anchor              anchor;
    uint32_t                   maxAnchorDistance = 4096;

    // Mid-hook-specific
    std::vector<std::string> param_types;       // per-capture type, "i8" / "i32" / "ptr" / etc.
    std::vector<std::string> param_captures;    // per-capture source, "r14b" / "[rcx+0x10]" / etc.
    int                      stack_restore_offset = 0;  // 0 = auto-decode via hde64
    CallOriginalMode         callOriginal = CallOriginalMode::True;
    std::string              lua_callback;      // dotted Lua function name; required
};

// HISTORICAL (apply-consolidation cut): the TOML-fed engine state
// (g_hooks / g_mid_hooks) and their apply orchestration (ApplyOneHook,
// ApplyOneMidHook, ApplyAll, DumpMidHookFingerprints) were removed. Those
// vectors had no populator after the TOML behavior tables were removed,
// so the apply loop in hooks.cpp
// that walked them was dead code. The HookEntry / MidHookEntry /
// CallOriginalMode definitions above are retained as dead-but-present types
// (nothing constructs them now). The LIVE hook path is kcdx.hook /
// kcdxHookInterface via src/hook_chain.cpp; it and kcdx.memory.dynamic_hook
// install via InstallRuntime below.

// --- runtime hook installation --------------------------------------------
//
// Parallel to ApplyOneHook but for hooks resolved at runtime (not from
// TOML pre-flight). Used by kcdx.memory.dynamic_hook in pak Lua and by
// future plugin-DLL APIs that install hooks programmatically.
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
                                      // pointer (for future call-original
                                      // support; v0.1 ignores). void* not
                                      // LPVOID to avoid pulling windows.h
                                      // into every consumer of this header.
};

// Which detour backend InstallRuntime drives for a given install. Engine-internal
// only — never a literal at a call site (callers pass an InstallKind; the engine
// selects the Backend via select_backend below). There is no author knob — the
// engine alone selects the backend (cornerstones.md: the engine does the heavy
// lifting). See safetyhook_backend.h / minhook_backend.h for the two backends.
enum class Backend {
    MinHook,     // byte-patch without thread-suspend; the loader-lock-safe path
    Safetyhook,  // thread-safe install + far-target reach (E9->FF fallback)
};

// What an install IS — the call-site context the routing predicate keys on
// (design §4.2). A caller declares its KIND, never a Backend: the engine maps
// kind -> backend in select_backend, so a wrong-backend literal is unwriteable
// (the misroute-impossible bar, U5 / design §9.5). The loader-lock + bootstrap
// paths (early_hook, the HookedUpdate pump, the frealloc canary) install via
// raw MH_CreateHook and never reach this seam, so they have no InstallKind — the
// documented bootstrap exceptions (hook-engine.md), structurally outside the seam.
// Mid-function hooks do NOT route through InstallRuntime — the mid path is a
// safetyhook::MidHook adapter (src/safetyhook_midhook.{cpp,h}) installed DIRECTLY
// from AddMid / AddCMid, never through this seam (the former ChainMid kind only
// ever faked a function-entry install; it retired with the make_jit_midfunc
// replacement, design §5.3/§8). InstallRuntime now serves only function-entry +
// dynamic_hook.
enum class InstallKind {
    ChainFunctionEntry,  // hook_chain function-entry (Add / AddC) — safetyhook
    DynamicHook,         // kcdx.memory.dynamic_hook (non-chain caller) — MinHook
};

// The single-sourced §4.2 routing table: kind -> backend. The ONLY place the
// routing decision lives; every InstallRuntime caller passes its kind and this
// computes the backend. `constexpr` so the mapping is a compile-time fact (the
// predicate-correctness static_asserts in InstallRuntime's TU verify it with
// zero runtime cost — no live launch, no DI seam).
constexpr Backend select_backend(InstallKind kind) {
    switch (kind) {
        case InstallKind::ChainFunctionEntry: return Backend::Safetyhook;
        case InstallKind::DynamicHook:        return Backend::MinHook;
    }
    return Backend::MinHook;  // unreachable; loader-lock-safe default fail-closed
}

// The caller passes its InstallKind; InstallRuntime selects the backend via
// select_backend and asserts the selection cannot misroute a loader-lock /
// bootstrap context onto safetyhook (the forward guard, design §4.2). A caller
// literally cannot name a backend, so a wrong-backend literal is unwriteable.
RuntimeInstallResult InstallRuntime(const std::string& name,
                                    uintptr_t          target_addr,
                                    void*              detour_addr,
                                    InstallKind        kind);

// NOTE: the load-path engine-modification inventory moved to
// src/modification_inventory.{h,cpp}. It used to read this TU's legacy
// g_hooks / g_patches / g_mid_hooks vectors, but those are DEAD post-Phase-5
// (empty, nothing populates them). The live inventory enumerates
// hook_chain::g_chains + the RegisterModification'd fixed installs instead.

}  // namespace kcdx::hook_engine
