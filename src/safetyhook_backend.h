#pragma once
// SafetyhookBackend — IDetourBackend over safetyhook::InlineHook. The second
// backend; the function-entry hook_chain path routes here. One responsibility:
// safetyhook-backed detours.
//
// WHY safetyhook for function-entry: safetyhook's InlineHook tries a 5-byte E9
// rel32 jump first and falls back to a 14-byte FF25 absolute jump when the
// target is out of rel32 range, so it reaches ANY 64-bit target (the cap-21/
// cap-22 far-target gap closes here, no per-module branch-pool special-case).
// SOURCE: vendor/safetyhook/src/inline_hook.cpp setup()/e9_hook()/ff_hook() +
// the typed InlineHook::Error enum — read this session.
//
// SAFETY: safetyhook's enable() thread-suspends ALL threads while it patches
// (trap_threads, inline_hook.cpp:372). Correct for the function-entry chain
// path (the only caller this step); the loader-lock paths (early_hook, the
// update pump) stay MinHook because suspending under the loader lock deadlocks.
//
// get_original() returns the trampoline entry (InlineHook::original() ->
// m_trampoline.address()), which InstallRuntime writes into runtime_func_t's
// JIT call-original slot — the backend PRODUCES the value, it does NOT own that
// slot (runtime_func_t does). U2 (proven Phase 1 step 2): the trampoline entry
// is callable with the original ABI from the asmjit thunk like MinHook's
// pOriginal.
//
// hook_engine::InstallRuntime is the only driver of this backend.

#include <memory>
#include <string>

#include <safetyhook/inline_hook.hpp>

#include "detour_backend.h"

namespace kcdx {

class SafetyhookBackend final : public IDetourBackend {
public:
    SafetyhookBackend() = default;
    ~SafetyhookBackend() override = default;

    void set_instance(const std::string& hook_name, void* target, void* detour) override;
    void enable() override;
    void disable() override;

    // Returns &original_ — the stable slot the safetyhook trampoline entry is
    // stored into at enable(). InstallRuntime reads *get_original() once after
    // enable() and copies it into runtime_func_t's JIT slot. A null value is
    // the create/enable-failed signal InstallRuntime checks.
    void** get_original() override { return &original_; }

private:
    std::string name_;
    void* target_   = nullptr;
    void* detour_   = nullptr;
    void* original_ = nullptr;
    bool  enabled_  = false;
    // Owns the live safetyhook hook for the session (kcdx never unhooks —
    // SKSE "no FreeLibrary, no teardown"). Held so the trampoline stays valid.
    safetyhook::InlineHook hook_;
};

}  // namespace kcdx
