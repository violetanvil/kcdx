#include "detour_hook.h"

#include "MinHook.h"
#include "log.h"

namespace kcdx {

detour_hook::~detour_hook() {
    if (enabled_) {
        // Best-effort cleanup. We can't fail destruction, so swallow any
        // MinHook error. In practice kcdx never unloads plugins so this
        // path is rarely hit (matches SKSE's "no FreeLibrary, no teardown"
        // model).
        MH_RemoveHook(target_);
    }
}

void detour_hook::set_instance(const std::string& hook_name, void* target, void* detour) {
    name_   = hook_name;
    target_ = target;
    detour_ = detour;
}

void detour_hook::enable() {
    if (enabled_) return;
    if (!target_ || !detour_) {
        log::ErrorF("detour_hook '%s': enable() called without set_instance()",
                    name_.c_str());
        return;
    }

    // Create the hook. MinHook gives us the trampoline pointer back via
    // pOriginal — that's what RoM's JIT code reads via get_original_ptr().
    MH_STATUS rc = MH_CreateHook(target_, detour_, &original_);
    if (rc != MH_OK) {
        log::ErrorF("detour_hook '%s': MH_CreateHook failed (%s) at 0x%p",
                    name_.c_str(), MH_StatusToString(rc), target_);
        return;
    }

    rc = MH_EnableHook(target_);
    if (rc != MH_OK) {
        log::ErrorF("detour_hook '%s': MH_EnableHook failed (%s) at 0x%p",
                    name_.c_str(), MH_StatusToString(rc), target_);
        // Try to clean up the half-installed hook.
        MH_RemoveHook(target_);
        original_ = nullptr;
        return;
    }

    enabled_ = true;
    log::InfoF("detour_hook '%s': installed at 0x%p (detour=0x%p, original=0x%p)",
               name_.c_str(), target_, detour_, original_);
}

void detour_hook::disable() {
    if (!enabled_) return;
    MH_STATUS rc = MH_DisableHook(target_);
    if (rc != MH_OK) {
        log::ErrorF("detour_hook '%s': MH_DisableHook failed (%s) at 0x%p",
                    name_.c_str(), MH_StatusToString(rc), target_);
        // Don't return — flip the flag anyway so destructor doesn't double-disable.
    }
    enabled_ = false;
}

}  // namespace kcdx
