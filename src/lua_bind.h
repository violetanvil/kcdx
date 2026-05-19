#pragma once

extern "C" {
struct lua_State;
}

namespace kcdx::lua_bind {

// Registers a global `KCDX` table with three functions:
//   KCDX.ScanAndWrite(spec)  -- spec is a table mirroring the TOML schema.
//                                    Runs the patch through patch_engine::ApplyPatch
//                                    so all safety checks apply. Returns (ok, msg).
//   KCDX.ReadBytes(addr, n)  -- read n bytes at absolute address `addr`,
//                                    return them as a string of hex pairs separated
//                                    by spaces. Validates the address is readable.
//   KCDX.GetWHGameBase()     -- returns WHGame.dll base address as a Lua number.
//
// Safe to call once after the lua_State* is captured.
void RegisterKcdxTable(lua_State* L);

// True after RegisterKcdxTable has run and the kcdxMessage_LuaReady
// message has fired. Used by lua_bind_dev::on_ready to fast-path the
// already-ready case, and by anything else that needs to gate on
// "kcdx.* is callable from pak Lua right now."
bool IsKcdxGlobalReady();

}  // namespace kcdx::lua_bind
