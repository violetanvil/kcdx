#pragma once

extern "C" {
struct lua_State;
}

namespace kcdx::lua_bind {

// Registers the lowercase `kcdx` Lua global by walking every sub-binder
// (kcdx::lua_memory::bind, kcdx::lua_bind_log::bind, kcdx::lua_bind_hook::bind,
// …) to populate kcdx.memory.*, kcdx.log.*, kcdx.hook.*, etc., then publishes
// the populated table as `_G.kcdx`. Drains any kcdxScriptingInterface
// RegisterFunction queue, flips IsKcdxGlobalReady() true, and fires
// kcdxMessage_LuaReady.
//
// Safe to call once after the lua_State* is captured.
void RegisterKcdxTable(lua_State* L);

// True after RegisterKcdxTable has run and the kcdxMessage_LuaReady
// message has fired. Used by lua_bind_dev::on_ready to fast-path the
// already-ready case, and by anything else that needs to gate on
// "kcdx.* is callable from pak Lua right now."
bool IsKcdxGlobalReady();

}  // namespace kcdx::lua_bind
