#pragma once

namespace kcdx::cvar {

// Engine-side init. Resolves gEnv->pConsole (id 10) + IConsole::GetCVar
// (id 16) by canonical name via the Address Library, and reads the
// ICVar::GetIVal (id 156) / ICVar::GetFVal (id 157) vtable SLOT INDICES
// from the DB. Idempotent. Call once after the Address Library is
// populated, alongside console::Init() (shares the gEnv->pConsole
// availability precondition).
//
// Returns true if the console + GetCVar resolved and the two ICVar slots
// were read. False logs a WARN — every cvar::Get* call returns false
// (the surface stays disabled); all other kcdx functionality is
// unaffected.
bool Init();

// Read a game CVar's 32-bit int value by name.
//
// Resolves gEnv->pConsole, calls IConsole::GetCVar(name) to get an
// ICVar*; if non-null, dispatches the ICVar's GetIVal through the runtime
// object's OWN vtable (slot index from the DB), writes *out, returns true.
//
// Returns false WITHOUT writing *out when: the surface is unready/
// unresolved, name is null/empty, GetCVar returns null (no such CVar), or
// the ICVar pointer is null. A failed read NEVER writes garbage to *out.
bool GetInt(const char* name, int* out);

// Read a game CVar's float value by name. Same contract as GetInt, via
// the ICVar's GetFVal vtable slot. Returns false without writing *out on
// any miss.
bool GetFloat(const char* name, float* out);

}  // namespace kcdx::cvar
