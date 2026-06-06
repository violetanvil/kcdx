#pragma once

extern "C" {
struct lua_State;
}

namespace kcdx::hooks {

// AOB-scan WHGame.dll for lua_pcall + an update tick function, install
// MinHook hooks. Safe to call once at startup. Returns true on success.
bool Install();

// Returns the latest captured lua_State* (populated each time the game calls
// lua_pcall). May be nullptr early in startup before the first pcall fires.
lua_State* CurrentLuaState();

// Publish the AUTHORITATIVE lua_State kcdx built on its worker thread (the
// keystone — lua_vm_build::BuildAndAdoptVM). This is the SINGLE authoritative
// writer of g_L: a RELEASE store that is the cross-thread happens-before edge
// the game-thread intercept's ACQUIRE load (and HookedUpdate's first-tick
// ACQUIRE load) pair with. Call ONCE, on the worker, AFTER the kcdx.* state is
// fully built and validated, BEFORE installing the lua_newstate intercept.
//
// After this publish, the engine.lua_pcall hook's store becomes a guarded
// CONFIRMATION (HookedLuaPcall_Engine): it asserts the engine's incoming L
// EQUALS this published L and fails LOUD on a mismatch (a different L = a silent
// second VM = a hard failure). Returns the prior g_L value (nullptr on the
// expected first publish) so the caller can detect a double-publish.
lua_State* PublishLuaState(lua_State* L);

}  // namespace kcdx::hooks
