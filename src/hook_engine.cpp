#include "hook_engine.h"

#include <windows.h>
#include <cstring>
#include <unordered_map>

#include "MinHook.h"
#include "conflict_engine.h"
#include "log.h"
#include "patch_engine.h"  // for Resolve, ResolvedPatch (the locator pipeline)
#include "trampoline.h"

namespace kcdx::hook_engine {

std::vector<HookEntry> g_hooks;

namespace {

// Track which target addresses already have a hook installed, so we can
// detect hook-on-hook collisions in v0.1's first-wins policy. Maps
// targetAddr -> name of the first hook that grabbed it.
std::unordered_map<uintptr_t, std::string> g_installed;

// Adapter: shim a HookEntry's locator data into a PatchEntry that's just
// good enough for patch::Resolve() to do its job. The locator pipeline
// reads `pattern`, `context`, `anchor`, `module`, `maxAnchorDistance` and
// `offset`. We use offset = 0 because [[hook]]'s target IS the pattern
// match (functions are typically pointed at by their entry).
patch::PatchEntry MakeLocatorPatch(const HookEntry& h) {
    patch::PatchEntry p;
    p.sourceFile = h.sourceFile;
    p.name = h.name;
    p.module = h.module;
    p.pattern = h.pattern;
    p.context = h.context;
    p.anchor = h.anchor;
    p.maxAnchorDistance = h.maxAnchorDistance;
    p.offset = h.offset;
    // original/replacement aren't used by Resolve's locator-resolution
    // path, but Resolve checks original.size() == replacement.size() at
    // the very top — give it equal empty vectors so the check passes.
    p.original.clear();
    p.replacement.clear();
    return p;
}

}  // namespace

bool ApplyOneHook(size_t hookIdx) {
    if (hookIdx >= g_hooks.size()) return false;
    const HookEntry& h = g_hooks[hookIdx];

    // conflict_engine::RunPreFlight should have populated g_resolvedHooks
    // already. If we're being called outside that orchestration (tests,
    // future runtime paths), fall back to local resolution for this entry.
    if (hookIdx >= conflict_engine::g_resolvedHooks.size()) {
        log::Warn("Hook engine: conflict_engine pre-flight not run, "
                  "resolving hook locally (no cross-engine conflict matrix).");
        conflict_engine::g_resolvedHooks.resize(g_hooks.size());
        if (h.bytes.empty()) {
            conflict_engine::g_resolvedHooks[hookIdx].reason = "empty 'bytes' field";
        } else {
            patch::PatchEntry locator = MakeLocatorPatch(h);
            patch::ResolvedPatch r = patch::Resolve(locator);
            if (r.ok) {
                conflict_engine::g_resolvedHooks[hookIdx].ok = true;
                conflict_engine::g_resolvedHooks[hookIdx].targetAddr = r.patchAddr;
            } else {
                conflict_engine::g_resolvedHooks[hookIdx].reason = r.reason;
            }
        }
    }
    const auto& rh = conflict_engine::g_resolvedHooks[hookIdx];

    if (!rh.ok) {
        log::ErrorF("[hook '%s'] aborted: %s", h.name.c_str(), rh.reason.c_str());
        return false;
    }
    uintptr_t targetAddr = rh.targetAddr;

    // Hook-on-hook collision check (first-wins).
    if (auto it = g_installed.find(targetAddr); it != g_installed.end()) {
        log::WarnF("[hook '%s'] aborted: target 0x%p already hooked by '%s' "
                   "(v0.1 first-wins; chained hooks are v0.2+; "
                   "see conflict_engine HookOnHook WARN above)",
                   h.name.c_str(),
                   reinterpret_cast<void*>(targetAddr),
                   it->second.c_str());
        return false;
    }

    // Allocate branch-pool space for the detour body.
    void* detour = trampoline::AllocateBranch(/*owner=*/0, h.bytes.size());
    if (!detour) {
        log::ErrorF("[hook '%s'] aborted: trampoline branch pool exhausted "
                    "(needed %zu bytes)", h.name.c_str(), h.bytes.size());
        return false;
    }

    // Copy the plugin's bytes into the detour slot.
    std::memcpy(detour, h.bytes.data(), h.bytes.size());

    // Install via MinHook. pOriginal stores MinHook's trampoline-to-original
    // pointer; v0.1 doesn't surface this to hook authors (call-original
    // support is Phase 5), but MinHook still needs the out-pointer slot.
    LPVOID pOriginal = nullptr;
    MH_STATUS rc = MH_CreateHook(reinterpret_cast<LPVOID>(targetAddr),
                                 detour,
                                 &pOriginal);
    if (rc != MH_OK) {
        log::ErrorF("[hook '%s'] aborted: MH_CreateHook failed (%s) at 0x%p",
                    h.name.c_str(),
                    MH_StatusToString(rc),
                    reinterpret_cast<void*>(targetAddr));
        return false;
    }
    rc = MH_EnableHook(reinterpret_cast<LPVOID>(targetAddr));
    if (rc != MH_OK) {
        log::ErrorF("[hook '%s'] aborted: MH_EnableHook failed (%s) at 0x%p",
                    h.name.c_str(),
                    MH_StatusToString(rc),
                    reinterpret_cast<void*>(targetAddr));
        MH_RemoveHook(reinterpret_cast<LPVOID>(targetAddr));
        return false;
    }

    log::InfoF("[hook '%s'] installed at 0x%p (detour at 0x%p, %zu bytes)",
               h.name.c_str(),
               reinterpret_cast<void*>(targetAddr),
               detour,
               h.bytes.size());

    // Diagnostic: read the first 5 bytes at the target site after MinHook
    // claims to have installed. If MinHook rewrote the prologue, bytes
    // start with E9 (rel32 jmp) or FF 25 (abs64 jmp via [rip+0]). If still
    // unchanged, MinHook silently failed.
    const uint8_t* siteBytes = reinterpret_cast<const uint8_t*>(targetAddr);
    log::InfoF("[hook '%s'] post-install bytes at target: %02X %02X %02X %02X %02X",
               h.name.c_str(),
               siteBytes[0], siteBytes[1], siteBytes[2], siteBytes[3], siteBytes[4]);

    g_installed.emplace(targetAddr, h.name);
    return true;
}

size_t ApplyAll() {
    if (g_hooks.empty()) return 0;
    log::InfoF("Hook engine: applying %zu hook(s) (fallback path — production "
               "orchestration uses the unified apply loop in hooks.cpp)...",
               g_hooks.size());
    size_t installed = 0;
    for (size_t i = 0; i < g_hooks.size(); ++i) {
        if (ApplyOneHook(i)) ++installed;
    }
    log::InfoF("Hook engine: %zu of %zu hook(s) installed", installed, g_hooks.size());
    return installed;
}

}  // namespace kcdx::hook_engine
