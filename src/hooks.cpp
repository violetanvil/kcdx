#include "hooks.h"

#include <windows.h>
#include <psapi.h>
#include <intrin.h>  // for PROBE Q: _AddressOfReturnAddress

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf (cap-47 self-report reason)
#include <cstring>  // strcmp  (cap-47 owner-name check)
#include <vector>

#include "MinHook.h"
#include "console.h"
#include "init_phase.h"
#include "modification_inventory.h"
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
#include "mod_absorb/record_synth_selftest.h"  // cap-52 engine self-report
#include "mod_absorb/mod_manifest_selftest.h"  // cap-53 engine self-report
#include "mod_absorb/pak_mod_registry_selftest.h"  // cap-54 engine self-report
#include "mod_absorb/enabled_list_builder_selftest.h"  // cap-55 engine self-report
#include "mod_absorb/order_persist_selftest.h"  // cap-56 engine self-report

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

    // Record this engine self-instrumentation hook in the live modification
    // inventory (PROBE Q / frealloc — the dual-Lua sentinel canary).
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(frealloc_addr),
        kcdx::modification_inventory::Category::Engine, "frealloc");
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
                // Trampolines populate the symbol table so any
                // target_symbol locator resolves correctly. The legacy
                // unified patch+hook apply orchestration that once ran here
                // (conflict_engine::RunPreFlight + a g_applyOrder dispatch
                // loop over g_patches/g_hooks/g_mid_hooks) was removed in the
                // apply-consolidation cut: those TOML-fed vectors have had no
                // populator since Phase 5, so the loop was dead. The live
                // apply path is the kcdx.* surface drained by ApplyZone below
                // (kcdx.bytes/.hook via lua_registry + hook_chain) plus the
                // before_game ldr_notify path. g_applyOrder/RunPreFlight no
                // longer exist.
                kcdx::trampoline_engine::ApplyAll();

                // Dormant scan diagnostic entries — locator-resolve only,
                // no apply. The legacy [[scan]] TOML path that populated
                // g_scans was removed in Phase 5, so g_scans is empty and
                // RunAll is effectively a no-op today; the live scan surface
                // is the kcdx.scan Lua verb. The call stays here (and runs
                // BEFORE patches/hooks apply) so that if a populator ever
                // returns, scans see the pristine pre-patch byte state — a
                // scan whose pattern overlapped an applied byte rewrite would
                // otherwise see post-patch bytes and miss.
                kcdx::scan_engine::RunAll();

                // Emit the engine-modification inventory now that every
                // patch/hook/mid-hook is resolved + applied. SUMMARY at Info
                // (always-on, build-to-build diffable fingerprint); per-target
                // DETAIL at Debug (dev-only). This call ALSO refreshes the
                // cached pre-formatted summary string the crash guard dumps
                // from its SEH handler — so boot and a later crash share the
                // same content. Re-emitted at each save-load start
                // (save_load_hooks.cpp) so the load path has a diffable signal
                // for the 0xC8 load-crash bisect (docs/known-issues/).
                kcdx::modification_inventory::LogInventory(
                    kcdx::log::Level::Info);

                // cap-45: engine self-report for the load-time inventory
                // mechanism (manifest stub at
                // test-plugins/cap-45-load-hook-inventory/). The behavior under
                // test is engine machinery (LogInventory ran + emitted a
                // summary with a nonzero modification count), so the engine
                // reports it directly — same pattern as cap-43/cap-44. Reported
                // right after the boot LogInventory call so it observes the
                // same counts the summary line just emitted.
                //
                // The inventory now reads the LIVE sources (hook_chain::g_chains
                // + the RegisterModification'd fixed engine/lifecycle/probe
                // installs), so total > 0 holds at boot: the engine self-
                // instrumentation hooks (lua_pcall / update / frealloc) alone
                // guarantee a nonzero count even before any plugin hook lands.
                {
                    const size_t total =
                        kcdx::modification_inventory::LastTotalModifications();
                    kcdx::test::ReportResult(
                        "cap-45-load-inventory",
                        total > 0,
                        total > 0
                            ? "boot LogInventory emitted summary; live "
                              "modification inventory count nonzero"
                            : "boot LogInventory ran but the live modification "
                              "inventory is empty — engine self-instrumentation "
                              "hooks should have registered");
                }

                // cap-46: engine self-report for the per-session log-stamp
                // fix (manifest stub at test-plugins/cap-46-session-stamp/).
                // The behavior under test is engine machinery: the dev log
                // must open as "kcdx-dev_<stamp>.log", NOT "kcdx-dev_.log"
                // (empty stamp). The bug was that the DllMain-phase dev-mode
                // enable opened the dev log before log::Init() set the stamp,
                // so every session overwrote one file and the watchdog crash
                // bundle couldn't find "kcdx-dev_<stamp>.log". EnsureSessionStamp
                // now sets the stamp set-once before either log opens, so the
                // engine log + dev log share one stamp. This site runs at boot,
                // long after the dev log is open, so DevLogName() is populated.
                // Engine reports directly — same pattern as cap-43/44/45.
                {
                    const std::string& stamp   = kcdx::log::SessionStamp();
                    const std::string& devName = kcdx::log::DevLogName();
                    const std::string  expected =
                        "kcdx-dev_" + stamp + ".log";
                    // Clause (3) guards the exact pre-fix symptom: the bug
                    // opened the dev log as the empty-stamp filename
                    // "kcdx-dev_.log". This clause fails directly if that ever
                    // recurs, independent of clause (2)'s frozen-name reasoning
                    // and regardless of how DevLogName is later implemented.
                    const bool pass =
                        !stamp.empty()                       // (1)
                        && devName == expected               // (2)
                        && devName != "kcdx-dev_.log";       // (3)
                    kcdx::test::ReportResult(
                        "cap-46-session-stamp",
                        pass,
                        pass
                            ? "session stamp non-empty; dev log filename "
                              "matches kcdx-dev_<stamp>.log (engine + dev "
                              "logs share one stamp)"
                            : (stamp.empty()
                                   ? "session stamp is EMPTY at boot — "
                                     "EnsureSessionStamp did not run before "
                                     "the dev log opened"
                                   : (devName == "kcdx-dev_.log"
                                          ? "dev log opened as the empty-stamp "
                                            "filename kcdx-dev_.log — the "
                                            "pre-fix bug has recurred"
                                          : "dev log filename does not match "
                                            "kcdx-dev_<stamp>.log — stamp "
                                            "mismatch between engine log and "
                                            "dev log")));
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

                // PHASE 10 (ctx C): AfterGameApply — the after_game load-order
                // slice is applied (the ApplyZone(AfterGame) passes above) and
                // the KCDX Lua table is registered (RegisterKcdxTable at the top
                // of this one-shot block). Advanced once, from inside the
                // first-update-tick latch (this whole block runs exactly once per
                // session via the `done` compare_exchange). The per-tick
                // ApplyZone drain below this block is the idempotent steady-state
                // path, not a phase boundary. This is pure instrumentation —
                // no after_game operation was added, removed, or reordered.
                kcdx::init::AdvanceTo(kcdx::init::InitPhase::AfterGameApply);
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

    // cap-47: engine self-report for the crash breadcrumb (Part 2) + the
    // owner-named inventory DETAIL (Part 3). Manifest stub at
    // test-plugins/cap-47-crash-breadcrumb/; the plugin installs a `before`
    // hook on a kcdx.code stub and dynamic_calls it from kcdx.on("ready") so
    // the detour fires and leaves a breadcrumb. Reported from the per-tick
    // update (NOT the first-update-tick one-shot above) because that one-shot
    // runs BEFORE ApplyZone installs plugin hooks and before "ready" fires —
    // the ring is still empty there. The per-tick path runs after both, so by
    // the tick following "ready" the breadcrumb ring is populated. One-shot
    // guarded; retries each tick until a fire is observed so a slow ready
    // doesn't fail it. The culprit-walk (Part 1) needs a real fault and is
    // marked [manual] (covered by the live 0xC8 repro) — not asserted here.
    {
        static bool s_cap47Reported = false;
        if (!s_cap47Reported) {
            namespace mi = kcdx::modification_inventory;
            mi::FireRecord fires[mi::kFireRingSize];
            const unsigned nFires = mi::LastFires(fires, mi::kFireRingSize);

            // (b) inventory DETAIL now names the owner: at least one live
            // chain yields a non-generic, non-empty plugin name (was the
            // hardcoded "kcdx.hook" before Part 3).
            bool ownerNamed = false;
            const char* ownerExample = "";
            for (const auto& t : kcdx::hook_chain::GetAllChainTargets()) {
                if (t.pluginName && t.pluginName[0] &&
                    std::strcmp(t.pluginName, "kcdx.hook") != 0) {
                    ownerNamed = true;
                    ownerExample = t.pluginName;
                    break;
                }
            }

            if (nFires > 0) {
                // (a) the ring recorded a fire after a boot-firing hook ran.
                s_cap47Reported = true;
                const bool pass = ownerNamed;
                char reason[320];
                if (pass) {
                    std::snprintf(reason, sizeof(reason),
                        "breadcrumb ring recorded %u fire(s) (newest: plugin=%s "
                        "hook=%s seq=%llu); inventory chain owner named (e.g. "
                        "plugin=%s) — was generic \"kcdx.hook\" pre-Part-3",
                        nFires,
                        fires[0].pluginName ? fires[0].pluginName : "(none)",
                        fires[0].hookName ? fires[0].hookName : "(none)",
                        (unsigned long long)fires[0].seq, ownerExample);
                } else {
                    std::snprintf(reason, sizeof(reason),
                        "breadcrumb ring recorded %u fire(s) but NO live chain "
                        "yields a non-generic owner plugin name — Part 3's "
                        "GetAllChainTargets owner attribution is not landing",
                        nFires);
                }
                kcdx::test::ReportResult("cap-47-crash-breadcrumb", pass, reason);
                kcdx::test::EmitSummaryIfChanged("cap-47 self-report");
            }
            // nFires == 0: leave s_cap47Reported false; retry next tick (the
            // hook may not have fired yet — ready fires shortly after boot).
        }
    }

    // cap-52-mod-record-synth: engine self-report for the mod_absorb
    // record-synthesis module (src/mod_absorb/record_synth.cpp). Unlike cap-47,
    // it has NO dependency on a hook firing or "ready" — BuildRecord works as
    // soon as the Address Library resolves (available at boot) — so it reports
    // on the first tick. One-shot guarded internally; safe to call every tick.
    kcdx::mod_absorb::RunSelfTestOnce();

    // cap-53-mod-manifest-version-gate: engine self-report for the mod.manifest
    // reader (src/mod_absorb/mod_manifest.cpp) + the shared version-compat
    // helper (src/version_compat.cpp). STEP 2 of mod-loader-absorb. Same timing
    // as cap-52: no hook-fire / "ready" dependency — parses a literal XML string
    // + runs the helper, both work at boot. One-shot guarded internally.
    kcdx::mod_absorb::RunManifestSelfTestOnce();

    // cap-54-pak-mod-registry: engine self-report for the pak-mod registry +
    // the load-order fold + the version gate (src/mod_absorb/pak_mod_registry.cpp
    // + the load_order Resolve fold). STEP 3 of mod-loader-absorb. Same timing
    // as cap-52/53: no hook-fire / "ready" dependency — the parse + fold + gate
    // logic all work at boot. Assertions 3+4 drive the global load_order state
    // and RESTORE it before returning (snapshot + re-Read + re-Resolve), so the
    // live load order is untouched. One-shot guarded internally.
    kcdx::mod_absorb::RunPakRegistrySelfTestOnce();

    // cap-55-enabled-list-builder: engine self-report for the enabled-list
    // builder (src/mod_absorb/enabled_list_builder.cpp) — STEP 4 of
    // mod-loader-absorb. Same timing as cap-52/53/54: no hook-fire / "ready"
    // dependency — the build + normalization logic work at boot. Drives the
    // global load_order + registry + g_manifests state in isolation and RESTORES
    // it before returning. The live MOUNT end-to-end (every enabled mod mounts,
    // in kcdx order) is the batched verification checkpoint, not this self-test.
    // One-shot guarded internally.
    kcdx::mod_absorb::RunEnabledListSelfTestOnce();

    // cap-56-order-persist: engine self-report for order persistence
    // (src/mod_absorb/order_persist.cpp) — STEP 5 of mod-loader-absorb. Same
    // timing as cap-52/53/54/55: no hook-fire / "ready" dependency — the pure
    // string serializers (merge, idempotence, mod_order round-trip, merge-
    // preserve) all work at boot on literals. Touches NO global state (unlike
    // cap-54/55 there is nothing to snapshot/restore). The live on-disk write +
    // write-if-changed skip + fail-loud paths are the batched verification
    // checkpoint, not this self-test. One-shot guarded internally.
    kcdx::mod_absorb::RunOrderPersistSelfTestOnce();

    // cap-39-bytes-in-inventory: engine self-report that a successful
    // kcdx.bytes / kcdxBytesInterface byte rewrite reaches the modification
    // inventory as Category::Bytes (the RegisterModification(Category::Bytes,
    // ...) wiring on patch_engine's apply path — Batch C #15). cap-39's C++
    // plugin (kcdxBytesInterface::Register at outfit_swap_callsite_aob) is the
    // producer; cap-01's Lua kcdx.bytes hits the same site — so a live suite
    // has ≥1 Bytes entry once those byte patches apply.
    //
    // TIMING (mirrors cap-47, NOT the cap-45 boot one-shot): the boot
    // LogInventory() above runs in the first-update-tick block BEFORE the
    // ApplyZone(AfterGame) passes install the byte patches — its bytes count is
    // 0 there. So this report runs from the PER-TICK update path (after the
    // per-tick ApplyZone(AfterGame) drain above), refreshes the inventory with
    // a Debug-level LogInventory() (recomputes LastBytesCount over the now-
    // applied registry), and reads the refreshed count. One-shot guarded +
    // retried each tick: a tick where the bytes patch has not yet applied reads
    // 0 and simply tries again next tick (no false FAIL), exactly like cap-47's
    // empty-ring retry.
    //
    // FALSIFIABLE (AP15): reverting RegisterModification(Category::Bytes,...)
    // on patch_engine's apply/idempotent-skip paths leaves bytes=0 forever →
    // this row never flips to PASS (stays PENDING, a visible suite gap), and if
    // it were asserted unconditionally it would FAIL. It reads the feature's
    // own output (the Bytes subtotal the inventory folds), not a constant.
    {
        static bool s_cap39BytesReported = false;
        if (!s_cap39BytesReported) {
            namespace mi = kcdx::modification_inventory;
            // Refresh so LastBytesCount reflects the byte patches applied by the
            // ApplyZone(AfterGame) passes (Debug: dev-only, no Info-line spam
            // per tick — the always-on boot/load SUMMARY lines stay at Info).
            mi::LogInventory(kcdx::log::Level::Debug);
            const size_t nBytes = mi::LastBytesCount();
            if (nBytes > 0) {
                s_cap39BytesReported = true;
                char reason[256];
                std::snprintf(reason, sizeof(reason),
                    "modification inventory folded %zu Category::Bytes "
                    "entry(ies) — a kcdx.bytes/kcdxBytesInterface rewrite "
                    "(cap-39 / cap-01) registered as bytes; "
                    "RegisterModification(Category::Bytes,...) is live",
                    nBytes);
                kcdx::test::ReportResult("cap-39-bytes-in-inventory", true,
                                         reason);
                kcdx::test::EmitSummaryIfChanged("cap-39 bytes-inventory");
            }
            // nBytes == 0: leave s_cap39BytesReported false; retry next tick
            // (the byte patches may not have applied yet — they install during
            // the ApplyZone(AfterGame) passes around boot).
        }
    }

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

    // Record the engine self-instrumentation hooks in the live modification
    // inventory (category "engine"). These two install on every boot, so the
    // inventory is guaranteed non-empty even before any plugin hook lands.
    kcdx::modification_inventory::RegisterModification(
        pcallAddr, kcdx::modification_inventory::Category::Engine, "lua_pcall");
    kcdx::modification_inventory::RegisterModification(
        updateAddr, kcdx::modification_inventory::Category::Engine, "update");

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
