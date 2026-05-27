// Lua C-API binding for kcdx.memory.pointer.
//
// Owns the kcdx.memory.pointer metatable: 22 instance methods + the
// __gc / __index / __metatable slots. Called from
// lua_bind_helpers::RegisterMetatables() at kcdx.memory.bind() time.

#include <cstdint>
#include <new>
#include <string>
#include <type_traits>

extern "C" {
#include "lua.h"
#include "lauxlib.h"
}

#include "log.h"
#include "lua_bind_helpers.h"
#include "lua_memory.h"

namespace kcdx::lua_bind_helpers {
namespace pointer_binding {

namespace {

using kcdx::lua_memory::pointer;

// --- typed get_* / set_* methods -----------------------------------------
//
// Templated over the underlying C++ type. Lua wraps these as concrete
// `int(*)(lua_State*)` functions in the metatable bindings below; the
// template lets us share the body.

// Pointer:get_<type>() implementation.
//
// Precision caveat for get_qword (T = uint64_t): the value gets pushed
// via lua_pushinteger, which on KCD2 stores via LUA_NUMBER=float and
// rounds anything > 2^24 to a float-grid. For pointer-magnitude values
// (~2^47), the step size is 16MB. If you're reading a pointer from
// memory and need to use it, use pointer:deref() instead (returns a
// new pointer userdata) — that path stays in the safe channel.
// See docs/lua-number-precision.md.
template <typename T>
int Get(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:get_*: null pointer");
    }
    // Differentiate the Lua type by the T's domain.
    if constexpr (std::is_floating_point_v<T>) {
        lua_pushnumber(L, (lua_Number)p->get<T>());
    } else {
        lua_pushinteger(L, (lua_Integer)p->get<T>());
    }
    return 1;
}

template <typename T>
int Set(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:set_*: null pointer");
    }
    if constexpr (std::is_floating_point_v<T>) {
        T v = (T)luaL_checknumber(L, 2);
        p->set<T>(v);
    } else {
        T v = (T)luaL_checkinteger(L, 2);
        p->set<T>(v);
    }
    return 0;
}

// --- arithmetic / chainable methods --------------------------------------

int Add(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    lua_Integer off = luaL_checkinteger(L, 2);
    PushPointer(L, p->add((uintptr_t)off));
    return 1;
}

int Sub(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    lua_Integer off = luaL_checkinteger(L, 2);
    PushPointer(L, p->sub((uintptr_t)off));
    return 1;
}

int Rip(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:rip: null pointer");
    }
    PushPointer(L, p->rip());
    return 1;
}

int RipCmp(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:rip_cmp: null pointer");
    }
    PushPointer(L, p->rip_cmp());
    return 1;
}

int Deref(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:deref: null pointer");
    }
    PushPointer(L, p->deref());
    return 1;
}

// --- string variants -----------------------------------------------------

int GetString(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:get_string: null pointer");
    }
    std::string s = p->get_string();
    lua_pushlstring(L, s.data(), s.size());
    return 1;
}

int SetString(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    if (p->is_null()) {
        return luaL_error(L, "kcdx.memory.pointer:set_string: null pointer");
    }
    size_t len = 0;
    const char* s = luaL_checklstring(L, 2, &len);
    lua_Integer max_len = luaL_checkinteger(L, 3);
    p->set_string(std::string(s, len), (int)max_len);
    return 0;
}

// --- predicates / accessors ---------------------------------------------

int IsNull(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    lua_pushboolean(L, p->is_null() ? 1 : 0);
    return 1;
}

int IsValid(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    lua_pushboolean(L, p->is_valid() ? 1 : 0);
    return 1;
}

// pointer:get_address() -> integer (LOSSY at pointer magnitudes).
//
// Returns the raw integer VA. Kept for backwards compatibility and
// for plugins that only need an opaque numeric ID. On KCD2 this value
// is rounded to a 16MB grid because CryEngine's Lua 5.1 is built with
// LUA_NUMBER=float (24-bit mantissa). DO NOT pass the result back to
// any kcdx API that expects an exact address (dynamic_hook target,
// dynamic_call target, etc.) — pass the pointer userdata itself.
// See docs/lua-number-precision.md.
int GetAddress(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    lua_pushinteger(L, (lua_Integer)p->get_address());
    return 1;
}

// --- gc ------------------------------------------------------------------

int Gc(lua_State* L) {
    auto* p = CheckPointer(L, 1);
    p->~pointer();
    return 0;
}

// Method table for the metatable's __index. Listed once here, walked
// once at registration time.
const luaL_Reg kMethods[] = {
    {"add",        Add},
    {"sub",        Sub},
    {"rip",        Rip},
    {"rip_cmp",    RipCmp},
    {"deref",      Deref},
    {"is_null",    IsNull},
    {"is_valid",   IsValid},
    {"get_address",GetAddress},
    {"get_byte",   Get<uint8_t>},
    {"get_word",   Get<uint16_t>},
    {"get_dword",  Get<uint32_t>},
    {"get_qword",  Get<uint64_t>},
    {"get_float",  Get<float>},
    {"get_double", Get<double>},
    {"set_byte",   Set<uint8_t>},
    {"set_word",   Set<uint16_t>},
    {"set_dword",  Set<uint32_t>},
    {"set_qword",  Set<uint64_t>},
    {"set_float",  Set<float>},
    {"set_double", Set<double>},
    {"get_string", GetString},
    {"set_string", SetString},
    {nullptr, nullptr},
};

}  // namespace

// Forward-declared in lua_bind_helpers.cpp. Pushes the populated
// metatable onto the stack; idempotent if already registered.
// Stack effect: +1 (caller is expected to pop or use it).
void PushMetatable(lua_State* L) {
    LOG_INFO("LUA_BIND",
        "      pointer::PushMetatable ENTER L=%p", (void*)L);
    if (luaL_newmetatable(L, kcdx::lua_memory::kPointerMetatable) == 0) {
        // Already registered; the existing metatable is on the stack.
        LOG_INFO("LUA_BIND",
            "      pointer::PushMetatable EXIT (already registered)");
        return;
    }
    // mt at stack top
    LOG_INFO("LUA_BIND", "        before __index/__gc/__metatable bind");
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");        // mt.__index = mt
    lua_pushcfunction(L, Gc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "kcdx.memory.pointer");
    lua_setfield(L, -2, "__metatable");    // hide from pak Lua
    LOG_INFO("LUA_BIND", "        after  metamethods bound");

    // Populate methods
    LOG_INFO("LUA_BIND", "        before methods bind");
    for (const luaL_Reg* m = kMethods; m->name; ++m) {
        lua_pushcfunction(L, m->func);
        lua_setfield(L, -2, m->name);
    }
    LOG_INFO("LUA_BIND", "        after  methods bind");
    LOG_INFO("LUA_BIND", "      pointer::PushMetatable EXIT (freshly registered)");
}

}  // namespace pointer_binding
}  // namespace kcdx::lua_bind_helpers
