#pragma once

#include "kcdx/Interfaces.h"

namespace kcdx::console {

// Get the published kcdxConsoleInterface. Used by interfaces.cpp's
// QueryInterface dispatch.
const kcdxConsoleInterface* GetInterface();

// Engine-side init. Resolves gEnv->pConsole via the Address Library
// (ids 1009 + 2000 + 2001). Idempotent. Call once after the
// Address Library is populated (i.e. after WHGame.dll is loaded;
// safe to call from dllmain's WorkerThread).
//
// Returns true if IConsole was resolved successfully. False on
// failure logs a warn — the kcdx.command surface stays disabled
// (RegisterCommand returns false for every call). All other kcdx
// functionality is unaffected.
bool Init();

}  // namespace kcdx::console
