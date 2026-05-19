#include "hook_engine.h"

#include <windows.h>
#include <cstring>
#include <unordered_map>

#include "MinHook.h"
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

size_t ApplyAll() {
    if (g_hooks.empty()) return 0;

    log::InfoF("Hook engine: applying %zu hook(s)...", g_hooks.size());
    size_t installed = 0;

    for (const HookEntry& h : g_hooks) {
        if (h.bytes.empty()) {
            log::ErrorF("[hook '%s'] aborted: empty 'bytes' field", h.name.c_str());
            continue;
        }

        // Step 1: resolve target via the existing locator pipeline.
        patch::PatchEntry locator = MakeLocatorPatch(h);
        patch::ResolvedPatch r = patch::Resolve(locator);
        if (!r.ok) {
            log::ErrorF("[hook '%s'] aborted: %s", h.name.c_str(), r.reason.c_str());
            continue;
        }
        uintptr_t targetAddr = r.patchAddr;

        // Step 2: hook-on-hook collision check (first-wins).
        if (auto it = g_installed.find(targetAddr); it != g_installed.end()) {
            log::WarnF("[hook '%s'] aborted: target 0x%p already hooked by '%s' "
                       "(v0.1 first-wins; chained hooks are v0.2+)",
                       h.name.c_str(),
                       reinterpret_cast<void*>(targetAddr),
                       it->second.c_str());
            continue;
        }

        // Step 3: allocate branch-pool space for the detour body.
        //
        // Owner = 0 means "engine" (no plugin handle attached). In Phase 4b
        // when TOML hooks come from plugin DLLs we'll thread the owner
        // through; for now [[hook]] is a kcdx.toml-only feature so the
        // engine owns the alloc.
        void* detour = trampoline::AllocateBranch(/*owner=*/0, h.bytes.size());
        if (!detour) {
            log::ErrorF("[hook '%s'] aborted: trampoline branch pool exhausted "
                        "(needed %zu bytes)", h.name.c_str(), h.bytes.size());
            continue;
        }

        // Step 4: copy the plugin's bytes into the detour slot.
        std::memcpy(detour, h.bytes.data(), h.bytes.size());

        // Step 5: install via MinHook. pOriginal stores MinHook's
        // trampoline-to-original pointer; v0.1 doesn't surface this to the
        // hook author (call-original support comes in Phase 5 with typed
        // marshaling), but MinHook still needs an out-pointer slot.
        LPVOID pOriginal = nullptr;
        MH_STATUS rc = MH_CreateHook(reinterpret_cast<LPVOID>(targetAddr),
                                     detour,
                                     &pOriginal);
        if (rc != MH_OK) {
            log::ErrorF("[hook '%s'] aborted: MH_CreateHook failed (%s) at 0x%p",
                        h.name.c_str(),
                        MH_StatusToString(rc),
                        reinterpret_cast<void*>(targetAddr));
            continue;
        }
        rc = MH_EnableHook(reinterpret_cast<LPVOID>(targetAddr));
        if (rc != MH_OK) {
            log::ErrorF("[hook '%s'] aborted: MH_EnableHook failed (%s) at 0x%p",
                        h.name.c_str(),
                        MH_StatusToString(rc),
                        reinterpret_cast<void*>(targetAddr));
            // Best-effort cleanup. The CreateHook left state behind.
            MH_RemoveHook(reinterpret_cast<LPVOID>(targetAddr));
            continue;
        }

        log::InfoF("[hook '%s'] installed at 0x%p (detour at 0x%p, %zu bytes)",
                   h.name.c_str(),
                   reinterpret_cast<void*>(targetAddr),
                   detour,
                   h.bytes.size());

        // Diagnostic: read the first 5 bytes at the target site after
        // MinHook claims to have installed. If MinHook actually rewrote
        // the prologue, those bytes will start with E9 (rel32 jmp) or
        // FF 25 (abs64 jmp via [rip+0]). If they're still whatever the
        // function originally started with, MinHook silently failed.
        const uint8_t* siteBytes = reinterpret_cast<const uint8_t*>(targetAddr);
        log::InfoF("[hook '%s'] post-install bytes at target: %02X %02X %02X %02X %02X",
                   h.name.c_str(),
                   siteBytes[0], siteBytes[1], siteBytes[2], siteBytes[3], siteBytes[4]);

        g_installed.emplace(targetAddr, h.name);
        ++installed;
    }

    log::InfoF("Hook engine: %zu of %zu hook(s) installed", installed, g_hooks.size());
    return installed;
}

}  // namespace kcdx::hook_engine
