#include "hooks.h"

#include <windows.h>
#include <psapi.h>
#include <intrin.h>  // for PROBE Q: _AddressOfReturnAddress

#include <atomic>
#include <cstdint>
#include <vector>

#include "MinHook.h"
#include "conflict_engine.h"
#include "console.h"
#include "hook_engine.h"
#include "log.h"
#include "load_order.h"
#include "hook_chain.h"
#include "lua_bind.h"
#include "lua_plugin_loader.h"
#include "lua_registry.h"
#include "messaging.h"
#include "patch_engine.h"
#include "pe_helpers.h"
#include "plugin_loader.h"  // plugins::RunPostGameLoad + GetEngineInterface
#include "scan_engine.h"
#include "scripting.h"
#include "task.h"
#include "test.h"
#include "trampoline_engine.h"

// bugsplat_ctor_probe.h is included from dllmain.cpp now — PROBE T
// installs from kcdx.dll DllMain, not from hooks::Install.

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lstate.h"
}

namespace kcdx::hooks {

namespace {

// Signatures derived from yobson1/kcd2lua (last AOB update 2025-10-03 against
// KCD2 1.3). These may need refresh if the game updates significantly.
const char* PCALL_SIG  = "48 89 5C 24 ? 57 48 83 EC 40 33 C0 41 8B F8";
const char* UPDATE_SIG = "48 8B C4 48 89 58 ? 48 89 70 ? 48 89 78 ? 55 41 54 41 55 41 56 41 57 48 8D A8 ? ? ? ? 48 81 EC ? ? ? ? 0F 29 70 ? 0F 29 78 ? 44 0F 29 40 ? 44 0F 29 48 ? 44 0F 29 50 ? 48 8B 05 ? ? ? ? 48 33 C4 48 89 85 ? ? ? ? 48 8B F1";

std::atomic<lua_State*> g_L{nullptr};

using lua_pcall_t = int(__cdecl*)(lua_State*, int, int, int);
lua_pcall_t g_orig_lua_pcall = nullptr;

using update_t = void(__cdecl*)(long long*, uint32_t, DWORD);
update_t g_orig_update = nullptr;

int __cdecl HookedLuaPcall(lua_State* L, int nargs, int nresults, int errfunc) {
    // === DIAGNOSTIC (PROBE H): log every distinct L that flows through
    // CryEngine's lua_pcall. If multiple distinct L pointers appear, we
    // captured one specific one (the first), but CryEngine uses several.
    // Calling lua_newtable on the wrong L would corrupt unrelated VM
    // state. Throttled: only log the first 8 distinct L values to keep
    // the dev log small.
    static std::atomic<lua_State*> seen[8] = {};
    static std::atomic<int> seen_n{0};
    lua_State* prev = g_L.load(std::memory_order_relaxed);
    g_L.store(L, std::memory_order_relaxed);
    bool new_L = true;
    for (int i = 0; i < 8; ++i) {
        lua_State* s = seen[i].load(std::memory_order_relaxed);
        if (s == L) { new_L = false; break; }
        if (!s) {
            lua_State* expected = nullptr;
            if (seen[i].compare_exchange_strong(expected, L,
                                                std::memory_order_acq_rel)) {
                seen_n.fetch_add(1, std::memory_order_relaxed);
            }
            break;
        }
    }
    if (new_L) {
        LOG_DEBUG_KV("MID_HOOK", "lua_pcall.new_L_seen",
            log::KV("L",        (void*)L),
            log::KV("prev_g_L", (void*)prev),
            log::KV("nargs",    (int64_t)nargs),
            log::KV("nresults", (int64_t)nresults),
            log::KV("seen_n",   (int64_t)seen_n.load()));
    }
    return g_orig_lua_pcall(L, nargs, nresults, errfunc);
}

// === PROBE Q: frealloc interception to verify the dummynode hypothesis ===
//
// Hypothesis: WHGame's Lua eventually calls g->frealloc(g->ud, ptr, ...) on
// our kcdx-static `dummynode_` pointer, mistaking it for a heap allocation.
// PROBE Q hooks the captured `frealloc` and logs any call whose `block`
// parameter falls inside kcdx.dll's image range.
//
// If we see such a call before the heap-corruption crash, hypothesis is
// proven. If we never see it but the crash still happens, the mechanism
// differs and we need another probe.

using lua_Alloc_t = void* (*)(void* ud, void* block, size_t osize, size_t nsize);
lua_Alloc_t g_orig_frealloc = nullptr;

// kcdx.dll image range (resolved at probe-arm time, immutable thereafter).
uintptr_t g_kcdx_image_base = 0;
size_t    g_kcdx_image_size = 0;

// Our static-Lua's `&dummynode_` (resolved by creating a temp Table at
// probe-arm time and reading its t->node). Logged for sanity, then used
// to make the in-range check more useful (any frealloc with block ==
// g_kcdx_dummynode is the exact corruption we predicted).
const void* g_kcdx_dummynode = nullptr;

static bool IsInKcdxImage(const void* p) {
    if (!p || !g_kcdx_image_base) return false;
    uintptr_t addr = reinterpret_cast<uintptr_t>(p);
    return addr >= g_kcdx_image_base &&
           addr <  g_kcdx_image_base + g_kcdx_image_size;
}

static void* __cdecl HookedFrealloc(void* ud, void* block, size_t osize,
                                    size_t nsize) {
    if (IsInKcdxImage(block)) {
        // Log with full context. Don't throttle — this is the smoking gun
        // we've been hunting; we want every instance up to the crash.
        // Caller RA: the actual `_AddressOfReturnAddress()` value is the
        // address of the return-address slot on our stack; the value AT
        // that slot is the return address itself.
        void* ret_slot = _AddressOfReturnAddress();
        void* caller_ra = ret_slot ? *static_cast<void**>(ret_slot) : nullptr;
        LOG_DEBUG_KV("MID_HOOK", "frealloc.kcdx_image_ptr",
            log::KV("ud",         ud),
            log::KV("block",      block),
            log::KV("osize",      (int64_t)osize),
            log::KV("nsize",      (int64_t)nsize),
            log::KV("caller_ra",  caller_ra),
            log::KV("is_dummynode",
                (int64_t)(block == g_kcdx_dummynode ? 1 : 0)));
    }
    // Pass through unchanged so we don't intervene in the corruption
    // chain. If we returned NULL, Lua would throw LUA_ERRMEM and the
    // crash signature changes — masking what we want to observe.
    return g_orig_frealloc(ud, block, osize, nsize);
}

static void ArmFreallocProbe(lua_State* L) {
    // Idempotent: only arm once per game session.
    static std::atomic<bool> armed{false};
    bool expected = false;
    if (!armed.compare_exchange_strong(expected, true,
                                       std::memory_order_acq_rel)) {
        return;
    }

    // Step 1: resolve kcdx.dll image range.
    HMODULE kcdx_mod = nullptr;
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            reinterpret_cast<LPCWSTR>(&HookedFrealloc),
                            &kcdx_mod) || !kcdx_mod) {
        log::Error("PROBE Q: GetModuleHandleEx for kcdx.asi failed");
        return;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), kcdx_mod, &mi, sizeof(mi))) {
        log::Error("PROBE Q: GetModuleInformation failed");
        return;
    }
    g_kcdx_image_base = reinterpret_cast<uintptr_t>(mi.lpBaseOfDll);
    g_kcdx_image_size = mi.SizeOfImage;
    LOG_DEBUG_KV("MID_HOOK", "probe_q.kcdx_image",
        log::KV("base", (void*)g_kcdx_image_base),
        log::KV("size", (int64_t)g_kcdx_image_size),
        log::KV("end",  (void*)(g_kcdx_image_base + g_kcdx_image_size)));

    // Step 2: resolve our static-Lua's dummynode_ by creating a temp
    // Table (which makes t->node = &dummynode_) and reading the field.
    // The Table is then popped, so it's eligible for GC; that's the
    // exact same code path cap-04 exercises, so this is a smaller-scale
    // probe of the same corruption — adding ~1 corrupting Table to the
    // rootgc chain. PROBE N showed one such Table from HookedUpdate's
    // main-thread frame doesn't crash on its own, so this is safe.
    {
        lua_createtable(L, 0, 0);
        void* tbl = const_cast<void*>(lua_topointer(L, -1));
        // Table struct layout per vendor/lua/lobject.h: node field at
        // offset 0x20 (validated by PROBE P hex dumps).
        if (tbl) {
            void** node_field = reinterpret_cast<void**>(
                static_cast<uint8_t*>(tbl) + 0x20);
            g_kcdx_dummynode = *node_field;
        }
        lua_pop(L, 1);
    }
    if (!g_kcdx_dummynode) {
        log::Error("PROBE Q: failed to resolve dummynode pointer");
        return;
    }
    // VirtualQuery the dummynode address to confirm it's in the kcdx.dll image.
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(g_kcdx_dummynode, &mbi, sizeof(mbi)) == 0) {
        log::Error("PROBE Q: VirtualQuery on dummynode failed");
        return;
    }
    wchar_t mod_name[MAX_PATH] = {};
    DWORD mod_name_len = 0;
    if (mbi.AllocationBase) {
        mod_name_len = GetModuleFileNameW(
            reinterpret_cast<HMODULE>(mbi.AllocationBase),
            mod_name, MAX_PATH);
    }
    char mod_name_a[MAX_PATH] = {};
    if (mod_name_len) {
        WideCharToMultiByte(CP_UTF8, 0, mod_name, -1, mod_name_a,
                            MAX_PATH, nullptr, nullptr);
    }
    LOG_DEBUG_KV("MID_HOOK", "probe_q.dummynode",
        log::KV("addr",            (void*)g_kcdx_dummynode),
        log::KV("alloc_base",      (void*)mbi.AllocationBase),
        log::KV("region_base",     (void*)mbi.BaseAddress),
        log::KV("region_size",     (int64_t)mbi.RegionSize),
        log::KV("protect",         (int64_t)mbi.Protect),
        log::KV("state",           (int64_t)mbi.State),
        log::KV("module",          std::string(mod_name_a)),
        log::KV("in_kcdx_image",   (int64_t)(IsInKcdxImage(g_kcdx_dummynode) ? 1 : 0)));

    // Step 3: hook g->frealloc via MinHook.
    global_State* g = L->l_G;
    if (!g || !g->frealloc) {
        log::Error("PROBE Q: g->frealloc is null");
        return;
    }
    void* frealloc_addr = reinterpret_cast<void*>(g->frealloc);
    if (MH_CreateHook(frealloc_addr,
                      reinterpret_cast<LPVOID>(&HookedFrealloc),
                      reinterpret_cast<LPVOID*>(&g_orig_frealloc)) != MH_OK) {
        log::Error("PROBE Q: MH_CreateHook(frealloc) failed");
        return;
    }
    if (MH_EnableHook(frealloc_addr) != MH_OK) {
        log::Error("PROBE Q: MH_EnableHook(frealloc) failed");
        return;
    }
    LOG_DEBUG_KV("MID_HOOK", "probe_q.armed",
        log::KV("frealloc_addr", frealloc_addr),
        log::KV("g",             (void*)g),
        log::KV("g_ud",          g->ud));
}

void __cdecl HookedUpdate(long long* p1, uint32_t p2, DWORD p3) {
    static std::atomic<bool> done{false};
    if (!done.load(std::memory_order_acquire)) {
        lua_State* L = g_L.load(std::memory_order_acquire);
        if (L) {
            bool expected = false;
            if (done.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
                log::Info("First update tick with live lua_State — registering KCDX + applying patches/hooks");
                kcdx::lua_bind::RegisterKcdxTable(L);
                kcdx::scripting::set_lua_state(L);
                kcdx::hook_chain::SetLuaState(L);

                // Phase 2b sub-4: execute each enabled plugin's
                // [entrypoints].lua now that kcdx.* is live + the VM is
                // bound. Their kcdx.hook/.bytes/... calls queue intent
                // into lua_registry; the deferred-apply pass
                // (ApplyZone(AfterGame), below this block) installs
                // everything in unified load order. Runs once per
                // session (internal latch). Each file is SEH-guarded so
                // a faulty plugin.lua can't break the engine.
                kcdx::lua_plugin_loader::RunAll(L);
                // PROBE Q: arm the frealloc interceptor + resolve the
                // kcdx-static dummynode address. Runs once per session.
                // See the function definition above for the design.
                ArmFreallocProbe(L);
                // Unified orchestration:
                //   1. Trampolines populate the symbol table so patch/hook
                //      target_symbol resolves correctly.
                //   2. conflict_engine runs unified pre-flight: resolves
                //      every patch + hook, classifies conflicts, builds
                //      the unified apply order (priority asc, name asc
                //      across ALL entry types).
                //   3. A single sorted loop dispatches per-entry apply
                //      functions. Patches and hooks interleave correctly
                //      by global priority — fixes the v0.1 bug where
                //      patches always applied before hooks regardless
                //      of priority.
                kcdx::trampoline_engine::ApplyAll();
                kcdx::conflict_engine::RunPreFlight();

                // [[scan]] diagnostic entries — locator-resolve only,
                // no apply. Runs BEFORE patches/hooks apply so scans
                // see the pristine pre-patch byte state (a scan whose
                // pattern overlaps an applied [[patch]] would otherwise
                // see post-patch bytes and miss). New-modder onramp
                // per docs/design-gaps.md gap #7.
                kcdx::scan_engine::RunAll();

                size_t okPatches = 0, okHooks = 0;
                size_t totalPatches = kcdx::patch::g_patches.size();
                size_t totalHooks   = kcdx::hook_engine::g_hooks.size();
                if (totalPatches + totalHooks > 0) {
                    log::InfoF("Applying %zu patch(es) + %zu hook(s) in unified load order%s",
                               totalPatches, totalHooks,
                               kcdx::patch::g_dryRun ? " [dry_run=true]" : "");
                    for (const auto& ref : kcdx::conflict_engine::g_applyOrder) {
                        if (ref.kind == kcdx::conflict_engine::EntryKind::Patch) {
                            bool ok = kcdx::patch::ApplyResolvedPatch(
                                kcdx::patch::g_patches[ref.index],
                                kcdx::conflict_engine::g_resolvedPatches[ref.index]);
                            kcdx::patch::g_patches[ref.index].appliedOK = ok;
                            if (ok) ++okPatches;
                        } else {
                            // ApplyOneHook sets appliedOK on the HookEntry
                            // internally before returning true.
                            if (kcdx::hook_engine::ApplyOneHook(ref.index)) {
                                ++okHooks;
                            }
                        }
                    }
                    log::InfoF("Apply summary: %zu/%zu patch(es), %zu/%zu hook(s)",
                               okPatches, totalPatches, okHooks, totalHooks);
                }

                // Phase 5g: mid-hooks. Not part of g_applyOrder yet
                // (conflict_engine doesn't track them in v0.1); apply
                // separately. Each mid-hook resolves its locator inline
                // and goes through hook_engine::InstallRuntime so the
                // global first-wins map still catches direct VA
                // collisions with [[hook]] or runtime kcdx.memory.dynamic_hook
                // installs.
                const size_t totalMidHooks = kcdx::hook_engine::g_mid_hooks.size();
                if (totalMidHooks > 0) {
                    log::InfoF("Applying %zu mid-hook(s)%s",
                               totalMidHooks,
                               kcdx::patch::g_dryRun ? " [dry_run=true]" : "");
                    size_t okMidHooks = 0;
                    for (size_t i = 0; i < totalMidHooks; ++i) {
                        if (kcdx::hook_engine::ApplyOneMidHook(i)) ++okMidHooks;
                    }
                    log::InfoF("Mid-hook summary: %zu/%zu installed",
                               okMidHooks, totalMidHooks);
                }

                // Phase 7: resolve gEnv->pConsole + IConsole::AddCommand/
                // RemoveCommand via the Address Library and arm the
                // [[command]] dispatch surface. After this returns true,
                // plugin RegisterCommand calls succeed.
                kcdx::console::Init();

                // Apply the after_game registration queue NOW — before
                // any "ready" signal. plugin.lua (RunAll, above) queued
                // its kcdx.hook/.bytes registrations; install them in
                // unified load order so they're LIVE before InputLoaded
                // tells plugins "you're ready to run/verify". Steps run
                // in order: register -> apply -> ready. (The per-tick
                // ApplyZone drain below still picks up registrations that
                // arrive later, e.g. from pak Lua callbacks.)
                kcdx::lua_registry::ApplyZone(
                    kcdx::load_order::Zone::AfterGame);

                // Per-entry-zone model: run every plugin's after-game Lua
                // slot ([entrypoints].lua_after) NOW — after the ApplyZone
                // above (so every plugin's before-work is LIVE: hooks +
                // byte patches installed) and BEFORE InputLoaded (so the
                // after-work is done by the time plugins are told they're
                // ready). RunAfterEntrypoints fires each enabled plugin's
                // lua_after files in LOAD-ORDER PRIORITY. This is the clean
                // phase boundary: all BEFORE work registered+applied, THEN
                // all AFTER work runs with before-work live.
                kcdx::lua_plugin_loader::RunAfterEntrypoints(L);

                // A lua_after entrypoint may itself call kcdx.hook/.bytes,
                // which QUEUE into lua_registry AFTER the ApplyZone above
                // already ran. Drain+install them with a SECOND
                // ApplyZone(AfterGame) so they're LIVE before InputLoaded.
                // ApplyZone is idempotent (already-applied entries skip),
                // so this is cheap + correct; the per-tick ApplyZone below
                // would eventually catch them, but we want them live now.
                kcdx::lua_registry::ApplyZone(
                    kcdx::load_order::Zone::AfterGame);

                // C++ parity for lua_after: run every C++ plugin's optional
                // kcdxPlugin_PostGameLoad export NOW — same after_game phase
                // as RunAfterEntrypoints (above), at load-order priority.
                // Fires AFTER all before-game work is applied (the ApplyZone
                // passes above) and BEFORE InputLoaded, so a C++ plugin and a
                // Lua plugin reach the same logical point. Lua after-work and
                // C++ PostGameLoad are two sequential passes (all lua_after by
                // priority, then all PostGameLoad by priority); a Lua and a
                // C++ plugin at the same priority is the only edge case and
                // does not interleave. The DLL load wave's `api` is reused
                // (GetEngineInterface() — same source plugin_loader's load
                // wave uses).
                kcdx::plugins::RunPostGameLoad(
                    kcdx::plugins::GetEngineInterface());

                // Lifecycle: input subsystem is alive by the time the first
                // update tick fires (Lua VM is up). Closest analogue to
                // SKSE's kInputLoaded message.
                log::Info("Firing kcdxMessage_InputLoaded...");
                kcdx::messaging::FireEngineMessage(kcdxMessage_InputLoaded);
            }
        }
    }

    // Drain pending kcdx.* registrations queued by Lua plugin code. New
    // entries arrive any time after kcdxMessage_LuaReady (pak Lua's
    // OnSystemStarted / OnLoadingComplete / etc.) — we apply them here
    // so they participate in the same lifecycle as TOML-declared
    // entries. ApplyZone is idempotent: already-applied / already-
    // failed entries are skipped, so re-running per tick is cheap
    // (no-op when queue is empty). Phase 2a apples after_game zone
    // only; before_game zone applies after Phase 11 lands the early
    // VM startup.
    kcdx::lua_registry::ApplyZone(kcdx::load_order::Zone::AfterGame);

    // Drain the task queue every tick. Plugins that called AddTask from
    // any thread get their tasks executed here, on the main thread.
    kcdx::task::DrainQueue();

    // After tasks ran, if any reported a test result (async path), emit
    // a fresh suite summary. Cheap — no-op when nothing changed since
    // the last emit. Catches CAP-09-style tests that report from a task
    // queued during Plugin_Load (task fires AFTER kInputLoaded summary).
    kcdx::test::EmitSummaryIfChanged("update tick");

    g_orig_update(p1, p2, p3);
}

uintptr_t FindUniqueSig(const pe::ModuleView& mod, const char* sig, const char* label) {
    auto pat = kcdx::patch::ParsePattern(sig);
    auto sections = pe::ExecutableSections(mod);
    std::vector<uintptr_t> all;
    for (const auto& sec : sections) {
        auto offs = kcdx::patch::FindAllInBuffer(sec.data, sec.size, pat);
        for (size_t off : offs) all.push_back(reinterpret_cast<uintptr_t>(sec.data + off));
    }
    if (all.size() != 1) {
        log::ErrorF("%s sig: %zu matches (need exactly 1)", label, all.size());
        return 0;
    }
    log::InfoF("%s found at 0x%p", label, reinterpret_cast<void*>(all[0]));
    return all[0];
}

bool VerifyExecutable(void* p, const char* label) {
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(p, &mbi, sizeof(mbi)) == 0) {
        log::ErrorF("%s VirtualQuery failed", label);
        return false;
    }
    if (mbi.State != MEM_COMMIT ||
        !(mbi.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ |
                         PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))) {
        log::ErrorF("%s not executable memory", label);
        return false;
    }
    return true;
}

}  // namespace

lua_State* CurrentLuaState() {
    return g_L.load(std::memory_order_acquire);
}

bool Install() {
    pe::ModuleView whgame;
    if (!pe::OpenModule(L"WHGame.dll", whgame)) {
        log::Error("WHGame.dll not loaded — refusing to install hooks");
        return false;
    }
    log::InfoF("WHGame.dll base 0x%p size 0x%zx",
               reinterpret_cast<const void*>(whgame.baseBytes), whgame.size);

    uintptr_t pcallAddr  = FindUniqueSig(whgame, PCALL_SIG,  "lua_pcall");
    uintptr_t updateAddr = FindUniqueSig(whgame, UPDATE_SIG, "update");
    if (!pcallAddr || !updateAddr) {
        log::Error("aborting hook install — required sigs not unique");
        return false;
    }
    if (!VerifyExecutable(reinterpret_cast<void*>(pcallAddr), "lua_pcall") ||
        !VerifyExecutable(reinterpret_cast<void*>(updateAddr), "update")) {
        return false;
    }

    // MinHook may already be initialized: PROBE T (and any future
    // before_game-zone hook) calls MH_Initialize from kcdx.dll
    // DllMain so it can install detours before the game's startup
    // code reaches them. Treat ALREADY_INITIALIZED as the no-op
    // success path.
    {
        MH_STATUS mi = MH_Initialize();
        if (mi != MH_OK && mi != MH_ERROR_ALREADY_INITIALIZED) {
            log::ErrorF("MH_Initialize failed: %d", (int)mi);
            return false;
        }
    }

    if (MH_CreateHook(reinterpret_cast<LPVOID>(pcallAddr),
                      reinterpret_cast<LPVOID>(&HookedLuaPcall),
                      reinterpret_cast<LPVOID*>(&g_orig_lua_pcall)) != MH_OK) {
        log::Error("MH_CreateHook(lua_pcall) failed");
        return false;
    }
    if (MH_CreateHook(reinterpret_cast<LPVOID>(updateAddr),
                      reinterpret_cast<LPVOID>(&HookedUpdate),
                      reinterpret_cast<LPVOID*>(&g_orig_update)) != MH_OK) {
        log::Error("MH_CreateHook(update) failed");
        return false;
    }
    if (MH_EnableHook(MH_ALL_HOOKS) != MH_OK) {
        log::Error("MH_EnableHook failed");
        return false;
    }

    log::Info("Hooks installed: lua_pcall + update");

    // bugsplat_ctor_probe (PROBE S/T) install timing note: worker-thread
    // install (PROBE S, retired 2026-05-21) was too late — the ctor fired
    // before this code ran. The install lives in src/dllmain.cpp
    // RunBeforeGameZoneInDllMain via LdrRegisterDllNotification (PROBE T),
    // NOT here. That probe is KEEP-for-Phase-11 (the proven before_game-hook
    // install machinery; see docs/outstanding-work/before-game-hooks.md §5).

    return true;
}

}  // namespace kcdx::hooks
