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

// Print one plain line to the in-game `~` console overlay via CryEngine's
// IConsole::PrintLine. The shared engine entry both the C++ interface thunk
// (kcdxConsoleInterface::Print) and the Lua surface (kcdx.console.print) route
// through.
//
// Returns true on success. Returns false (with a WARN — never a silent no-op)
// if the console surface isn't ready yet, or if IConsole::PrintLine did not
// resolve (a reference DB that predates the PrintLine entity); returns false
// for a null/empty string (a no-op that reports it did nothing). The author's
// string is passed through verbatim — PrintLine owns the line.
bool PrintLine(const char* text);

}  // namespace kcdx::console
