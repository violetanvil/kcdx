#pragma once
// detour_hook — thin MinHook wrapper that shape-matches RoM's
// big::detour_hook so the vendored runtime_func_t code can call this
// class with near-zero diff against upstream.
//
// RoM uses PolyHook2 (via its detour_hook). kcdx uses MinHook (already
// vendored, already used for the engine's lua_pcall / update hooks +
// the [[hook]] schema). This class encapsulates the adaptation so the
// rest of the rom-borrowed code doesn't need PolyHook2 references.

#include <cstdint>
#include <string>

namespace kcdx {

class detour_hook {
public:
    detour_hook() = default;
    ~detour_hook();

    detour_hook(const detour_hook&) = delete;
    detour_hook& operator=(const detour_hook&) = delete;

    // Configure the hook. Mirrors big::detour_hook's API. Does NOT install
    // the hook; call enable() for that.
    void set_instance(const std::string& hook_name, void* target, void* detour);

    // PolyHook2 has a "follow call instruction at target_func_ptr" mode.
    // MinHook has no equivalent — it always installs at the address you
    // give it. Accepting the setter as a no-op keeps the API
    // shape-compatible; we silently ignore the flag.
    void set_is_follow_call_on_fn_address(bool /*follow*/) { /* noop */ }

    // Install the hook (MH_CreateHook + MH_EnableHook). Idempotent —
    // calling enable() repeatedly is a no-op.
    void enable();

    // Uninstall the hook (MH_DisableHook + MH_RemoveHook).
    void disable();

    // Pointer-to-the-slot-where-MinHook-stored-pOriginal.
    //
    // CRITICAL: this returns void** (a stable address INTO this object),
    // not void* (the value of the slot). RoM's JIT code bakes the
    // address returned here as an asmjit qword_ptr; the JIT'd
    // instruction reads the CURRENT value of m_original at runtime.
    // If we returned void* (the value), the JIT would bake whatever
    // m_original was at JIT time — which is null because we JIT
    // BEFORE calling MH_CreateHook.
    //
    // Verified against RoM upstream src/hooks/detour_hook.hpp
    // @ commit d30217b6 (2026-05-19 investigation).
    void** get_original_ptr() { return &original_; }

private:
    std::string name_;
    void* target_   = nullptr;
    void* detour_   = nullptr;
    void* original_ = nullptr;
    bool  enabled_  = false;
};

}  // namespace kcdx
