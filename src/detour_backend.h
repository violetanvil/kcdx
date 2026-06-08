#pragma once
// IDetourBackend — the uniform byte-patcher / trampoline / install contract.
//
// Core contract: depends on nothing kcdx-specific. Every concrete backend
// depends on this; this depends on none of them. One responsibility: the
// install/uninstall + relocated-original surface a detour engine must expose.
// MinHook is the only backend today; safetyhook and a context router land in
// later steps without touching this seam's consumers.
//
// hook_engine::InstallRuntime owns a backend, drives create -> enable, and
// writes the backend's relocated-original (read via get_original()) into the
// runtime_func_t-owned JIT call-original slot. The backend produces the value;
// it does NOT own the JIT slot the trampoline derefs (runtime_func_t does).

#include <string>

namespace kcdx {

class IDetourBackend {
public:
    virtual ~IDetourBackend() = default;

    // Non-copyable, non-movable. A backend owns the original_ storage MinHook
    // writes its relocated-original into (get_original()); the object must
    // stay put for the hook's lifetime. InstallRuntime reads the value out of
    // get_original() once after enable() and writes it into runtime_func_t's
    // JIT slot, so the backend object itself outlives the call (it is leaked
    // for the session — kcdx never unhooks).
    IDetourBackend(const IDetourBackend&)            = delete;
    IDetourBackend& operator=(const IDetourBackend&) = delete;
    IDetourBackend(IDetourBackend&&)                 = delete;
    IDetourBackend& operator=(IDetourBackend&&)      = delete;

    // Configure the detour (name + target + replacement). Does NOT install;
    // call enable() for that.
    virtual void set_instance(const std::string& hook_name, void* target, void* detour) = 0;

    // Install (create + enable the detour). Idempotent — repeated calls no-op.
    virtual void enable() = 0;

    // Uninstall (disable + remove the detour).
    virtual void disable() = 0;

    // Pointer-to-the-slot-holding-the-relocated-original-entry.
    //
    // Returns void** (a stable address into the backend object). MinHook
    // writes pOriginal into this slot at enable(); InstallRuntime reads
    // *get_original() and copies the value into runtime_func_t's own JIT
    // slot (the address the JIT actually baked). A null value here is the
    // create/enable-failed signal InstallRuntime checks.
    virtual void** get_original() = 0;

protected:
    IDetourBackend() = default;
};

}  // namespace kcdx
