#include "hook_engine.h"

#include <windows.h>
#include <cstdio>
#include <cstring>
#include <memory>
#include <unordered_map>

#include <asmjit/asmjit.h>

#include "MinHook.h"
#include "conflict_engine.h"
#include "load_order.h"
#include "log.h"
#include "patch_engine.h"  // for Resolve, ResolvedPatch (the locator pipeline)
#include "rom_borrowed/runtime_func_t.h"
#include "scripting.h"
#include "trampoline.h"

// Vendored from MinHook: hde64 instruction-length decoder. Used to auto-
// derive stack_restore_offset for [[mid_hook]] entries when the author
// leaves it at the default (0). The MinHook static lib already compiles
// hde64.c into the build; we just need the header to call hde64_disasm.
extern "C" {
#include "hde/hde64.h"
}

namespace kcdx::hook_engine {

std::vector<HookEntry>    g_hooks;
std::vector<MidHookEntry> g_mid_hooks;

namespace {

// Track which target addresses already have a hook installed, so we can
// detect hook-on-hook collisions in v0.1's first-wins policy. Maps
// targetAddr -> name of the first hook that grabbed it.
std::unordered_map<uintptr_t, std::string> g_installed;

// Storage for TOML-lua_callback hooks' runtime_func_t instances.
// Keyed by target VA. Lifetime: process. The runtime_func_t holds
// the JIT'd trampoline pointer + asmjit state; destroying it would
// disable the MinHook entry, which we never want in v0.1 (hooks
// live for the session, matching SKSE).
std::unordered_map<uintptr_t,
                   std::unique_ptr<kcdx::rom::runtime_func_t>> g_runtime_funcs;

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
    p.targetSymbol = h.targetSymbol;
    p.addressId = h.addressId;
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
    HookEntry& h = g_hooks[hookIdx];

    // load_order.toml disabled gate. Production orchestration in
    // hooks.cpp filters disabled plugins out of g_applyOrder so we
    // never reach this check by that path, but the ApplyAll fallback
    // (used by tests, the Lua runtime path, and any future caller
    // bypassing conflict_engine) still hits ApplyOneHook directly.
    if (!load_order::IsPluginEnabled(h.pluginName)) {
        log::InfoF("[%s] skipping hook '%s' (plugin disabled via load_order.toml)",
                   h.pluginName.c_str(), h.name.c_str());
        return false;
    }

    // conflict_engine::RunPreFlight should have populated g_resolvedHooks
    // already. If we're being called outside that orchestration (tests,
    // future runtime paths), fall back to local resolution for this entry.
    if (hookIdx >= conflict_engine::g_resolvedHooks.size()) {
        log::Warn("Hook engine: conflict_engine pre-flight not run, "
                  "resolving hook locally (no cross-engine conflict matrix).");
        conflict_engine::g_resolvedHooks.resize(g_hooks.size());
        if (h.bytes.empty() && h.lua_callback.empty()) {
            conflict_engine::g_resolvedHooks[hookIdx].reason =
                "neither 'bytes' nor 'lua_callback' set";
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

    // ---------------------------------------------------------------------
    // Phase 5f branch: lua_callback hooks route through the runtime_func_t
    // JIT machinery + hook_engine::InstallRuntime + scripting::register_*_by_name.
    // Same path as kcdx.memory.dynamic_hook but driven by TOML.
    if (!h.lua_callback.empty()) {
        auto rf = std::make_unique<kcdx::rom::runtime_func_t>();
        uintptr_t jit_addr = rf->make_jit_func(
            h.return_type,
            h.param_types,
            asmjit::Arch::kX64,
            &kcdx::scripting::dynamic_hook_pre,
            &kcdx::scripting::dynamic_hook_post,
            targetAddr);
        if (!jit_addr) {
            log::ErrorF("[hook '%s'] aborted: make_jit_func failed "
                        "(check return_type='%s' / param_types signature)",
                        h.name.c_str(), h.return_type.c_str());
            return false;
        }

        // Install via InstallRuntime (handles MinHook + first-wins +
        // g_installed bookkeeping; same path as kcdx.memory.dynamic_hook).
        auto install = InstallRuntime(h.name, targetAddr, (void*)jit_addr);
        if (!install.ok) {
            log::ErrorF("[hook '%s'] InstallRuntime failed: %s",
                        h.name.c_str(), install.reason.c_str());
            return false;
        }

        // Wire MinHook's pOriginal into the JIT'd trampoline's
        // call-original slot — see ApplyOneMidHook for the long
        // explanation. Same fix; both paths bypass m_detour->enable().
        if (void** slot = rf->get_jit_original_slot()) {
            *slot = install.pOriginal;
        }

        // Register the runtime_func_t with scripting so the dispatchers
        // can resolve target_func_ptr -> param_types for arg marshaling.
        kcdx::scripting::register_hook(targetAddr, rf.get());
        kcdx::scripting::register_pre_callback_by_name(targetAddr, h.lua_callback);
        if (!h.lua_post_callback.empty()) {
            kcdx::scripting::register_post_callback_by_name(targetAddr, h.lua_post_callback);
        }

        // Stable storage so the runtime_func_t survives this scope.
        g_runtime_funcs[targetAddr] = std::move(rf);

        log::InfoF("[hook '%s'] lua_callback='%s' wired (target 0x%p, "
                   "JIT detour 0x%p)",
                   h.name.c_str(), h.lua_callback.c_str(),
                   reinterpret_cast<void*>(targetAddr), (void*)jit_addr);
        return true;
    }
    // ---------------------------------------------------------------------

    // Hook-on-hook collision check (first-wins). Raw-bytes path only;
    // the lua_callback branch above runs the same check inside
    // InstallRuntime.
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
    LOG_DEBUG("HOOK", "[%s] post-install bytes at target: %02X %02X %02X %02X %02X",
              h.name.c_str(),
              siteBytes[0], siteBytes[1], siteBytes[2], siteBytes[3], siteBytes[4]);

    g_installed.emplace(targetAddr, h.name);
    h.appliedOK = true;
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

bool ApplyOneMidHook(size_t midHookIdx) {
    if (midHookIdx >= g_mid_hooks.size()) return false;
    const MidHookEntry& mh = g_mid_hooks[midHookIdx];

    // load_order.toml disabled gate.
    if (!load_order::IsPluginEnabled(mh.pluginName)) {
        log::InfoF("[%s] skipping mid_hook '%s' (plugin disabled via load_order.toml)",
                   mh.pluginName.c_str(), mh.name.c_str());
        return false;
    }

    // Mid-hooks resolve their target inline. They don't participate in
    // conflict_engine pre-flight today (Phase 5g v0.1 limitation;
    // future work could lift the same first-wins matrix to cover them,
    // but the install-time first-wins via InstallRuntime is sufficient
    // for v0.1 since mid-hook collisions are rare in practice).
    patch::PatchEntry locator;
    locator.sourceFile        = mh.sourceFile;
    locator.name              = mh.name;
    locator.module            = mh.module;
    locator.pattern           = mh.pattern;
    locator.targetSymbol      = mh.targetSymbol;
    locator.addressId         = mh.addressId;
    locator.context           = mh.context;
    locator.anchor            = mh.anchor;
    locator.maxAnchorDistance = mh.maxAnchorDistance;
    locator.offset            = mh.offset;
    locator.original.clear();
    locator.replacement.clear();
    patch::ResolvedPatch r = patch::Resolve(locator);
    if (!r.ok) {
        log::ErrorF("[mid_hook '%s'] aborted: %s",
                    mh.name.c_str(), r.reason.c_str());
        return false;
    }
    uintptr_t targetAddr = r.patchAddr;

    if (mh.param_types.size() != mh.param_captures.size()) {
        log::ErrorF("[mid_hook '%s'] aborted: param_types (%zu) and "
                    "param_captures (%zu) length mismatch",
                    mh.name.c_str(),
                    mh.param_types.size(), mh.param_captures.size());
        return false;
    }
    if (mh.lua_callback.empty()) {
        log::ErrorF("[mid_hook '%s'] aborted: lua_callback is required",
                    mh.name.c_str());
        return false;
    }

    // Auto-decode stack_restore_offset when the author left it at 0.
    //
    // MinHook patches AT LEAST 5 bytes at the hook site (the size of a
    // near-rel32 jmp on x64; it's the smallest direct-jump encoding
    // that reaches anywhere in the 64-bit address space). If the
    // CAPTURED instruction is < 5 bytes (e.g., a 4-byte add or 3-byte
    // mov), MinHook patches additional following bytes too so the
    // total is ≥ 5. Resume after the hook MUST land on an instruction
    // boundary past that whole patched region — not just past the
    // captured instruction. Otherwise `ret` jumps into the middle of
    // MinHook's rel32 displacement or a half-consumed instruction.
    //
    // Algorithm: hde64-disassemble forward from target until the
    // accumulated length is ≥ 5 bytes. That's the resume offset.
    // Author can still override by setting stack_restore_offset
    // explicitly in their TOML — if they pre-computed a value < 5,
    // we trust them (some hook targets are inside a longer prologue
    // where MinHook can patch within a single instruction's bytes).
    constexpr int kMinHookPatchBytes = 5;
    int stack_restore_offset = mh.stack_restore_offset;
    if (stack_restore_offset == 0) {
        uintptr_t scan = targetAddr;
        int accumulated = 0;
        while (accumulated < kMinHookPatchBytes) {
            hde64s hs{};
            unsigned int len = hde64_disasm(
                reinterpret_cast<const void*>(scan), &hs);
            if (len == 0 || (hs.flags & F_ERROR) != 0) {
                log::ErrorF("[mid_hook '%s'] aborted: hde64_disasm failed "
                            "at target+%d (0x%p, flags=0x%x); set "
                            "stack_restore_offset explicitly in TOML",
                            mh.name.c_str(), accumulated,
                            reinterpret_cast<void*>(scan), hs.flags);
                return false;
            }
            scan += len;
            accumulated += static_cast<int>(len);
        }
        stack_restore_offset = accumulated;
        LOG_DEBUG_KV("MID_HOOK", "ApplyOneMidHook.auto_decoded_offset",
            log::KV("name",                 mh.name),
            log::KV("target",               (void*)targetAddr),
            log::KV("stack_restore_offset", (int64_t)stack_restore_offset));
    }

    // resume_addr is what `ret` jumps to when skipping the captured
    // instruction (call_original=false or auto-with-_skip). It must
    // be PAST the captured instruction — that is, target_addr +
    // stack_restore_offset. For call_original=true this value is
    // unused by codegen.
    uintptr_t resume_addr = targetAddr + stack_restore_offset;

    // skip_flag_addr — only meaningful for Auto mode. We pass it
    // unconditionally; codegen only reads it when mode==2.
    uintptr_t skip_flag_addr = reinterpret_cast<uintptr_t>(
        kcdx::scripting::get_mid_skip_flag_address());

    // Build the JIT trampoline.
    auto rf = std::make_unique<kcdx::rom::runtime_func_t>();
    uintptr_t jit_addr = rf->make_jit_midfunc(
        mh.param_types,
        mh.param_captures,
        stack_restore_offset,
        static_cast<int>(mh.callOriginal),
        skip_flag_addr,
        resume_addr,
        asmjit::Arch::kX64,
        &kcdx::scripting::dynamic_hook_mid,
        targetAddr);
    if (!jit_addr) {
        log::ErrorF("[mid_hook '%s'] aborted: make_jit_midfunc failed "
                    "(check param_captures syntax — see kcdx.log for asmjit error)",
                    mh.name.c_str());
        return false;
    }

    auto install = InstallRuntime(mh.name, targetAddr, (void*)jit_addr);
    if (!install.ok) {
        log::ErrorF("[mid_hook '%s'] InstallRuntime failed: %s",
                    mh.name.c_str(), install.reason.c_str());
        return false;
    }

    LOG_DEBUG_KV("MID_HOOK", "ApplyOneMidHook.installed",
        log::KV("name",       mh.name),
        log::KV("target",     (void*)targetAddr),
        log::KV("jit_detour", (void*)jit_addr),
        log::KV("pOriginal",  install.pOriginal),
        log::KV("rf",         (void*)rf.get()));

    // CRITICAL: the JIT'd trampoline reads `[&m_detour->original_]`
    // at runtime to find the call-original address. Because we bypass
    // m_detour->enable() (InstallRuntime calls MH_CreateHook directly
    // for first-wins coordination with TOML hooks), m_detour->original_
    // never gets set by detour_hook::enable(). Write it now from the
    // pOriginal MinHook returned via RuntimeInstallResult, so the
    // trampoline's push reads the right value.
    if (void** slot = rf->get_jit_original_slot()) {
        *slot = install.pOriginal;
        LOG_DEBUG_KV("MID_HOOK", "ApplyOneMidHook.original_slot_written",
            log::KV("name",       mh.name),
            log::KV("slot_addr",  (void*)slot),
            log::KV("slot_value", install.pOriginal));
    }

    // Wire scripting: dispatchers need the runtime_func_t for
    // param_types lookup; the callback name is resolved lazily on fire.
    kcdx::scripting::register_hook(targetAddr, rf.get());
    kcdx::scripting::register_mid_callback_by_name(targetAddr, mh.lua_callback);

    LOG_DEBUG_KV("MID_HOOK", "ApplyOneMidHook.exit",
        log::KV("name",       mh.name),
        log::KV("target",     (void*)targetAddr),
        log::KV("jit_buf",    rf->get_jit_buffer()),
        log::KV("jit_size",   (int64_t)rf->get_jit_size()),
        log::KV("fnv_jit",    rf->fingerprint_jit_buffer()),
        log::KV("fnv_self",   rf->fingerprint_self()),
        log::KV("fnv_detour", rf->fingerprint_detour()));

    g_runtime_funcs[targetAddr] = std::move(rf);

    const char* mode_name =
        (mh.callOriginal == CallOriginalMode::True)  ? "true"  :
        (mh.callOriginal == CallOriginalMode::False) ? "false" : "auto";
    log::InfoF("[mid_hook '%s'] lua_callback='%s' wired (target 0x%p, "
               "JIT detour 0x%p, %zu captures, stack_restore_offset=%d, "
               "call_original=%s, resume_addr=0x%p)",
               mh.name.c_str(), mh.lua_callback.c_str(),
               reinterpret_cast<void*>(targetAddr), (void*)jit_addr,
               mh.param_captures.size(), stack_restore_offset,
               mode_name, reinterpret_cast<void*>(resume_addr));
    return true;
}

// Diagnostic helper: walk g_mid_hooks + g_runtime_funcs to (a) log the
// current fingerprint of each installed mid-hook's JIT buffer, and
// (b) compare against the fingerprint captured at install time.
// Called from save_load_hooks just before HookedLoadGameWrapper passes
// control to the engine's deserializer, so we can pin down whether the
// mid-hook JIT buffers got overwritten between install and save-load.
void DumpMidHookFingerprints(const char* label) {
    LOG_DEBUG_KV("MID_HOOK", "fingerprint_scan.begin",
        log::KV("label", std::string(label ? label : "")),
        log::KV("count", (int64_t)g_mid_hooks.size()));
    for (const auto& mh : g_mid_hooks) {
        // Each mid_hook's runtime_func_t is keyed in g_runtime_funcs by
        // target VA. Resolve via the locator; if the mid-hook never
        // applied (Resolve.ok was false), there's nothing to fingerprint.
        patch::PatchEntry locator;
        locator.sourceFile        = mh.sourceFile;
        locator.name              = mh.name;
        locator.module            = mh.module;
        locator.pattern           = mh.pattern;
        locator.targetSymbol      = mh.targetSymbol;
        locator.addressId         = mh.addressId;
        locator.context           = mh.context;
        locator.anchor            = mh.anchor;
        locator.maxAnchorDistance = mh.maxAnchorDistance;
        locator.offset            = mh.offset;
        locator.original.clear();
        locator.replacement.clear();
        patch::ResolvedPatch r = patch::Resolve(locator);
        if (!r.ok) continue;
        auto it = g_runtime_funcs.find(r.patchAddr);
        if (it == g_runtime_funcs.end()) continue;
        const auto& rf = it->second;
        LOG_DEBUG_KV("MID_HOOK", "fingerprint",
            log::KV("label",       std::string(label ? label : "")),
            log::KV("name",        mh.name),
            log::KV("target",      (void*)r.patchAddr),
            log::KV("jit_buf",     rf->get_jit_buffer()),
            log::KV("fnv_jit",     rf->fingerprint_jit_buffer()),
            log::KV("fnv_self",    rf->fingerprint_self()),
            log::KV("fnv_detour",  rf->fingerprint_detour()));
    }
    LOG_DEBUG_KV("MID_HOOK", "fingerprint_scan.end",
        log::KV("label", std::string(label ? label : "")));
}

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
    // runtime_func_t::make_jit_func now routes through branch_pool
    // since Phase 5c.7b.1).
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
