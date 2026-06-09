#include "hooks.h"

#include <windows.h>
#include <psapi.h>
#include <intrin.h>  // for the frealloc canary: _AddressOfReturnAddress

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf (cap-47 self-report reason)
#include <cstring>  // strcmp  (cap-47 owner-name check)

#include "MinHook.h"
#include "asset_namespace.h"  // NotifyVmReady — freeze the KI-0005 boot-opened set
#include "console.h"
#include "cvar.h"
#include "console_commands_scan.h"
#include "init_phase.h"
#include "modification_inventory.h"
#include "log.h"
#include "load_order.h"
#include "hook_chain.h"
#include "hook_payload.h"
#include "hook_signature.h"
#include "lua_bind.h"
#include "lua_plugin_loader.h"
#include "lua_registry.h"
#include "messaging.h"
#include "plugin_loader.h"  // plugins::RunPostGameLoad + GetEngineInterface
#include "refdb.h"
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
#include "mod_absorb/mod_absorb_e2e_selftest.h"  // cap-57 engine self-report
#include "mod_absorb/record_validate_selftest.h"  // cap-58 engine self-report
#include "blake3_selftest.h"                       // cap-59 engine self-report
#include "version_check_selftest.h"                // cap-60 engine self-report
#include "ki0001_node_classifier_selftest.h"       // cap-66 KI-0001 regression
#include "statement_resolve_selftest.h"             // cap-83 refdb statement-resolution API
#include "lua_shim_selftest.h"                      // cap-79 Lua shim forward layer
#include "early_hook_selftest.h"                    // cap-80 early-hook primitive
#include "cap81_vm_adopt_selftest.h"                // cap-81 keystone: VM build + engine adopt
#include "foreign_hook_detect_selftest.h"           // comp-18 foreign-hook prologue classifier

// early_hook.h is included from dllmain.cpp now — the BugSplat ctor hook
// installs from kcdx.dll DllMain (early_hook::bugsplat::Arm), not from
// hooks::Install.

extern "C" {
#include "lua.h"
#include "lauxlib.h"
#include "lstate.h"
}

namespace kcdx::hooks {

namespace {

std::atomic<lua_State*> g_L{nullptr};

using update_t = void(__cdecl*)(long long*, uint32_t, DWORD);
update_t g_orig_update = nullptr;

// === Engine-direct lua_pcall hook (migrated from raw MH_CreateHook to
// hook_chain::AddCEngine in the engine-direct migration) ===
//
// Pre-migration this was a direct MinHook detour calling g_orig_lua_pcall.
// Post-migration this is a Before-mode chain entry registered as
// engine.lua_pcall via AddCEngine; the chain owns the MinHook detour and
// runs the original after every chain entry's Before callback.
//
// KEYSTONE CHANGE: g_L is now AUTHORITATIVELY published by
// the worker thread (lua_vm_build::BuildAndAdoptVM → hooks::PublishLuaState,
// RELEASE) — kcdx builds the one VM, the engine adopts it. So this hook's
// historical g_L.store is now a GUARDED CONFIRMATION, not the publisher:
//   - If g_L already holds the worker-built state (the expected keystone path),
//     this asserts the engine's incoming L EQUALS it. A MISMATCH means the
//     engine's lua_pcall is running on a DIFFERENT lua_State than the one kcdx
//     built and the intercept was supposed to make Init adopt — i.e. a silent
//     SECOND VM. That is a hard failure (the exact dual-Lua hazard the keystone
//     kills); fail LOUD (Error) and do NOT overwrite the authoritative g_L.
//   - If g_L is still null (the worker build failed / the intercept never armed),
//     fall back to the legacy CAPTURE behavior: store L (release) so the live
//     bootstrap path stays intact (HookedUpdate's first-tick latch reads g_L →
//     hook_chain::SetLuaState; without it no Lua callback ever fires). This keeps
//     the engine-builds-its-own-VM fallback working — the keystone step does not
//     remove any fallback (design §VM acquisition).
//
// HookedUpdate's first-tick SetLuaState bootstrap (g_L.load ACQUIRE) is UNCHANGED
// — it now reads the worker-published state on the keystone path, or the
// captured state on the fallback path.
//
// The distinct-L diagnostic stays as-is (it was the dual-Lua sentinel canary;
// noise-throttled to first 8 Ls), and is the breadcrumb that pairs with the loud
// mismatch ERROR below.
//
// Per-mode ABI: Before is `void cFn(uintptr_t args[], int* outCount,
// /* typed args... */)`. The Before mode never returns a value to the
// hooked function (the chain runs the original after the Before chain
// completes), so the callback is void.
//
// Signature: "i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)" — same
// as the verified row in data/seeds/address_versions_seed.csv kcdx_id=1.
extern "C" void HookedLuaPcall_Engine(uintptr_t args[], int* /*outCount*/,
                                      lua_State* L,
                                      int /*nargs*/, int /*nresults*/,
                                      int /*errfunc*/) {
    (void)args;

    // Guarded-confirm vs. fallback-capture (load-bearing — see hook-engine.md
    // §Engine-owned chain entries + the keystone change note above).
    lua_State* published = g_L.load(std::memory_order_acquire);
    if (published != nullptr) {
        // Keystone path: the worker authoritatively published g_L. CONFIRM the
        // engine's state matches; loud on a mismatch. Do NOT store (the worker
        // owns g_L). Mismatch fires ONCE per distinct bad L (the seen[] guard
        // below throttles the per-frame lua_pcall flood).
        if (published != L) {
            static std::atomic<lua_State*> mismatchSeen[8] = {};
            bool firstMismatch = true;
            for (int i = 0; i < 8; ++i) {
                lua_State* s = mismatchSeen[i].load(std::memory_order_relaxed);
                if (s == L) { firstMismatch = false; break; }
                if (!s) {
                    lua_State* expected = nullptr;
                    if (mismatchSeen[i].compare_exchange_strong(
                            expected, L, std::memory_order_acq_rel)) {
                    }
                    break;
                }
            }
            if (firstMismatch) {
                LOG_ERROR_KV("MID_HOOK", "lua_pcall.divergent_L",
                    log::KV("engine_L",   (void*)L),
                    log::KV("kcdx_g_L",   (void*)published),
                    log::KV("detail",
                        "the engine's lua_pcall is running on a DIFFERENT "
                        "lua_State than the one kcdx built + published — the "
                        "lua_newstate intercept did NOT make CScriptSystem::Init "
                        "adopt kcdx's state, so a SECOND VM exists (the dual-Lua "
                        "hazard the keystone kills). kcdx does NOT overwrite the "
                        "authoritative g_L; this is a hard adoption failure."));
            }
        }
        return;
    }

    // Fallback-capture path: g_L still null (worker build failed / intercept
    // never armed). Store L (release) so the legacy bootstrap chain stays intact
    // (lua_pcall captures L -> update tick reads L -> chain dispatchers bind).
    static std::atomic<lua_State*> seen[8] = {};
    static std::atomic<int> seen_n{0};
    lua_State* prev = g_L.load(std::memory_order_relaxed);
    g_L.store(L, std::memory_order_release);
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
            log::KV("seen_n",   (int64_t)seen_n.load()));
    }
}

// === FREALLOC CANARY: frealloc interception to verify the dummynode hypothesis ===
//
// Hypothesis: WHGame's Lua eventually calls g->frealloc(g->ud, ptr, ...) on
// our kcdx-static `dummynode_` pointer, mistaking it for a heap allocation.
// The canary hooks the captured `frealloc` and logs any call whose `block`
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
        log::Error("frealloc canary: GetModuleHandleEx for kcdx.asi failed");
        return;
    }
    MODULEINFO mi{};
    if (!GetModuleInformation(GetCurrentProcess(), kcdx_mod, &mi, sizeof(mi))) {
        log::Error("frealloc canary: GetModuleInformation failed");
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
    // rootgc chain. A prior probe showed one such Table from HookedUpdate's
    // main-thread frame doesn't crash on its own, so this is safe.
    {
        lua_createtable(L, 0, 0);
        void* tbl = const_cast<void*>(lua_topointer(L, -1));
        // Table struct layout per vendor/lua/lobject.h: node field at
        // offset 0x20 (validated by hex dumps against the binary).
        if (tbl) {
            void** node_field = reinterpret_cast<void**>(
                static_cast<uint8_t*>(tbl) + 0x20);
            g_kcdx_dummynode = *node_field;
        }
        lua_pop(L, 1);
    }
    if (!g_kcdx_dummynode) {
        log::Error("frealloc canary: failed to resolve dummynode pointer");
        return;
    }
    // VirtualQuery the dummynode address to confirm it's in the kcdx.dll image.
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(g_kcdx_dummynode, &mbi, sizeof(mbi)) == 0) {
        log::Error("frealloc canary: VirtualQuery on dummynode failed");
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
        log::Error("frealloc canary: g->frealloc is null");
        return;
    }
    void* frealloc_addr = reinterpret_cast<void*>(g->frealloc);
    if (MH_CreateHook(frealloc_addr,
                      reinterpret_cast<LPVOID>(&HookedFrealloc),
                      reinterpret_cast<LPVOID*>(&g_orig_frealloc)) != MH_OK) {
        log::Error("frealloc canary: MH_CreateHook(frealloc) failed");
        return;
    }
    if (MH_EnableHook(frealloc_addr) != MH_OK) {
        log::Error("frealloc canary: MH_EnableHook(frealloc) failed");
        return;
    }
    LOG_DEBUG_KV("MID_HOOK", "probe_q.armed",
        log::KV("frealloc_addr", frealloc_addr),
        log::KV("g",             (void*)g),
        log::KV("g_ud",          g->ud));

    // Record this engine self-instrumentation hook in the live modification
    // inventory (the frealloc / dual-Lua sentinel canary).
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
                log::Info("First update tick with live lua_State — registering kcdx + applying patches/hooks");

                // KI-0005: FREEZE the boot-opened-vpath set NOW — the engine's Lua
                // VM is captured (this latch is the VM-up boundary), so every asset
                // the engine opened during the boot window has been recorded, and
                // plugin.lua (RunAll below) is about to run. Freezing here, BEFORE
                // RunAll, guarantees a plugin's kcdx.assets.register/replace sees a
                // frozen set when it checks WasBootOpened (the warn-check). The latch
                // (done) makes this one-shot; NotifyVmReady is idempotent regardless.
                kcdx::asset_namespace::NotifyVmReady();

                kcdx::lua_bind::RegisterKcdxTable(L);
                kcdx::scripting::set_lua_state(L);
                kcdx::hook_chain::SetLuaState(L);

                // Execute each enabled plugin's
                // [entrypoints].lua now that kcdx.* is live + the VM is
                // bound. Their kcdx.hook/.bytes/... calls queue intent
                // into lua_registry; the deferred-apply pass
                // (ApplyZone(AfterGame), below this block) installs
                // everything in unified load order. Runs once per
                // session (internal latch). Each file is SEH-guarded so
                // a faulty plugin.lua can't break the engine.
                kcdx::lua_plugin_loader::RunAll(L);
                // Arm the frealloc canary + resolve the
                // kcdx-static dummynode address. Runs once per session.
                // See the function definition above for the design.
                ArmFreallocProbe(L);

                // Trampolines populate the symbol table so any
                // target_symbol locator resolves correctly. The legacy
                // unified patch+hook apply orchestration that once ran here
                // (conflict_engine::RunPreFlight + a g_applyOrder dispatch
                // loop over g_patches/g_hooks/g_mid_hooks) was removed in the
                // apply-consolidation cut: those TOML-fed vectors have had no
                // populator after the TOML behavior tables were removed, so
                // the loop was dead. The live
                // apply path is the kcdx.* surface drained by ApplyZone below
                // (kcdx.bytes/.hook via lua_registry + hook_chain) plus the
                // before_game ldr_notify path. g_applyOrder/RunPreFlight no
                // longer exist.
                kcdx::trampoline_engine::ApplyAll();

                // Dormant scan diagnostic entries — locator-resolve only,
                // no apply. The legacy [[scan]] TOML path that populated
                // g_scans was removed, so g_scans is empty and
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
                // for the 0xC8 load-crash bisect.
                kcdx::modification_inventory::LogInventory(
                    kcdx::log::Level::Info);

                // cap-45: engine self-report for the load-time inventory
                // mechanism (manifest stub at
                // test-plugins/cap-45-load-hook-inventory/). The behavior under
                // test is engine machinery (LogInventory ran + emitted a
                // summary with a nonzero modification count), so the engine
                // reports it directly — same pattern as cap-44. Reported
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
                // Engine reports directly — same pattern as cap-44/45.
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

                // Resolve gEnv->pConsole + IConsole::AddCommand/
                // RemoveCommand via the Address Library and arm the
                // kcdx.command dispatch surface. After this returns true,
                // plugin RegisterCommand calls succeed.
                kcdx::console::Init();

                // Arm the kcdx.cvar.* read surface alongside console::Init() —
                // it shares the gEnv->pConsole availability precondition
                // (same console-ready first-update-tick latch). After this,
                // cvar::Get{Int,Float} can read game CVars by name.
                kcdx::cvar::Init();

                // Register the engine-owned kcdx_scan console command now that
                // the console surface is armed. Sits after Init() so the
                // command registers immediately (Init failure accept-defers it
                // through the same queue plugin commands use, then drops it
                // loudly if the surface never comes up).
                kcdx::console_commands_scan::Register();

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

                // STEP 10 (ctx C): AfterGameApply — the after_game load-order
                // slice is applied (the ApplyZone(AfterGame) passes above) and
                // the kcdx Lua table is registered (RegisterKcdxTable at the top
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
    // (no-op when queue is empty). This applies the after_game zone
    // only; the before_game zone applies once the early VM startup
    // lands.
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

    // cap-57-mod-absorb-e2e: engine self-report for the END-TO-END mod-loader-
    // absorb regression net (closeout) — src/mod_absorb/mod_absorb_e2e_selftest.cpp.
    // Same timing as cap-52..56: no hook-fire / "ready" dependency — by the
    // first tick, discovery + load_order::Resolve + the version gate have run,
    // so the live resolved state is final. Assertion 1 READS that live state
    // read-only (the discovery->registry->fold contract for every real mods/
    // pak mod, vacuous on an empty mods/); assertions 2-3 drive the global
    // registry + load_order state in isolation (synthetic Discover root + a
    // synthetic resolved set) and RESTORE it verbatim before returning. The
    // native MOUNT end-to-end is the batched verification checkpoint, not this
    // self-test. One-shot guarded internally.
    kcdx::mod_absorb::RunModAbsorbE2ESelfTestOnce();

    // cap-58-record-validate: engine self-report for the synthesized-record
    // validator (src/mod_absorb/record_validate.cpp) — the takeover self-
    // validation guard. Same timing as cap-52..57: no hook-fire / "ready"
    // dependency — BuildRecord + the validator work at boot. The test builds a
    // well-formed record and asserts ACCEPT, then constructs deliberately-
    // malformed records (corrupted CryString nLength; nulled vtable) and asserts
    // REJECT — the reject cases prove the guard checks the invariants rather than
    // passing everything. One-shot guarded internally.
    kcdx::mod_absorb::RunRecordValidateSelfTestOnce();

    // cap-59-blake3-vectors: engine self-report for the BLAKE3 wrapper
    // (src/blake3.cpp) over the vendored portable BLAKE3. Same timing as
    // cap-52..58: no hook-fire / "ready" dependency — the hash is deterministic
    // and works at boot. Hashes all 35 official BLAKE3 test-vector inputs and
    // asserts the first 32 bytes match the canonical digest; this is the
    // falsifiable proof the port is byte-identical to the canonical algorithm,
    // which the survival check (src/survival.cpp) depends on. One-shot guarded
    // internally.
    kcdx::blake3::RunSelfTestOnce();

    // cap-60-version-check-cache: engine self-report for the per-version
    // survival-verification cache (src/version_check_cache.cpp) + the unified
    // survival pass (src/survival_pass.cpp). Same timing as cap-52..59: no
    // hook-fire / "ready" dependency — all three sub-checks (cache codec round-
    // trip, invalidation forces a miss, the pass records CannotCheck for a non-
    // byte ref + surfaces the posture) run on SYNTHETIC data at boot, with no
    // live-resolution dependency (the pass is not yet wired into the live apply
    // path). Drives + RESETS both modules' in-memory state in isolation and
    // leaves an empty on-disk cache behind. One-shot guarded internally.
    kcdx::version_check_selftest::RunSelfTestOnce();

    // cap-66-node-classifier: KI-0001 permanent regression. Asserts the
    // vendored-Lua crash guard (kcdx_node_freeable in vendor/lua/ltable.c)
    // classifies a module-image (.rdata) sentinel as NOT-freeable, so kcdx's GC
    // never hands a foreign Lua copy's dummynode to frealloc (the 0xC0000374
    // save-load heap corruption). Boot-only, one-shot guarded internally.
    kcdx::ki0001::RunSelfTestOnce();

    // cap-83-stmt-resolve: engine self-report for the refdb statement-resolution
    // API (the §9.3 locator catalog + the captures-by-name join). Resolves
    // SaveGame's locator families against the in-memory statement cache and
    // asserts each against ground truth measured from the curated DB. Boot-only,
    // no hook-fire / "ready" dependency (refdb is open by the first suite tick).
    // GRACEFUL on the pre-deploy state (statement tables absent) — reports a
    // clear DEGRADED PASS, never a hard FAIL or crash. One-shot guarded internally.
    kcdx::stmt_resolve::RunSelfTestOnce();

    // cap-80-early-hook: engine self-report for the author-parameterized
    // early-install primitive (src/early_hook.{h,cpp}). Boot-only, same timing
    // as cap-66 — no hook-fire / "ready" / VM dependency; MinHook is up by the
    // first suite tick. It installs a detour by (module + export + signature +
    // detour) on a known already-mapped module (kcdx's own DLL) targeting a
    // dedicated exported no-op, then calls the export and asserts the detour
    // FIRED and passed through — proving the GENERALIZED install works, not just
    // the baked BugSplat target. One-shot guarded internally.
    kcdx::early_hook_selftest::RunSelfTestOnce();

    // cap-79-lua-shim-forward: engine self-report for the Lua symbol shim's
    // forward layer (src/lua_shim.{h,cpp}, restructure Phase 11 P2 step 1).
    // Unlike cap-52..66, it DOES depend on the live lua_State — it pushes a
    // string through a FORWARDED shim member and reads it back, so it needs the
    // VM captured at the first lua_pcall. The self-test early-returns (no
    // report, retries next tick) while CurrentLuaState() is null, then reports
    // once the VM is up — mirrors cap-47's "retry until the dependency lands."
    // One-shot guarded internally. (The shim coexists with the static-linked
    // Lua this step — vendor/lua/*.c is dropped in P5 — so a live VM exists to
    // call into; PROBE Q stays silent because this step adds no new sentinel.)
    kcdx::lua_shim::RunSelfTestOnce();

    // cap-81-vm-adopt: engine self-report for the KEYSTONE — kcdx builds the one
    // Lua VM on its worker thread (lua_vm_build::BuildAndAdoptVM) and the engine
    // ADOPTS it via the lua_newstate-callee intercept. Like cap-79 it depends on
    // the live state (captured at the first lua_pcall) AND on the intercept
    // having fired (at CScriptSystem::Init) — it early-returns (retries next
    // tick) until both have landed, then asserts ONE state (live==built),
    // the mainthread invariant on the adopted state, and that both kcdx.* and
    // CryEngine's own scripts live on it. One-shot guarded internally. The game
    // BOOTING is itself the falsifiable observable (a bad adoption AVs before any
    // report).
    kcdx::cap81_vm_adopt_selftest::RunSelfTestOnce();

    // comp-18-foreign-classifier: engine self-report for the foreign-hook
    // prologue classifier (foreign_hook_detect, design §6.1; Phase 4 step 7).
    // Feeds SYNTHETIC prologues (a clean prologue, an E9 into a registered
    // kcdx-owned range, a foreign E9 + foreign FF25 into an unregistered range,
    // an unrecognized FF shape) through Classify + the E9/FF25 byte decode and
    // asserts each verdict against ground truth. Needs NO live target — the
    // classifier reads bytes kcdx owns the address of, so a static byte buffer
    // is a faithful prologue. Boot-only, no hook-fire / "ready" / VM dependency.
    // One-shot guarded internally.
    kcdx::foreign_hook_detect_selftest::RunSelfTestOnce();

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
    // FALSIFIABLE (this row can actually go red): reverting RegisterModification(Category::Bytes,...)
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

lua_State* PublishLuaState(lua_State* L) {
    // The single authoritative writer of g_L (the keystone — see
    // HookedLuaPcall_Engine's guarded-confirm note). RELEASE store: this is the
    // cross-thread happens-before edge the game-thread lua_newstate intercept's
    // ACQUIRE load (and HookedUpdate's first-tick ACQUIRE load) pair with. The
    // caller (lua_vm_build, worker thread) has fully built + validated the state
    // BEFORE this store, so the release edge guarantees the game thread observes
    // a complete VM. exchange (not store) so the caller can detect a
    // double-publish (a non-null prior return means publish ran twice — a bug).
    return g_L.exchange(L, std::memory_order_release);
}

bool Install() {
    // Resolve the two engine-bootstrap targets through the Address Library by
    // canonical name: the name yields the VA (base + the curated RVA) directly,
    // with no runtime AOB scan. ResolveAddrByName returns 0 when the entity does
    // not resolve on this build (name unknown/unverified, or WHGame.dll not
    // mapped); the abort below + VerifyExecutable reject a bad/zero target.
    uintptr_t pcallAddr  = refdb::ResolveAddrByName("lua_pcall");
    uintptr_t updateAddr = refdb::ResolveAddrByName("CGame_Update");
    if (!pcallAddr || !updateAddr) {
        log::Error("aborting hook install — lua_pcall/CGame_Update name resolution failed");
        return false;
    }
    if (!VerifyExecutable(reinterpret_cast<void*>(pcallAddr), "lua_pcall") ||
        !VerifyExecutable(reinterpret_cast<void*>(updateAddr), "update")) {
        return false;
    }

    // MinHook may already be initialized: the BugSplat ctor probe (and any
    // future before_game-zone hook) calls MH_Initialize from kcdx.dll
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

    // update STAYS direct MH_CreateHook — the single documented bootstrap
    // exception (per hook-engine.md §"kcdx.hook chaining"). HookedUpdate
    // calls hook_chain::SetLuaState AND drives the chain's per-frame
    // DispatchPre/Post; making it itself a chain entry would self-deadlock
    // (the chain dispatcher would be the function the chain dispatches
    // through). Every OTHER engine-direct hook moved off raw MH onto
    // hook_chain::AddCEngine in the engine-direct migration.
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

    // lua_pcall migration: register the engine's lua_pcall hook through
    // the chain (AddCEngine — the engine-stamp internal entry point). The
    // chain stamps Kind::Engine on the entry, owns the MinHook detour, and
    // runs the original after the chain-Before callback fires. The
    // bootstrap side-effect (g_L.store(L) in HookedLuaPcall_Engine) is
    // preserved — HookedUpdate's first-tick latch still reads g_L to drive
    // hook_chain::SetLuaState.
    {
        auto sigParse = kcdx::hook_signature::Parse(
            "i32 (ptr L, i32 nargs, i32 nresults, i32 errfunc)");
        if (!sigParse.ok) {
            log::ErrorF("engine.lua_pcall: signature parse failed: %s",
                        sigParse.error.c_str());
            return false;
        }
        kcdx::hook_payload::HookPayload p;
        p.mode         = kcdx::hook_payload::Mode::Before;
        p.address      = pcallAddr;
        p.signature    = sigParse.sig;
        p.hasSignature = true;
        p.owningPlugin = "kcdx";
        p.owningAuthor = "kcdx";
        p.name         = "engine.lua_pcall";
        auto add = kcdx::hook_chain::AddCEngine(
            p, reinterpret_cast<void*>(&HookedLuaPcall_Engine),
            sigParse.sig, /*pluginName=*/"kcdx",
            /*priority=*/0, /*name=*/"engine.lua_pcall",
            /*handleId=*/0);
        if (!add.ok) {
            log::ErrorF("engine.lua_pcall: AddCEngine failed: %s",
                        add.reason.c_str());
            return false;
        }
    }

    // Record the engine self-instrumentation hooks in the live modification
    // inventory (category "engine"). These two install on every boot, so the
    // inventory is guaranteed non-empty even before any plugin hook lands.
    kcdx::modification_inventory::RegisterModification(
        pcallAddr, kcdx::modification_inventory::Category::Engine, "lua_pcall");
    kcdx::modification_inventory::RegisterModification(
        updateAddr, kcdx::modification_inventory::Category::Engine, "update");

    log::Info("Hooks installed: lua_pcall (via hook_chain::AddCEngine) + "
              "update (direct MH — the documented bootstrap exception)");

    // BugSplat ctor hook install timing note: worker-thread install was too
    // late — the ctor fired before this code ran. The install lives in
    // src/dllmain.cpp RunBeforeGameZoneInDllMain via the early_hook primitive
    // (early_hook::bugsplat::Arm → LdrRegisterDllNotification), NOT here.

    return true;
}

}  // namespace kcdx::hooks
