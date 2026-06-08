#include "hook_engine.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "log.h"
#include "minhook_backend.h"
#include "safetyhook_backend.h"

namespace kcdx::hook_engine {

// HISTORICAL (apply-consolidation cut): the TOML-fed g_hooks / g_mid_hooks
// vectors and their orchestration (ApplyOneHook, ApplyOneMidHook,
// ApplyAll, DumpMidHookFingerprints, and the MakeLocatorPatch adapter)
// were removed. Those vectors had no populator after the TOML behavior
// tables were removed, so the apply
// loop in hooks.cpp that dispatched them was dead. The LIVE hook path is
// kcdx.hook / kcdxHookInterface routed through src/hook_chain.cpp; both it
// and kcdx.memory.dynamic_hook install via InstallRuntime (below). The
// backend seam lives HERE: a caller passes its InstallKind (what the install
// IS), and InstallRuntime selects the IDetourBackend via select_backend (the
// single-sourced §4.2 routing table) and drives create -> enable. The caller
// names no backend, so a wrong-backend literal is unwriteable (the
// misroute-impossible bar). The loader-lock + bootstrap paths (early_hook, the
// HookedUpdate pump, the frealloc canary) install via raw MH_CreateHook and
// never reach this seam — the documented bootstrap exceptions (hook-engine.md).

namespace {

// The re-homed cross-registry double-install guard. Maps targetAddr ->
// name of the first hook that grabbed it. The chain's FindChain front-runs
// this for chain hooks (so this never fires redundantly for them); it is
// load-bearing ONLY for the cross-registry collision FindChain cannot see —
// kcdx.memory.dynamic_hook (a NON-chain caller registering in a separate
// registry) colliding with a chain hook, or two dynamic_hooks, on one VA.
std::unordered_map<uintptr_t, std::string> g_installed;

// Compile-time proof the §4.2 routing table is correct — the unit-level
// predicate check (zero runtime cost; verified by the build gate itself, no
// live launch and no DI seam). Function-entry -> safetyhook, dynamic -> MinHook.
// Mid no longer routes through InstallRuntime (the safetyhook::MidHook adapter
// installs directly from AddMid/AddCMid; the ChainMid kind retired with the
// make_jit_midfunc replacement, design §5.3).
static_assert(select_backend(InstallKind::ChainFunctionEntry) == Backend::Safetyhook,
              "chain function-entry must route to safetyhook (design §4.2)");
static_assert(select_backend(InstallKind::DynamicHook) == Backend::MinHook,
              "dynamic_hook must route to MinHook (design §4.2)");

}  // namespace

RuntimeInstallResult InstallRuntime(const std::string& name,
                                    uintptr_t          target_addr,
                                    void*              detour_addr,
                                    InstallKind        kind) {
    RuntimeInstallResult out;

    const Backend backend = select_backend(kind);

    if (target_addr == 0) {
        out.reason = "target_addr is 0";
        log::ErrorF("[hook '%s'] aborted: %s", name.c_str(), out.reason.c_str());
        return out;
    }
    if (!detour_addr) {
        out.reason = "detour_addr is null";
        log::ErrorF("[hook '%s'] aborted: %s", name.c_str(), out.reason.c_str());
        return out;
    }

    // Misroute-impossible forward guard (E18, the design's one safety-critical
    // mechanism). The loader-lock + bootstrap paths NEVER reach InstallRuntime
    // (raw MH_CreateHook), so this is a forward guard for a FUTURE install path:
    // any kind that must stay on MinHook (mid + dynamic_hook today; a
    // hypothetical loader-lock kind tomorrow) resolving to safetyhook is the
    // bug the guard catches. A safetyhook install under the loader lock is a
    // deadlock (its unconditional thread-suspend) — fail LOUD and refuse, never
    // a silent wrong-backend (logging.md; AP14). Only ChainFunctionEntry may
    // ever select safetyhook; any other kind that does is a select_backend defect.
    if (backend == Backend::Safetyhook && kind != InstallKind::ChainFunctionEntry) {
        out.reason = "routing predicate selected safetyhook for a non-function-entry "
                     "install kind (loader-lock-unsafe misroute)";
        log::ErrorF("[hook '%s'] aborted: %s (kind=%d)",
                    name.c_str(), out.reason.c_str(), static_cast<int>(kind));
        return out;
    }

    // Cross-registry double-install refusal (the load-bearing guard re-homed
    // from the old v0.1 first-wins role). FindChain already gates a chain
    // hook before it reaches here, so for chain hooks this never fires; it
    // refuses the cross-registry case FindChain is blind to (a dynamic_hook
    // colliding with a chain hook / another dynamic_hook on a shared VA).
    if (auto it = g_installed.find(target_addr); it != g_installed.end()) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "target 0x%p already hooked by '%s' (v0.1 first-wins; "
                 "chained hooks are v0.2+)",
                 reinterpret_cast<void*>(target_addr), it->second.c_str());
        out.reason = buf;
        log::WarnF("[hook '%s'] aborted: %s", name.c_str(), out.reason.c_str());
        return out;
    }

    // Drive the detour backend, selected from the caller's InstallKind via
    // select_backend (the single-sourced §4.2 table; the guard above proved the
    // selection cannot misroute). MinHook patches without thread-suspend (the
    // loader-lock-safe path);
    // safetyhook is thread-safe and reaches any 64-bit target via its E9->FF
    // fallback (the function-entry chain path). detour_addr is expected to
    // already be within ±2 GB of target_addr (caller's responsibility; e.g.,
    // runtime_func_t::make_jit_func routes through branch_pool). The backend
    // is leaked deliberately: kcdx never unhooks (session-lifetime, SKSE
    // "no FreeLibrary, no teardown" model), and the backend must outlive this
    // call for the relocated-original it owns to stay valid for the session.
    IDetourBackend* impl =
        (backend == Backend::Safetyhook)
            ? static_cast<IDetourBackend*>(new SafetyhookBackend())
            : static_cast<IDetourBackend*>(new MinHookBackend());
    impl->set_instance(name, reinterpret_cast<void*>(target_addr), detour_addr);
    impl->enable();

    // enable() logs the specific backend failure itself; a null relocated-
    // original is the create/enable-failed signal (each backend resets
    // original_ to null on enable failure and never sets it on create failure).
    void* pOriginal = *impl->get_original();
    if (!pOriginal) {
        out.reason = "detour backend install failed (see kcdx.log)";
        log::ErrorF("[hook '%s'] aborted: %s at 0x%p",
                    name.c_str(), out.reason.c_str(),
                    reinterpret_cast<void*>(target_addr));
        delete impl;
        return out;
    }
    out.pOriginal = pOriginal;

    g_installed.emplace(target_addr, name);

    log::InfoF("[hook '%s'] installed at runtime target 0x%p (detour at 0x%p)",
               name.c_str(),
               reinterpret_cast<void*>(target_addr), detour_addr);

    // Diagnostic — same post-install probe as the legacy path so log readers
    // see the same "this is what the target looks like now" line.
    const uint8_t* siteBytes = reinterpret_cast<const uint8_t*>(target_addr);
    log::InfoF("[hook '%s'] post-install bytes at target: %02X %02X %02X %02X %02X",
               name.c_str(),
               siteBytes[0], siteBytes[1], siteBytes[2], siteBytes[3], siteBytes[4]);

    out.ok = true;
    return out;
}

}  // namespace kcdx::hook_engine
