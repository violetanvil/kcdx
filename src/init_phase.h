#pragma once

// kcdx initialization phase model — the DECLARED startup-ordering contract.
//
// docs/init.md §"The phase model (the contract)" is authoritative. This header
// declares a single ordered `enum class InitPhase`, a monotonic `g_phase`
// advanced explicitly as the boot progresses, and a `KCDX_REQUIRE_PHASE(p)`
// guard asserting a use site is reached at or after the phase its dependency
// requires.
//
// One-file-one-concern: this is the PHASE-MODEL concern only — the enum, the
// monotonic advance, the require-guard. It performs no boot work itself; the
// AdvanceTo() calls that drive it are pure INSTRUMENTATION bracketing the
// existing boot sequence in dllmain.cpp / hooks.cpp (NO operation is added,
// removed, or reordered by this model — see docs/init.md §Migration plan step 1,
// the pure behavior-preserving refactor).
//
// Version detection (VersionDetected) now advances EARLY — right after
// GameDllMapped, the earliest point WHGame.dll is mapped (ctx B), before
// hooks::Install and the full plugin load. Detection needs only WHGame mapped
// (GetModuleHandleW + kcd_launcher.log / VS_VERSIONINFO read), so it sits as
// early as physically possible. Ctx-A (DllMain) detection is impossible:
// GetModuleHandleW("WHGame.dll") is null under the loader lock, before the
// game's startup maps WHGame. See the AdvanceTo comments in dllmain.cpp.

#include <atomic>

// KCDX_REQUIRE_PHASE expands to LOG_ERROR_KV + ::kcdx::log::KV — include log.h
// here so a TU using the guard need only include init_phase.h (self-contained).
#include "log.h"

namespace kcdx::init {

// The ordered startup phases, in real-time order. Each value's doc-comment
// names its execution CONTEXT (a hard physical constraint, not a choice) per
// docs/init.md §"The three execution contexts":
//   A = DllMain(DLL_PROCESS_ATTACH), under the loader lock, before WHGame.dll
//       is mapped — LOADER-SAFE ONLY.
//   B = WorkerThread, a normal thread spawned by DllMain, at WHGame-MAPPED time
//       (before WHGame's DllMain, far before CSystem::Init) — FULL capability.
//   C = the game's main thread, first `update` tick, after CSystem::Init —
//       FULL capability + the engine is live.
//
// Monotonic: g_phase only advances. The integer value IS the order. This is an
// INTERNAL ordinal — no shipped artifact (cosave field, ABI, plugin surface)
// depends on a phase's numeric value, so reordering a value is safe; the
// version-detection promotion below moved VersionDetected from after
// ConfigLoaded to after GameDllMapped (its real, earliest-possible point —
// ctx B). Otherwise treat as append-only: a new phase appends, and its
// AdvanceTo call slots at the point the sequence reaches it.
enum class InitPhase {
    // [ctx A] paths::Init, log session stamp.
    PreInit = 0,
    // [ctx A] every kcdx.toml parsed; load order RESOLVED.
    ConfigLoaded,
    // [ctx A] before_game load-order slice applied + LDR notifications armed.
    BeforeGameApply,
    // ─── (DllMain returns; WorkerThread already spawned) ───
    // [ctx B] log::Init, exception filter, watchdog.
    WorkerInit,
    // [ctx B] WaitForGameDll returned; WHGame.dll mapped.
    GameDllMapped,
    // [ctx B] g_runtimeGameVersion known — refdb::Open() needs this to look
    // up the running build's row in game_versions before resolving anything.
    //
    // Advances right after GameDllMapped, the earliest point WHGame.dll is
    // mapped — detection reads GetModuleHandleW("WHGame.dll") + kcd_launcher.log
    // / WHGame's VS_VERSIONINFO, which need only WHGame mapped, not the engine
    // initialized. Ctx-A (DllMain) detection is IMPOSSIBLE: WHGame is not mapped
    // under the loader lock, so GetModuleHandleW returns null. This is the
    // earliest physically-achievable point, ahead of hooks::Install and the
    // full plugin load.
    VersionDetected,
    // [ctx B] refdb::Open() returned true — the SQLite-backed reference database
    // is mapped READ-ONLY, schema_version + game_versions row both validated,
    // AND the in-memory cache is BUILT (refdb owns the resolved per-entity
    // address + signature + state, bulk-built once at the running game version).
    // EVERY name / id resolution in this engine — refdb::ResolveByName,
    // refdb::ResolveById, refdb::ResolveAddrByName, refdb::ResolveAddrById,
    // refdb::SignatureByName, address_library::ResolveByName (its engine-seed
    // tier delegates here) — depends on >= RefdbOpened. Post-RefdbOpened, those
    // resolves are in-memory hash lookups; no per-call SQL.
    //
    // Advances right after VersionDetected: refdb needs g_runtimeGameVersionString
    // populated (that happens at VersionDetected) to locate the running build's
    // game_versions row, so this is the earliest physically-achievable point. A
    // refdb::Open() failure aborts the worker thread (the engine cannot resolve
    // named targets without it), so a boot that reaches this phase has a usable
    // refdb for the rest of the session.
    RefdbOpened,
    // [ctx B] hooks::Install (lua_pcall + update); MinHook live.
    EngineHooksInstalled,
    // [ctx B] the kcdx-owned ctor bracket INSTALLED
    // (mod_absorb::InstallCtorBracket) — kcdx FULLY replaces ModManager_ctor,
    // synthesizing the C_ModManager from scratch and writing kcdx's resolved
    // enabled list directly. Install is EARLY (right after EngineHooksInstalled)
    // so it wins the race against CSystem::Init's call to the native ctor on
    // the game's main thread — installing it later (e.g. after DiscoverAndLoad)
    // loses that race. The bracket FIRES later, inside CSystem::Init, AFTER
    // DiscoverAndLoad finishes on this worker thread — and waits on
    // g_kcdxReadyEvent before reading the kcdx-built list, so by fire time the
    // enabled list reflects every loaded plugin. docs/init.md §"The mod-loader
    // absorb".
    CtorBracketInstalled,
    // [ctx B] the Kind::Hook/Kind::Bytes deferred-apply handlers are registered
    // (before plugins), then DiscoverAndLoad runs; Plugin_Preload/Load fired.
    // Advances AFTER CtorBracketInstalled: the bracket install is earlier
    // (race-critical), but plugins LOAD before the bracket FIRES inside
    // CSystem::Init — so by fire time the enabled list reflects every plugin.
    PluginsLoaded,
    // [ctx B] the rebuilt enabled I_Mod* list is BUILT on the worker thread
    // (mod_absorb::BuildEnabledListOnWorker) and the readiness event the
    // ctor-bracket callback waits on is SIGNALED. By this phase, when the
    // ctor-bracket callback fires on the game's main thread inside
    // CSystem::Init, its WaitForSingleObject returns immediately (the list is
    // already built); if the game thread races ahead of the worker, it blocks
    // here briefly until the worker reaches this phase. Decouples the BUILD
    // (worker) from the FIRE (game thread inside CSystem::Init) so the
    // game-thread observable outcome is unchanged but the construction cost
    // is moved off the game thread's hot path.
    EnabledListBuiltAndReady,
    // [ctx B] save_load_hooks, serialization (after save_load). Advances LAST of
    // the ctx-B group.
    EngineSubsystemsInit,
    // ─── (game begins executing; CSystem::Init runs; first update tick) ───
    // [ctx C] after_game load-order slice applied + KCDX Lua table registered.
    AfterGameApply,
};

// The current phase. Monotonic, advanced only via AdvanceTo(). Atomic because
// it is written in context A/B and read across contexts (refdb cache reads
// from B, and from C once the absorb lands).
extern std::atomic<InitPhase> g_phase;

// Advance to `p`. Asserts a monotonic non-decreasing advance (a backward
// advance is a programming error and trips the debug assert); logs each
// advance via LOG_DEBUG_KV category "INIT_PHASE". Re-advancing to the current
// phase is a no-op (idempotent).
void AdvanceTo(InitPhase p);

// The current phase (relaxed atomic load).
InitPhase Current();

// Human-readable phase name (for log lines / guard diagnostics). Stable token.
const char* Name(InitPhase p);

}  // namespace kcdx::init

// -----------------------------------------------------------------------------
// KCDX_REQUIRE_PHASE(p) — observability guard at a use site.
//
// When the current phase < p, logs a LOUD Error (category "INIT_PHASE", naming
// the required vs current phase). A wrong-phase access is a correctness risk
// (e.g. the FOpen-probe-broke-on-version=0 class of bug) → Error severity
// (fail loud). It does NOT abort the process and does NOT change the
// guarded code's behavior — the game keeps running; this is logging only.
// -----------------------------------------------------------------------------
#define KCDX_REQUIRE_PHASE(p)                                                 \
    do {                                                                      \
        ::kcdx::init::InitPhase _kcdx_req = (p);                              \
        ::kcdx::init::InitPhase _kcdx_cur = ::kcdx::init::Current();          \
        if (static_cast<int>(_kcdx_cur) < static_cast<int>(_kcdx_req)) {      \
            LOG_ERROR_KV("INIT_PHASE", "require_phase_violation",             \
                ::kcdx::log::KV::BareStr("required",                         \
                    ::kcdx::init::Name(_kcdx_req)),                          \
                ::kcdx::log::KV::BareStr("current",                          \
                    ::kcdx::init::Name(_kcdx_cur)),                          \
                ::kcdx::log::KV::BareStr("detail",                           \
                    "a use site requiring this phase was reached before the "\
                    "phase advanced — a dependency ran too early; the value "\
                    "it reads may be unset/stale (correctness risk). The "   \
                    "game keeps running; this is a loud diagnostic, not an "  \
                    "abort"));                                                \
        }                                                                     \
    } while (0)
