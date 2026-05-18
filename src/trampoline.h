#pragma once
#include "kcdx/Interfaces.h"

namespace kcdx::trampoline {

// Get the published kcdxTrampolineInterface implementation. Used by
// interfaces.cpp's QueryInterface dispatch.
const kcdxTrampolineInterface* GetInterface();

// Allocate from the branch pool. Engine-internal entry point used by the
// hook engine (for raw-bytes detour bodies in [[hook]] entries) in addition
// to plugin-facing AllocateFromBranchPool.
void* AllocateBranch(kcdxPluginHandle owner, size_t size);

// Same for the local pool.
void* AllocateLocal(kcdxPluginHandle owner, size_t size);

}  // namespace kcdx::trampoline
