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

    // The trampoline-to-original function pointer, valid after enable().
    // RoM's JIT code references this directly to emit calls back into
    // the original function from inside the JIT-built trampoline.
    void* get_original_ptr() const { return original_; }

private:
    std::string name_;
    void* target_   = nullptr;
    void* detour_   = nullptr;
    void* original_ = nullptr;
    bool  enabled_  = false;
};

}  // namespace kcdx
