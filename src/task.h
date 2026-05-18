#pragma once
#include "kcdx/Interfaces.h"

namespace kcdx::task {

// Get the published kcdxTaskInterface implementation. Used by
// interfaces.cpp's QueryInterface dispatch.
const kcdxTaskInterface* GetInterface();

// Drain the task queue: run every queued task on the calling thread.
// Called from HookedUpdate (in hooks.cpp) on the game's main thread,
// once per update tick. Plugin tasks see a single-threaded execution
// environment matching the main thread's contract.
void DrainQueue();

}  // namespace kcdx::task
