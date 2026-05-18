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

}  // namespace kcdx::hooks
