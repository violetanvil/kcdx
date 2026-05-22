#pragma once
#include "kcdx/Interfaces.h"

namespace kcdx::trampoline {

// Get the published kcdxTrampolineInterface implementation. Used by
// interfaces.cpp's QueryInterface dispatch.
const kcdxTrampolineInterface* GetInterface();

// Allocate from the branch pool. Engine-internal entry point used by the
// hook engine (for raw-bytes detour bodies in [[hook]] entries) in addition
// to plugin-facing AllocateFromBranchPool.
//
// `nearVa` controls where the reservation is anchored so a 5-byte rel32 jmp
// (E9/E8) from a hook site can reach the returned buffer:
//   - nearVa == 0 (default): anchor near WHGame.dll's .text, as before. Every
//     legacy/generic caller keeps this behavior unchanged.
//   - nearVa != 0: anchor the reservation within ±2 GB of that VA. The
//     forward kcdx.hook path passes the target's VA so a hook whose target
//     lives in another loaded module (>2 GB from WHGame) still gets a
//     rel32-reachable trampoline. Reservations are reused only when their
//     WHOLE range is in rel32 range of `nearVa` (see Allocate()).
void* AllocateBranch(kcdxPluginHandle owner, size_t size, uintptr_t nearVa = 0);

// Same for the local pool.
void* AllocateLocal(kcdxPluginHandle owner, size_t size);

}  // namespace kcdx::trampoline
