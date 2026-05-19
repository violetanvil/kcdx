// lua_bind_helpers — shared raw-Lua-C-API helpers for pushing /
// checking kcdx userdata types. Used by lua_bind_pointer.cpp,
// lua_bind_memory.cpp, and scripting.cpp.
//
// All helpers operate on the live `lua_State*` directly. No sol2.
// See workspace memory `project-kcd2-sol2-incompatibility` for the
// rule and bisect record.
//
// Stack-effect conventions for "Push*" helpers: +1 (pushes one
// userdata onto the top of the stack, no other side effects).
// "Check*" helpers: 0 (read-only stack inspection; throws a Lua
// error if the value at the given index doesn't match expectations).
#pragma once

#include <cstdint>

extern "C" {
#include "lua.h"
}

#include "lua_memory.h"  // for pointer, value_wrapper_t, metatable names

namespace kcdx::lua_bind_helpers {

// Register the kcdx userdata metatables in LUA_REGISTRYINDEX. Idempotent
// (luaL_newmetatable is a no-op if the name is already registered).
// Must be called once before any of the Push*/Check* helpers can be
// used. lua_memory::bind() calls this; other callers shouldn't need to.
void RegisterMetatables(lua_State* L);

// Push a pointer userdata onto the stack. The userdata payload is a
// single uintptr_t copied from the input pointer.
void PushPointer(lua_State* L, kcdx::lua_memory::pointer p);

// Push a value_wrapper_t userdata onto the stack. The userdata payload
// is the value_wrapper_t itself (by value).
void PushValueWrapper(lua_State* L, kcdx::lua_memory::value_wrapper_t vw);

// Verify the value at stack index `idx` is a kcdx.memory.pointer
// userdata, and return it. Throws a Lua error (`luaL_error` style) if
// the type doesn't match.
kcdx::lua_memory::pointer* CheckPointer(lua_State* L, int idx);

// Same for value_wrapper_t.
kcdx::lua_memory::value_wrapper_t* CheckValueWrapper(lua_State* L, int idx);

}  // namespace kcdx::lua_bind_helpers
