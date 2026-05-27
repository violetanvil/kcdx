#include "hook_engine.h"

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include <string>
#include <unordered_map>

#include "MinHook.h"
#include "log.h"

namespace kcdx::hook_engine {

// HISTORICAL (apply-consolidation cut): the TOML-fed g_hooks / g_mid_hooks
// vectors and their orchestration (ApplyOneHook, ApplyOneMidHook,
// ApplyAll, DumpMidHookFingerprints, and the MakeLocatorPatch adapter)
// were removed. Those vectors had no populator after the TOML behavior
// tables were removed, so the apply
// loop in hooks.cpp that dispatched them was dead. The LIVE hook path is
// kcdx.hook / kcdxHookInterface routed through src/hook_chain.cpp; both it
// and kcdx.memory.dynamic_hook install via InstallRuntime (below), which
// still owns the shared first-wins g_installed map.

namespace {

// Track which target addresses already have a hook installed, so we can
// detect hook-on-hook collisions in v0.1's first-wins policy. Maps
// targetAddr -> name of the first hook that grabbed it. Live: shared by
// InstallRuntime (the kcdx.hook / dynamic_hook install path).
std::unordered_map<uintptr_t, std::string> g_installed;

}  // namespace

RuntimeInstallResult InstallRuntime(const std::string& name,
                                    uintptr_t          target_addr,
                                    void*              detour_addr) {
    RuntimeInstallResult out;

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

    // First-wins collision check. Mirrors ApplyOneHook line 82-90.
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

    // Hand to MinHook. detour_addr is expected to already be within
    // ±2 GB of target_addr (caller's responsibility; e.g.,
    // runtime_func_t::make_jit_func now routes through branch_pool).
    LPVOID pOriginalLp = nullptr;
    MH_STATUS rc = MH_CreateHook(reinterpret_cast<LPVOID>(target_addr),
                                 detour_addr,
                                 &pOriginalLp);
    out.pOriginal = pOriginalLp;
    if (rc != MH_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "MH_CreateHook failed (%s) at 0x%p",
                 MH_StatusToString(rc), reinterpret_cast<void*>(target_addr));
        out.reason = buf;
        log::ErrorF("[hook '%s'] aborted: %s", name.c_str(), out.reason.c_str());
        return out;
    }
    rc = MH_EnableHook(reinterpret_cast<LPVOID>(target_addr));
    if (rc != MH_OK) {
        char buf[256];
        snprintf(buf, sizeof(buf),
                 "MH_EnableHook failed (%s) at 0x%p",
                 MH_StatusToString(rc), reinterpret_cast<void*>(target_addr));
        out.reason = buf;
        log::ErrorF("[hook '%s'] aborted: %s", name.c_str(), out.reason.c_str());
        MH_RemoveHook(reinterpret_cast<LPVOID>(target_addr));
        return out;
    }

    g_installed.emplace(target_addr, name);

    log::InfoF("[hook '%s'] installed at runtime target 0x%p (detour at 0x%p)",
               name.c_str(),
               reinterpret_cast<void*>(target_addr), detour_addr);

    // Diagnostic — same post-install probe as ApplyOneHook so log readers
    // see the same "this is what the target looks like now" line.
    const uint8_t* siteBytes = reinterpret_cast<const uint8_t*>(target_addr);
    log::InfoF("[hook '%s'] post-install bytes at target: %02X %02X %02X %02X %02X",
               name.c_str(),
               siteBytes[0], siteBytes[1], siteBytes[2], siteBytes[3], siteBytes[4]);

    out.ok = true;
    return out;
}

}  // namespace kcdx::hook_engine
