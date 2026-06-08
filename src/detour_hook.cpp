#include "detour_hook.h"

#include "minhook_backend.h"

namespace kcdx {

detour_hook::detour_hook()
    : backend_(std::make_unique<MinHookBackend>()) {}

detour_hook::~detour_hook() = default;

void detour_hook::set_instance(const std::string& hook_name, void* target, void* detour) {
    backend_->set_instance(hook_name, target, detour);
}

void detour_hook::enable() {
    backend_->enable();
}

void detour_hook::disable() {
    backend_->disable();
}

}  // namespace kcdx
