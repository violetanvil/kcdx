#include "init_phase.h"

#include <cassert>

#include "log.h"

namespace kcdx::init {

// Starts at PreInit. The first AdvanceTo(PreInit) is a no-op (idempotent
// re-advance to the current phase), so the boot's first explicit advance is
// the real PreInit marker line; subsequent advances move forward.
std::atomic<InitPhase> g_phase{InitPhase::PreInit};

const char* Name(InitPhase p) {
    switch (p) {
        case InitPhase::PreInit:                 return "PreInit";
        case InitPhase::ConfigLoaded:            return "ConfigLoaded";
        case InitPhase::VersionDetected:         return "VersionDetected";
        case InitPhase::BeforeGameApply:         return "BeforeGameApply";
        case InitPhase::WorkerInit:              return "WorkerInit";
        case InitPhase::GameDllMapped:           return "GameDllMapped";
        case InitPhase::RefdbOpened:             return "RefdbOpened";
        case InitPhase::EngineHooksInstalled:    return "EngineHooksInstalled";
        case InitPhase::CtorBracketInstalled:    return "CtorBracketInstalled";
        case InitPhase::PluginsLoaded:           return "PluginsLoaded";
        case InitPhase::EnabledListBuiltAndReady: return "EnabledListBuiltAndReady";
        case InitPhase::EngineSubsystemsInit:    return "EngineSubsystemsInit";
        case InitPhase::AfterGameApply:          return "AfterGameApply";
    }
    return "(unknown)";
}

void AdvanceTo(InitPhase p) {
    InitPhase prev = g_phase.load(std::memory_order_relaxed);

    if (static_cast<int>(p) < static_cast<int>(prev)) {
        // A backward advance is a programming error (the AdvanceTo calls are
        // hand-placed instrumentation; a backward move means one was mis-placed).
        // Log it loudly as a correctness risk — but DO NOT roll g_phase back and
        // DO NOT abort: leaving g_phase at the highest reached phase keeps the
        // monotonic invariant true for downstream require-guards, and the game
        // must keep running (log loud, don't abort).
        LOG_ERROR_KV("INIT_PHASE", "advance_non_monotonic",
            ::kcdx::log::KV::BareStr("attempted", Name(p)),
            ::kcdx::log::KV::BareStr("current", Name(prev)),
            ::kcdx::log::KV::BareStr("detail",
                "AdvanceTo() was called with a phase EARLIER than the current "
                "phase — a mis-placed instrumentation call. g_phase is left at "
                "the higher phase (monotonic invariant preserved); the advance "
                "is ignored"));
        assert(false && "kcdx::init::AdvanceTo non-monotonic");
        return;
    }

    if (p == prev) {
        // Idempotent re-advance to the current phase. Not an error; just a
        // no-op (e.g. the PreInit marker, or a defensive double-call).
        return;
    }

    g_phase.store(p, std::memory_order_relaxed);
    LOG_DEBUG_KV("INIT_PHASE", "advance",
        ::kcdx::log::KV::BareStr("from", Name(prev)),
        ::kcdx::log::KV::BareStr("to", Name(p)));
}

InitPhase Current() {
    return g_phase.load(std::memory_order_relaxed);
}

}  // namespace kcdx::init
