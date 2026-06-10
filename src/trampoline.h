#pragma once
#include "kcdx/Interfaces.h"

namespace kcdx::trampoline {

// Get the published kcdxTrampolineInterface implementation. Used by
// interfaces.cpp's QueryInterface dispatch.
const kcdxTrampolineInterface* GetInterface();

// Allocate from the branch pool. Engine-internal entry point used by the
// hook engine (for raw-bytes detour bodies in kcdx.hook entries) in addition
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

// --- Test-only introspection (engine self-test, NOT a plugin surface) ---
// These exist solely so the engine-internal trampoline self-test can assert the
// branch pool's expansion + exhaustion behavior. They are READ-ONLY / pure: they
// do not alter the production allocator's behavior on any path. They add nothing
// to include/kcdx/Interfaces.h and no plugin links them.

// Count the branch-pool reservations currently live (the `branch == true`
// reservations in g_reservations), under the pool mutex. The self-test records
// this before + after driving real AllocateBranch calls and asserts it grew —
// proving the proactive 80%-expansion actually staged a new reservation. A pure
// read of internal state; it changes nothing.
size_t BranchReservationCountForTest();

// Format the teaching exhaustion error with synthetic inputs and copy it into
// `outErr`. The self-test calls this to assert the author-facing exhaustion
// message contains the required tokens (pool name, percentage, region count,
// the next-step guidance) WITHOUT physically exhausting the live pool. Returns
// true (a non-empty error was produced) when `outErr`/`outErrLen` are usable.
// This is the SAME formatter the production exhaustion path uses, so the test
// pins the exact text authors will read. No production-allocator branch is taken.
bool FormatExhaustionErrorForTest(size_t syntheticRegionsTried, double fullestPct,
                                  char* outErr, size_t outErrLen);

}  // namespace kcdx::trampoline
