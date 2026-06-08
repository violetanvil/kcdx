// safetyhook_link_selftest — a compile + link guard for the vendored safetyhook
// (Boost Software License 1.0) and its Zydis decoder (MIT). It references the
// types the hook-backend-marriage build depends on — InlineHook, MidHook,
// Context64 — so the build proves the include path resolves AND the static libs
// link into kcdx.dll. Without a reference, the linker dead-strips safetyhook and
// the wiring would go unverified.
//
// INVARIANT: this TU performs NO hooking. It takes the address of a safetyhook
// entry point and reads Context64's layout at compile time; nothing here runs
// against the live game. The real mid-hook proof is the cap-04 spike (the next
// step); this only certifies the backend the marriage routes to is present and
// linkable.

#include <cstddef>
#include <cstdint>
#include <expected>

#include <safetyhook/inline_hook.hpp>
#include <safetyhook/mid_hook.hpp>
#include <safetyhook/context.hpp>

namespace kcdx::safetyhook_link_selftest {

// Compile-time floor on the Context64 layout the mid-hook adapter will depend
// on: the full GPR file + rip (the call-original-mode lever) + trampoline_rsp
// (the writable stack pointer). The real struct is larger (it also carries the
// 16 XMM registers); 19 uintptr_t is a lower bound, not an exact field count.
static_assert(sizeof(safetyhook::Context64) >= sizeof(uintptr_t) * 19,
              "safetyhook::Context64 must carry the full register file + rip + "
              "trampoline_rsp the mid-hook adapter reads/writes");

// Keep the static libs linked: bind each create entry point to a function
// pointer of its exact (header-verified) signature, so the linker cannot
// dead-strip safetyhook. Presence, not invocation.
using InlineCreateFn =
    std::expected<safetyhook::InlineHook, safetyhook::InlineHook::Error> (*)(
        void*, void*, safetyhook::InlineHook::Flags);
using MidCreateFn =
    std::expected<safetyhook::MidHook, safetyhook::MidHook::Error> (*)(
        void*, safetyhook::MidHookFn, safetyhook::MidHook::Flags);

volatile InlineCreateFn g_inlineCreateAnchor = &safetyhook::InlineHook::create;
volatile MidCreateFn    g_midCreateAnchor    = &safetyhook::MidHook::create;

}  // namespace kcdx::safetyhook_link_selftest
