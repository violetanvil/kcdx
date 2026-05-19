// See lua_bind_helpers.h.
#include "lua_bind_helpers.h"

extern "C" {
#include "lauxlib.h"
}

#include <new>

#include "lua_memory.h"

namespace kcdx::lua_bind_helpers {

// Forward decls of the metatable-setup functions implemented in
// lua_bind_pointer.cpp (for the pointer metatable). value_wrapper
// is simple enough that we set it up inline below.
namespace pointer_binding {
    // Defined in lua_bind_pointer.cpp. Pushes the populated metatable
    // onto the stack and leaves it there for the caller to associate
    // with the metatable name.
    void PushMetatable(lua_State* L);
}  // namespace pointer_binding

}  // namespace kcdx::lua_bind_helpers

// Defined in lua_bind_dynamic_hook.cpp; forward-decl here so
// RegisterMetatables can call it. Idempotent (luaL_newmetatable guards
// the registration).
namespace kcdx::lua_bind_dynamic_hook {
    void PushHandleMetatable(lua_State* L);
}

namespace kcdx::lua_bind_helpers {

namespace value_wrapper_binding {

// __index handler: kcdx.memory.value_wrapper:get() / :set(v) entries
// live on the same metatable for compactness. The metatable is
// installed by SetupMetatable() below.

int Lua_ValueWrapperGet(lua_State* L) {
    auto* vw = CheckValueWrapper(L, 1);
    vw->push_value(L);  // pushes the unwrapped value, stack effect +1
    return 1;
}

int Lua_ValueWrapperSet(lua_State* L) {
    auto* vw = CheckValueWrapper(L, 1);
    luaL_checkany(L, 2);
    vw->assign_from(L, 2);
    return 0;
}

int Lua_ValueWrapperGc(lua_State* L) {
    auto* vw = CheckValueWrapper(L, 1);
    vw->~value_wrapper_t();
    return 0;
}

// Install the value_wrapper metatable. Idempotent (luaL_newmetatable
// returns 0 + leaves the existing table on the stack if already
// registered). Stack effect: 0.
void SetupMetatable(lua_State* L) {
    if (luaL_newmetatable(L, kcdx::lua_memory::kValueWrapperMetatable) == 0) {
        // Already registered. Pop the duplicate metatable left on stack.
        lua_pop(L, 1);
        return;
    }
    // Stack: [..., mt]
    lua_pushvalue(L, -1);
    lua_setfield(L, -2, "__index");        // mt.__index = mt
    lua_pushcfunction(L, Lua_ValueWrapperGc);
    lua_setfield(L, -2, "__gc");
    lua_pushliteral(L, "kcdx.memory.value_wrapper");
    lua_setfield(L, -2, "__metatable");    // hide from pak Lua
    lua_pushcfunction(L, Lua_ValueWrapperGet);
    lua_setfield(L, -2, "get");
    lua_pushcfunction(L, Lua_ValueWrapperSet);
    lua_setfield(L, -2, "set");
    lua_pop(L, 1);                          // pop mt; back to original stack
}

}  // namespace value_wrapper_binding

void RegisterMetatables(lua_State* L) {
    // pointer metatable (with its 22 methods) is owned by
    // lua_bind_pointer.cpp; that file does the setup.
    pointer_binding::PushMetatable(L);  // leaves the mt on stack
    lua_pop(L, 1);                       // we just want it registered

    value_wrapper_binding::SetupMetatable(L);

    // dynamic_hook handle metatable. Pushes the metatable; we don't
    // need it on the stack here.
    kcdx::lua_bind_dynamic_hook::PushHandleMetatable(L);
    lua_pop(L, 1);
}

// --- Push* ----------------------------------------------------------------

void PushPointer(lua_State* L, kcdx::lua_memory::pointer p) {
    auto* mem = static_cast<kcdx::lua_memory::pointer*>(
        lua_newuserdata(L, sizeof(kcdx::lua_memory::pointer)));
    new (mem) kcdx::lua_memory::pointer(p);
    luaL_getmetatable(L, kcdx::lua_memory::kPointerMetatable);
    lua_setmetatable(L, -2);
}

void PushValueWrapper(lua_State* L, kcdx::lua_memory::value_wrapper_t vw) {
    auto* mem = static_cast<kcdx::lua_memory::value_wrapper_t*>(
        lua_newuserdata(L, sizeof(kcdx::lua_memory::value_wrapper_t)));
    new (mem) kcdx::lua_memory::value_wrapper_t(vw);
    luaL_getmetatable(L, kcdx::lua_memory::kValueWrapperMetatable);
    lua_setmetatable(L, -2);
}

// --- Check* ---------------------------------------------------------------

kcdx::lua_memory::pointer* CheckPointer(lua_State* L, int idx) {
    return static_cast<kcdx::lua_memory::pointer*>(
        luaL_checkudata(L, idx, kcdx::lua_memory::kPointerMetatable));
}

kcdx::lua_memory::value_wrapper_t* CheckValueWrapper(lua_State* L, int idx) {
    return static_cast<kcdx::lua_memory::value_wrapper_t*>(
        luaL_checkudata(L, idx, kcdx::lua_memory::kValueWrapperMetatable));
}

}  // namespace kcdx::lua_bind_helpers

// --- to_lua / to_lua_return ------------------------------------------------
//
// Defined in this TU because they share dependencies (PushPointer +
// PushValueWrapper). Live in the lua_memory namespace per the
// declaration in lua_memory.h.

namespace kcdx::lua_memory {

void to_lua(lua_State*                                       L,
            const kcdx::rom::runtime_func_t::parameters_t*   params,
            uint8_t                                          arg_index,
            const std::vector<kcdx::rom::type_info_t>&       param_types) {
    if (arg_index >= param_types.size()) {
        lua_pushnil(L);
        return;
    }
    const auto& type = param_types[arg_index];
    if (type.m_val == kcdx::rom::type_info_t::none_) {
        lua_pushnil(L);
        return;
    }
    if (type.m_val == kcdx::rom::type_info_t::ptr_) {
        kcdx::lua_bind_helpers::PushPointer(L, pointer(params->get<uintptr_t>(arg_index)));
        return;
    }
    if (type.m_custom) {
        // Plugin-registered custom marshaler. Contract: feeder pushes
        // exactly one value onto the stack (stack effect +1). Will be
        // exercised by Phase 5e when kcdxScriptingInterface::RegisterType
        // lands.
        type.m_custom(L, params->get_arg_ptr(arg_index));
        return;
    }
    kcdx::lua_bind_helpers::PushValueWrapper(
        L, value_wrapper_t(params->get_arg_ptr(arg_index), type));
}

void to_lua_return(lua_State*                                 L,
                   kcdx::rom::runtime_func_t::return_value_t* return_value,
                   kcdx::rom::type_info_t                     return_type) {
    if (return_type.m_val == kcdx::rom::type_info_t::none_) {
        lua_pushnil(L);
        return;
    }
    if (return_type.m_val == kcdx::rom::type_info_t::ptr_) {
        kcdx::lua_bind_helpers::PushPointer(L, pointer((uintptr_t)return_value->get()));
        return;
    }
    if (return_type.m_custom) {
        return_type.m_custom(L, (char*)return_value->get());
        return;
    }
    kcdx::lua_bind_helpers::PushValueWrapper(
        L, value_wrapper_t((char*)return_value->get(), return_type));
}

}  // namespace kcdx::lua_memory
