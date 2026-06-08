#pragma once
// IDetourBackend — the uniform byte-patcher / trampoline / install contract.
//
// Core contract: depends on nothing kcdx-specific. detour_hook (the
// coordinator) and every concrete backend depend on this; this depends on
// none of them. One responsibility: the install/uninstall + relocated-original
// surface a detour engine must expose. MinHook is the only backend today;
// safetyhook and a context router land in later steps without touching this
// seam's consumers (runtime_func_t reads get_original() through detour_hook).

#include <string>

namespace kcdx {

class IDetourBackend {
public:
    virtual ~IDetourBackend() = default;

    // Non-copyable, non-movable. A backend owns the original_ storage slot
    // whose ADDRESS the JIT bakes (get_original()); the object must stay put
    // for the hook's lifetime. detour_hook holds it by unique_ptr (a stable
    // heap address) and never moves it after the JIT bakes &slot.
    IDetourBackend(const IDetourBackend&)            = delete;
    IDetourBackend& operator=(const IDetourBackend&) = delete;
    IDetourBackend(IDetourBackend&&)                 = delete;
    IDetourBackend& operator=(IDetourBackend&&)      = delete;

    // Configure the detour (name + target + replacement). Does NOT install;
    // call enable() for that. Mirrors detour_hook's existing configure step.
    virtual void set_instance(const std::string& hook_name, void* target, void* detour) = 0;

    // Install (create + enable the detour). Idempotent — repeated calls no-op.
    virtual void enable() = 0;

    // Uninstall (disable + remove the detour).
    virtual void disable() = 0;

    // Pointer-to-the-slot-holding-the-relocated-original-entry.
    //
    // CRITICAL: returns void** (a STABLE address into the backend object), not
    // void* (the slot value). The JIT bakes the address returned here as an
    // asmjit qword_ptr; the JIT'd instruction reads the CURRENT value of the
    // slot at runtime. Returning void* would bake the slot's JIT-time value,
    // which is null because the JIT runs BEFORE enable() populates the slot.
    // The slot must live at a stable address for the hook's lifetime — which
    // is why a backend is non-movable and held only by unique_ptr.
    virtual void** get_original() = 0;

protected:
    IDetourBackend() = default;
};

}  // namespace kcdx
