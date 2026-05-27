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
// THIS IS THE PURE-REFACTOR STEP. The enum NAMES every phase in real-time
// order, but each AdvanceTo() call sits at the point the EXISTING sequence
// already reaches that phase today. Notably VersionDetected advances LATE — at
// its current (PluginsLoaded-time) site inside DiscoverAndLoad, NOT promoted
// early. Promoting version detection to context A is a deliberate LATER
// migration step; bundling it here would change behavior and break the
// "pure refactor" guarantee. See the AdvanceTo comments in plugin_loader.cpp /
// dllmain.cpp.

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
// Monotonic: g_phase only advances. Append-only ordering — the integer value
// IS the order, so do not reorder or insert; a new phase appends (and its
// AdvanceTo call slots at the point the sequence reaches it).
enum class InitPhase {
    // [ctx A] paths::Init, log session stamp.
    PreInit = 0,
    // [ctx A] every kcdx.toml parsed; load order RESOLVED.
    ConfigLoaded,
    // [ctx A] g_runtimeGameVersion known — everything version-gated
    // (address_library::Resolve) depends on >= this phase.
    //
    // NOTE (pure-refactor step): this phase currently advances LATE, at its
    // existing site inside DiscoverAndLoad (PluginsLoaded time), NOT in ctx A.
    // The early-promotion to context A is the NEXT migration step; it is
    // deliberately out of scope here so this step changes no behavior.
    VersionDetected,
    // [ctx A] before_game load-order slice applied + LDR notifications armed.
    BeforeGameApply,
    // ─── (DllMain returns; WorkerThread already spawned) ───
    // [ctx B] log::Init, exception filter, watchdog.
    WorkerInit,
    // [ctx B] WaitForGameDll returned; WHGame.dll mapped.
    GameDllMapped,
    // [ctx B] hooks::Install (lua_pcall + update); MinHook live.
    EngineHooksInstalled,
    // [ctx B] the mod-loader SELECT detour installed — placement U.6-gated
    // (docs/init.md §"The mod-loader absorb"). Today: mod_loader_probe::Install.
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
// (e.g. the FOpen-probe-broke-on-version=0 class of bug) → Error severity per
// fail-state-logging.md. It does NOT abort the process and does NOT change the
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
