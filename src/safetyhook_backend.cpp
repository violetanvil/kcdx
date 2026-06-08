#include "safetyhook_backend.h"

#include "log.h"

namespace kcdx {

namespace {

// Map safetyhook's typed InlineHook::Error onto a stable reason string. The
// typed errors are richer than MinHook's MH_ERROR_* — name the specific failure
// so log readers can act on it (logging.md). SOURCE: the InlineHook::Error enum
// in vendor/safetyhook/include/safetyhook/inline_hook.hpp — read this session.
const char* InlineErrorToString(const safetyhook::InlineHook::Error& e) {
    switch (e.type) {
    case safetyhook::InlineHook::Error::BAD_ALLOCATION:
        return "BAD_ALLOCATION (trampoline allocation failed)";
    case safetyhook::InlineHook::Error::FAILED_TO_DECODE_INSTRUCTION:
        return "FAILED_TO_DECODE_INSTRUCTION (could not decode the target prologue)";
    case safetyhook::InlineHook::Error::SHORT_JUMP_IN_TRAMPOLINE:
        return "SHORT_JUMP_IN_TRAMPOLINE (a relocated short jump can't reach)";
    case safetyhook::InlineHook::Error::IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE:
        return "IP_RELATIVE_INSTRUCTION_OUT_OF_RANGE (a relocated RIP-relative insn is out of range)";
    case safetyhook::InlineHook::Error::UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE:
        return "UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE (an unrelocatable instruction in the prologue)";
    case safetyhook::InlineHook::Error::FAILED_TO_UNPROTECT:
        return "FAILED_TO_UNPROTECT (could not unprotect the target/trampoline memory)";
    case safetyhook::InlineHook::Error::NOT_ENOUGH_SPACE:
        return "NOT_ENOUGH_SPACE (not enough room at the target to write the jump)";
    default:
        return "unknown safetyhook error";
    }
}

}  // namespace

void SafetyhookBackend::set_instance(const std::string& hook_name, void* target, void* detour) {
    name_   = hook_name;
    target_ = target;
    detour_ = detour;
}

void SafetyhookBackend::enable() {
    if (enabled_) return;
    if (!target_ || !detour_) {
        log::ErrorF("safetyhook_backend '%s': enable() called without set_instance()",
                    name_.c_str());
        return;
    }

    // Create the InlineHook disabled, then enable — the two distinct calls
    // (mirroring MinHook's MH_CreateHook + MH_EnableHook). The trampoline (the
    // relocated original) is valid after create; enable() writes the prologue
    // jump (E9 rel32, FF25 absolute fallback for a far target). SOURCE:
    // InlineHook::create / enable in vendor/safetyhook/src/inline_hook.cpp.
    auto created = safetyhook::InlineHook::create(
        target_, detour_, safetyhook::InlineHook::StartDisabled);
    if (!created) {
        log::ErrorF("safetyhook_backend '%s': InlineHook::create failed (%s) at 0x%p",
                    name_.c_str(), InlineErrorToString(created.error()), target_);
        return;
    }
    hook_ = std::move(*created);

    if (auto enabled = hook_.enable(); !enabled) {
        log::ErrorF("safetyhook_backend '%s': InlineHook::enable failed (%s) at 0x%p",
                    name_.c_str(), InlineErrorToString(enabled.error()), target_);
        // Drop the half-built hook; original_ stays null (the failure signal
        // InstallRuntime checks). reset() restores the prologue if anything
        // was written.
        hook_.reset();
        original_ = nullptr;
        return;
    }

    // The relocated-original entry the JIT thunk derefs (U2 — callable with the
    // original ABI from the asmjit thunk, like MinHook's pOriginal).
    original_ = hook_.original<void*>();

    enabled_ = true;
    log::InfoF("safetyhook_backend '%s': installed at 0x%p (detour=0x%p, original=0x%p)",
               name_.c_str(), target_, detour_, original_);
}

void SafetyhookBackend::disable() {
    if (!enabled_) return;
    if (auto disabled = hook_.disable(); !disabled) {
        log::ErrorF("safetyhook_backend '%s': InlineHook::disable failed (%s) at 0x%p",
                    name_.c_str(), InlineErrorToString(disabled.error()), target_);
        // Don't return — flip the flag anyway so a later call doesn't double-disable.
    }
    enabled_ = false;
}

}  // namespace kcdx
