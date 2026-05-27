#include <windows.h>

#include <cstdio>
#include <string>

#include "config.h"
#include "crash_guard.h"
#include "hooks.h"
#include "init_phase.h"
#include "ldr_notify.h"
#include "load_order.h"
#include "log.h"
#include "lua_bind_bytes.h"   // RegisterHandlers() — Kind::Bytes apply handler
#include "lua_bind_hook.h"    // RegisterHandlers() — Kind::Hook apply handler
#include "mod_absorb/pak_mod_registry.h"  // pak-mod version gate (step 3)
#include "paths.h"
#include "plugin_loader.h"
#include "save_load_hooks.h"
#include "serialization.h"
#include "watchdog_spawn.h"

#include "probes/bugsplat_ctor_probe.h"  // KEEP — proven before_game-hook install
                                         // machinery; Phase 11 generalizes it
                                         // (docs/outstanding-work/before-game-hooks.md §5)
#include "probes/loc_dump_probe.h"       // loc runtime-dump feature, step 1: minimal
                                         // dev-mode probe (ctor capture + by-ID getter)
#include "probes/fopen_override_probe.h" // Phase 8.5 pak-resolver probe (FOpen
                                         // read-fires + override semantics)
#include "probes/mod_loader_probe.h"     // Phase 8.5 absorb PROBE U.6 (SELECT-
                                         // detour timing + I_Mod record layout)

DWORD WINAPI WorkerThread(LPVOID) {
    // paths::Init is also called from DllMain (idempotent). Calling it
    // again here is safe and keeps this worker startup self-contained.
    kcdx::paths::Init();

    kcdx::log::Init();
    kcdx::log::Info("");
    kcdx::log::Info("kcdx.dll loaded");

    // Install the unhandled-exception backstop early so even a crash
    // during config load / hook install gets a final log line. The
    // filter chains to the prior handler (BugSplat once KCD2 has
    // installed it).
    kcdx::guard::InstallUnhandledExceptionFilter();

    char pluginsUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, kcdx::paths::PluginsDir().c_str(), -1,
                        pluginsUtf8, sizeof(pluginsUtf8), nullptr, nullptr);
    kcdx::log::InfoF("plugins directory: %s", pluginsUtf8);
    char engineUtf8[512];
    WideCharToMultiByte(CP_UTF8, 0, kcdx::paths::EngineDataDir().c_str(), -1,
                        engineUtf8, sizeof(engineUtf8), nullptr, nullptr);
    kcdx::log::InfoF("engine data directory: %s", engineUtf8);

    // LoadAllConfigs has already run inside DllMain (synchronously, so
    // before_game-zoned [[patch]] entries could apply against ntdll /
    // kernel32 / etc. that were already mapped, and the LDR-notification
    // callback could be registered for WHGame.dll). The idempotence
    // guard inside LoadAllConfigs makes this call a no-op that simply
    // logs "skipping (already loaded earlier this session)".
    //
    // We keep the call here so the test-suite reporter, dev_mode flag
    // propagation, etc. all see a uniform code path regardless of
    // whether DllMain's pre-load succeeded or aborted partway. If
    // DllMain ever fails to parse configs (loader-lock edge case
    // someone hits), the worker thread's call is the fallback.
    kcdx::config::LoadAllConfigs(kcdx::paths::PluginsDir());

    // Spawn the external crash-bundle watchdog. It blocks on our
    // process handle and zips up logs + crash artifacts when we die
    // with a non-zero exit code. This catches crash classes that the
    // in-process exception filter can't (fast-fail, stack overflow
    // with no stack left, kernel-level termination). On launch
    // failure we just log a WARN — game still runs, just no
    // auto-bundle. Spawned AFTER LoadAllConfigs so the dev-mode flag
    // we pass it is the final post-config value.
    kcdx::watchdog::Spawn();

    // PHASE 3 (ctx B): WorkerInit — log::Init, the exception filter, and the
    // watchdog are all up. Reached after watchdog::Spawn (the last of the three).
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::WorkerInit);

    // Wait for KingdomCome.exe's startup code to load WHGame.dll
    // before installing our hooks. The launcher (kcdx.exe) injects
    // kcdx.dll into a CREATE_SUSPENDED game process, so kcdx's
    // DllMain + this worker thread run BEFORE KingdomCome.exe begins
    // executing — WHGame.dll isn't mapped yet. The ldr_notify
    // callback registered in DllMain signals an event the moment
    // WHGame.dll lands; we block here until it does. After
    // ResumeThread() (launcher) → KCD2 startup → LoadLibrary
    // WHGame.dll → notification → SetEvent, this wait returns and
    // hooks::Install proceeds normally.
    //
    // 60s timeout is generous (typical wait is well under a second).
    // On timeout we bail loudly rather than crashing inside
    // hooks::Install's "WHGame.dll not loaded" error path.
    if (!kcdx::ldr_notify::WaitForGameDll(/*timeoutMs=*/60'000)) {
        kcdx::log::Error("worker thread: WHGame.dll did not load within "
                         "60s — aborting hook install. Game will run "
                         "vanilla.");
        return 1;
    }

    // PHASE 4 (ctx B): GameDllMapped — WaitForGameDll returned successfully,
    // so WHGame.dll is mapped (the gate SetEvent fired). Reached only on the
    // success path (the timeout path above returns before getting here).
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::GameDllMapped);

    // PHASE 5 (ctx B): VersionDetected — detect the running KCD2 build NOW,
    // the earliest WHGame-mapped point. DetectRuntimeGameVersion reads
    // GetModuleHandleW("WHGame.dll") + kcd_launcher.log / VS_VERSIONINFO, which
    // need only WHGame mapped (just confirmed by WaitForGameDll above), NOT the
    // engine initialized. Doing it HERE — before hooks::Install and the full
    // plugin load — means g_runtimeGameVersion is known before every
    // version-gated read (address_library::Resolve) and before the per-plugin
    // compat gate in DiscoverAndLoad, which now READS this value rather than
    // detecting it. Ctx-A (DllMain) detection is impossible: WHGame is not
    // mapped under the loader lock (GetModuleHandleW returns null there), so
    // this is the earliest achievable point.
    kcdx::plugins::g_runtimeGameVersion =
        kcdx::plugins::DetectRuntimeGameVersion();
    // The version STRING (wh_sys_version from <game-root>/system.cfg) — the
    // source the unified <supports> string-prefix-wildcard gate compares
    // against. Same init point as the integer detect; reads a file (no WHGame
    // dependency), graceful-degrades to "" + WARN if system.cfg is absent.
    kcdx::plugins::g_runtimeGameVersionString =
        kcdx::plugins::DetectRuntimeGameVersionString();
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::VersionDetected);

    // Pak-mod version gate (mod-loader absorb, step 3). Discovery already ran
    // (config::LoadAllConfigs, in DllMain) and folded every pak mod into the
    // load_order model as a "mods.<modid>" row. The <supports> compatibility
    // decision must run HERE — the first point g_runtimeGameVersionString is
    // known (it is empty during the early dir scan, so gating there would be a
    // silent no-op). ApplyVersionGate flips engineAccepted=false on any pak mod
    // whose mod.manifest <supports> declares no matching version (the SAME
    // mechanism the plugin path + zone_gate use). It runs BEFORE DiscoverAndLoad
    // and the eventual enabled-list build consume the resolved order.
    {
        size_t disabled = kcdx::mod_absorb::ApplyVersionGate(
            kcdx::plugins::g_runtimeGameVersionString);
        kcdx::log::InfoF("pak-mod version gate: %zu mod(s) disabled as "
                         "incompatible with game '%s'",
                         disabled,
                         kcdx::plugins::g_runtimeGameVersionString.c_str());
    }

    if (!kcdx::hooks::Install()) {
        kcdx::log::Error("hooks::Install failed — no patches will be applied");
        return 1;
    }

    // PHASE 6 (ctx B): EngineHooksInstalled — hooks::Install succeeded
    // (lua_pcall + update hooked; MinHook live). Reached only on the success
    // path (the failure return above precedes this).
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::EngineHooksInstalled);

    // PHASE 7 (ctx B): ModLoaderTakeoverArmed — the mod-loader SELECT detour.
    // PROBE U.6 (dev-mode-gated, observe-only): a log-only detour on the engine's
    // mod-loader SELECT orchestrator (wh::C_ModManager FUN_180da104c), installed
    // here — after EngineHooksInstalled (WHGame.dll mapped + MinHook live), before
    // EngineSubsystemsInit (where CSystem::Init runs the native mod-load). Resolves
    // the two probe-first gates for the absorb (docs/init.md §"The mod-loader
    // absorb"): U.6.1 — does a worker-thread detour fire BEFORE the native mod-load
    // (decides whether the narrow takeover installs at ctx-B here, or needs ctx-A /
    // before_game timing); U.6.2 — the I_Mod 0x70-byte record layout kcdx must
    // synthesize for kcdx-plugins/ entries. ALWAYS calls the original SELECT
    // unchanged (no list mutation). No-op in production (dev-mode gate).
    kcdx::probes::mod_loader_probe::Install();
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::ModLoaderTakeoverArmed);

    // Localization runtime-dump feature: arm the dev-mode probe
    // (CLocalizedStringsManager ctor capture + LocalizeString overload hooks on
    // vtable slots 21/22 for key capture). Runs here, after hooks::Install
    // (WHGame.dll mapped + MinHook initialized), and BEFORE CryEngine's system
    // init constructs the loc manager — so the ctor detour is live when the
    // ctor runs (it installs the LocalizeString hooks off the captured vtable).
    // Dev-mode-gated + idempotent internally; a no-op in production. (The
    // prior slot-1 by-int-ID getter target was retargeted away after it was
    // proven to be GetLanguageName, the wrong function.)
    // DISABLED 2026-05-26 — loc RE/probe phase COMPLETE (find{text=} design
    // settled; text→gameplay-function proven impossible via the loc path, see
    // parallel-ghidra-research.md §6 + LOC-MANAGER-FINDINGS.md). The probe hooks
    // LocalizeString, which fires ~11.6k×/session on the UI-text hot path +
    // RtlCaptureStackBackTrace per @-key — a real per-frame cost with no
    // remaining diagnostic purpose. Disarmed. The probe code + cap-43 stay as
    // the loc-probe regression/evidence base; re-enable only for a fresh loc
    // launch. Disabling this line installs NO loc hooks → zero runtime cost.
    // kcdx::probes::loc_dump_probe::Install();

    // Phase 6 save/load lifecycle hooks. ABIs from ROUND 3 RECON via
    // _research/phase6-save-load/phase6_abi_walker.py — full-body capstone analysis,
    // not prologue-shape guessing. SaveGame correctly forwards all 7
    // args. See _research/phase6-save-load/SAVE-LOAD-CANDIDATES.md
    // §"ROUND 3 ABI RECON" for the derivation.
    kcdx::save_load_hooks::Install();

    // Phase 6b kcdxSerializationInterface — subscribes to save/load
    // lifecycle messages for the cosave (.kcdx) read/write pipeline.
    // Must initialize AFTER save_load_hooks::Install so the messages
    // exist; the order also matches the engine-internal listener
    // path (messaging::FireEngineMessage calls serialization
    // synchronously after firing to plugin listeners).
    kcdx::serialization::Init();

    // Register the deferred-apply handlers for the registry Kinds the C++
    // plugin interfaces queue (Kind::Hook via kcdxHookInterface, Kind::Bytes
    // via the future kcdxBytesInterface). These MUST run before
    // DiscoverAndLoad: a C++ DLL's kcdxPlugin_Load (driven below) can install
    // a hook through kcdxHookInterface, which queues a Kind::Hook entry into
    // lua_registry — and lua_registry::Append rejects any Kind with no
    // registered handler. The Lua-side bind() (RegisterKcdxTable, first-update-
    // tick) is too LATE for the C++ Load-time caller; these handlers are engine
    // state, not Lua-surface state, so they register at engine init.
    kcdx::lua_bind_hook::RegisterHandlers();
    kcdx::lua_bind_bytes::RegisterHandlers();

    // PHASE 8 (ctx B): EngineSubsystemsInit — save_load_hooks + serialization
    // (after save_load) + the Kind::Hook/Kind::Bytes deferred-apply handlers
    // (before plugins) are all registered. Reached after the two
    // RegisterHandlers calls (the last of this group) and before DiscoverAndLoad.
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::EngineSubsystemsInit);

    // Plugin DLL discovery + load. Runs after the engine's own hooks are
    // installed so plugins can rely on the MinHook + lua_State infrastructure
    // being present. Plugin_Preload + Plugin_Load fire here, before the first
    // game `update` tick.
    kcdx::plugins::DiscoverAndLoad(kcdx::paths::PluginsDir());

    // PHASE 9 (ctx B): PluginsLoaded — DiscoverAndLoad finished;
    // Plugin_Preload/Plugin_Load have fired for every plugin. (VersionDetected
    // is NO LONGER advanced here: version detection moved EARLY, to right after
    // GameDllMapped above — before hooks::Install. DiscoverAndLoad now relies on
    // g_runtimeGameVersion already being set, rather than detecting it.)
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::PluginsLoaded);

    // Phase 8.5 pak-resolver probe (PROBE U.1, observe-only): install the
    // CCryPak::FOpen body detour (Address Library id 1206) to (a) confirm the
    // resolver fires for asset READS at runtime and (b) log the early pak-
    // resident virtual paths, so PROBE U.2's override-target is confirmed-
    // firing, not guessed. Dev-mode-gated + idempotent; a no-op in production.
    //
    // address_library::Resolve(1206) gates on a g_runtimeGameVersion match, so
    // this probe must run AFTER VersionDetected. That is now guaranteed early —
    // the version is detected right after GameDllMapped (above), well before
    // this point — so the probe's placement here is comfortably past
    // VersionDetected. (Historically this had to sit after DiscoverAndLoad,
    // which was where the version got set; that dependency is gone now that
    // detection moved early, but the probe stays here.) The FOpen detour still
    // arms well before any menu/save asset reads; the resolver fires
    // continuously through boot→menu.
    //
    // (Was briefly disabled 2026-05-26 to isolate a parallel save-load crash
    // investigation; that crash is fixed and the baseline is clean, so it is
    // re-enabled now. U.1 already succeeded once: cap-44-fopen-read-fires PASS;
    // reach_check match=1; 64 read opens captured.)
    kcdx::probes::fopen_override_probe::Install();

    return 0;
}

// Synchronous DllMain-phase work for the before_game-zone path.
//
// Reads + parses every plugin's kcdx.toml, computes load-order
// resolution, applies before_game-zoned [[patch]] entries to any
// already-loaded target module, and registers an LdrRegisterDllNotification
// callback so future module loads (notably WHGame.dll) get their
// before_game patches applied right after they're mapped, BEFORE
// their own DllMain runs.
//
// Loader-safety contract: this runs under ntdll's loader lock during
// kcdx.dll's own DllMain. dumpbin /imports kcdx.dll confirms zero
// delay-loaded DLLs — std::filesystem, tomlplusplus, std::string,
// std::vector are all safe. MinHook init / CreateThread / LoadLibrary
// are NOT done here; those stay in the worker thread.
//
// See docs/load-order.md §"Loader-safety contract for before_game zone".
static void RunBeforeGameZoneInDllMain() {
    // PHASE 0 (ctx A): PreInit — paths::Init + the log session stamp below.
    // This is the first explicit advance; g_phase already starts at PreInit so
    // this marks the boot's start in the INIT_PHASE log without changing it.
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::PreInit);

    kcdx::paths::Init();

    // Establish the per-session log stamp BEFORE config parse can enable
    // dev mode (LoadAllConfigs → dev::SetEnabled(true) → SetDevMode opens
    // the dev log, which derives its filename from the stamp). This runs
    // under the loader lock; EnsureSessionStamp is loader-lock-safe (a
    // set-once over a self-contained localtime formatter). Without it the
    // dev log opened as "kcdx-dev_.log" (empty stamp). The worker thread's
    // later log::Init() keeps this same stamp, so the engine log and the
    // dev log share one stamp.
    kcdx::log::EnsureSessionStamp();

    // Synchronous config parse. Sets the idempotence flag so the
    // worker thread's later LoadAllConfigs call is a no-op.
    kcdx::config::LoadAllConfigs(kcdx::paths::PluginsDir());

    // load_order::Read + Resolve are called inside LoadAllConfigs, so
    // by this point every plugin has an Effective(zone, priority,
    // enabled) row computed.
    //
    // PHASE 1 (ctx A): ConfigLoaded — every kcdx.toml parsed; load order
    // RESOLVED. Reached as soon as LoadAllConfigs returns.
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::ConfigLoaded);

    // NOTE: VersionDetected is NOT advanced here. It CANNOT be — detection
    // calls GetModuleHandleW("WHGame.dll"), which is null under the loader lock
    // in DllMain (the launcher injected us into a CREATE_SUSPENDED process
    // before the game's startup mapped WHGame). The earliest physically-
    // achievable detection point is ctx B, right after WaitForGameDll returns
    // (WHGame mapped); that is where VersionDetected now advances (see
    // WorkerThread above). The enum reflects this: VersionDetected sits after
    // GameDllMapped, not in this ctx-A function.

    // Apply before_game [[patch]] entries against modules already mapped
    // (ntdll, kernel32, and kcdx.dll itself — the launcher injected us via
    // CreateRemoteThread(LoadLibraryW) before the game's own startup ran).
    kcdx::ldr_notify::ApplyAlreadyLoaded();

    // Register the notification callback for future module loads.
    // WHGame.dll mapping is what triggers any before_game patch
    // targeting it.
    kcdx::ldr_notify::Register();

    // KEEP (PROBE S/T, answered 2026-05-26): install the BugSplat ctor
    // log-only hook at DllMain time (immediate if BugSplat64.dll is
    // already mapped, otherwise via LDR notification). PROBE T CONFIRMED
    // DllMain-time install timing catches the ctor call that worker-thread
    // install (PROBE S) missed. This is the proven before_game-hook install
    // machinery — Phase 11 generalizes it into the real builtin; it is NOT
    // removed here (docs/outstanding-work/before-game-hooks.md §5).
    kcdx::probes::bugsplat_ctor_probe::ArmLdrInstall();

    // PHASE 2 (ctx A): BeforeGameApply — the before_game load-order slice is
    // applied (ApplyAlreadyLoaded) and the LDR notifications are armed
    // (Register + ArmLdrInstall). DllMain returns right after this; the
    // WorkerThread (ctx B) was already spawned by the caller below.
    // (VersionDetected sits AFTER this in the enum but advances later, in
    // ctx B — it cannot run in this ctx-A function; see the note above.)
    kcdx::init::AdvanceTo(kcdx::init::InitPhase::BeforeGameApply);
}

BOOL APIENTRY DllMain(HMODULE hModule, DWORD reason, LPVOID /*reserved*/) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(hModule);

        // before_game-zone path runs unconditionally. Zone is the
        // single source of truth for apply timing: zone=before_game
        // means before WHGame.dll's DllMain, full stop. No env-var
        // gating, no flag — the load order says when the patch
        // applies, and the engine honors it.
        RunBeforeGameZoneInDllMain();

        HANDLE h = CreateThread(nullptr, 0, WorkerThread, nullptr, 0, nullptr);
        if (h) {
            CloseHandle(h);
        } else {
            // Fail-state (Batch F #20): the worker thread is where log::Init,
            // hook install, plugin discovery, save/load hooks, and the watchdog
            // all run. If CreateThread fails, the ENTIRE engine is inert this
            // session — no hooks, no plugins, no save/load, no watchdog — and
            // log::Init never runs (it lives inside the thread that just failed
            // to start), so the file log is never up. OutputDebugStringA is the
            // ONLY sink; the [ERROR] tag substitutes for ODS's missing severity.
            // We KEEP returning TRUE: the host game process must continue (the
            // signal is this line, not aborting the host).
            DWORD err = GetLastError();
            char ods[256];
            snprintf(ods, sizeof(ods),
                     "[kcdx][ERROR] dllmain: CreateThread(WorkerThread) failed "
                     "(err=%lu); kcdx is INERT this session — no hooks, "
                     "plugins, save/load, or watchdog will load, and there is "
                     "no kcdx log file (log::Init runs inside the worker "
                     "thread that failed to start). The game runs vanilla.\n",
                     err);
            OutputDebugStringA(ods);
        }
    }
    return TRUE;
}
