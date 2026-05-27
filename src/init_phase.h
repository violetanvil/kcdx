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
    // [ctx B] g_runtimeGameVersion known — everything version-gated
    // (address_library::Resolve) depends on >= this phase.
    //
    // Advances right after GameDllMapped, the earliest point WHGame.dll is
    // mapped — detection reads GetModuleHandleW("WHGame.dll") + kcd_launcher.log
    // / WHGame's VS_VERSIONINFO, which need only WHGame mapped, not the engine
    // initialized. Ctx-A (DllMain) detection is IMPOSSIBLE: WHGame is not mapped
    // under the loader lock, so GetModuleHandleW returns null. This is the
    // earliest physically-achievable point, ahead of hooks::Install and the
    // full plugin load.
    VersionDetected,
    // [ctx B] hooks::Install (lua_pcall + update); MinHook live.
    EngineHooksInstalled,
    // [ctx B] the production mod-loader SELECT detour installed
    // (mod_absorb::InstallSelectDetour) — the takeover that rebuilds the enabled
    // list. Worker-thread placement is confirmed in time (the detour fires
    // before CSystem::Init completes mod selection). docs/init.md §"The
    // mod-loader absorb".
    ModLoaderTakeoverArmed,
    // [ctx B] save_load_hooks, serialization (after save_load), Kind handlers
    // (before plugins).
    EngineSubsystemsInit,
    // [ctx B] DiscoverAndLoad; Plugin_Preload/Load fired.
    PluginsLoaded,
    // ─── (game begins executing; CSystem::Init runs; first update tick) ───
    // [ctx C] after_game load-order slice applied + KCDX Lua table registered.
    AfterGameApply,
};

// The current phase. Monotonic, advanced only via AdvanceTo(). Atomic because
// it is written in context A/B and read across contexts (the version-gated
// reads in address_library run from B, and from C once the absorb lands).
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
