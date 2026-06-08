#pragma once
// MinHookBackend — IDetourBackend over MinHook (MH_CreateHook / EnableHook /
// DisableHook / RemoveHook). Holds the original_ slot MinHook populates and
// the JIT bakes the address of. One responsibility: MinHook-backed detours.
//
// RoM uses PolyHook2; kcdx uses MinHook (already vendored — the engine's
// lua_pcall / update hooks + the kcdx.hook surface). This is the verbatim body
// detour_hook held before the backend seam; behavior is byte-for-byte
// unchanged (same MH_* calls, same order, same logging, same enabled_ flag).

#include <string>

#include "detour_backend.h"

namespace kcdx {

class MinHookBackend final : public IDetourBackend {
public:
    MinHookBackend() = default;
    ~MinHookBackend() override;

    void set_instance(const std::string& hook_name, void* target, void* detour) override;
    void enable() override;
    void disable() override;

    // Returns &original_ — the stable slot MinHook writes pOriginal into.
    // Verified against RoM upstream src/hooks/detour_hook.hpp @ commit d30217b6
    // (2026-05-19 investigation): the JIT derefs this slot for the trampoline.
    void** get_original() override { return &original_; }

private:
    std::string name_;
    void* target_   = nullptr;
    void* detour_   = nullptr;
    void* original_ = nullptr;
    bool  enabled_  = false;
};

}  // namespace kcdx
