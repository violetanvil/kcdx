// scripting_interface — implements kcdxScriptingInterface (the
// C++-side plugin API for registering Lua-callable native functions).
//
// Plugins call kcdx->QueryInterface(kcdxInterface_Scripting, 1) and
// receive a kcdxScriptingInterface* with a RegisterFunction pointer.
// The function buffers registrations until the kcdx global table
// exists (first-update-tick) and applies them then. Registrations
// arriving after that point apply immediately.
//
// Storage: each registration becomes a 5-tuple (owner, table_name,
// fn_name, kcdxLuaCFunction, user_data). The Lua-side dispatcher is
// a tiny shim C function that calls back into the plugin's
// kcdxLuaCFunction with the captured user_data.

#pragma once

#include "kcdx/Interfaces.h"

namespace kcdx::scripting_interface {

// Returns the implementation. Called from interfaces.cpp's
// QueryInterface dispatch when the plugin asks for
// kcdxInterface_Scripting.
const kcdxScriptingInterface* GetInterface();

// Drain any queued registrations into the live Lua state. Called from
// lua_bind.cpp::RegisterKcdxTable AFTER the kcdx global is created
// but BEFORE lua_setglobal. The kcdx table must be on top of the
// Lua stack.
void ApplyPendingToTable(lua_State* L);

}  // namespace kcdx::scripting_interface
