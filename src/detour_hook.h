#pragma once
// detour_hook — the coordinator over a detour backend. Shape-matches RoM's
// big::detour_hook so the vendored runtime_func_t code calls this class with
// near-zero diff against upstream.
//
// RoM uses PolyHook2 (via its detour_hook). kcdx routes each install to an
// IDetourBackend (MinHook today; safetyhook + context routing land in later
// steps). detour_hook holds one backend and delegates every call to it —
// carrying no patching logic of its own. The public face here is UNCHANGED so
// runtime_func_t's three JIT-thunk sites see identical behavior.

#include <memory>
#include <string>

#include "detour_backend.h"

namespace kcdx {

class detour_hook {
public:
    // Defaults to a MinHookBackend — no routing yet (that is a later step).
    // The backend lives on the heap behind unique_ptr (a stable address), so
    // the slot get_original_ptr() returns stays put for the hook's lifetime.
    detour_hook();
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

    // Install the hook. Idempotent — calling enable() repeatedly is a no-op.
    void enable();

    // Uninstall the hook.
    void disable();

    // Pointer-to-the-slot-where-the-backend-stored-the-relocated-original.
    //
    // CRITICAL: this returns void** (a stable address, owned by the backend),
    // not void* (the value of the slot). RoM's JIT code bakes the address
    // returned here as an asmjit qword_ptr; the JIT'd instruction reads the
    // CURRENT value of the slot at runtime. If we returned void* (the value),
    // the JIT would bake whatever the slot was at JIT time — which is null
    // because we JIT BEFORE the backend's enable() populates it. The backend
    // is held by unique_ptr (stable heap address) and never moved, so the
    // returned address is valid for the hook's lifetime.
    //
    // Verified against RoM upstream src/hooks/detour_hook.hpp
    // @ commit d30217b6 (2026-05-19 investigation).
    void** get_original_ptr() { return backend_->get_original(); }

private:
    std::unique_ptr<IDetourBackend> backend_;
};

}  // namespace kcdx
