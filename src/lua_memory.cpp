// Pure-C++ method bodies for kcdx::lua_memory::pointer and
// value_wrapper_t.
//
// Lua bindings (metatables, the kcdx.memory.* table-level functions)
// live in lua_bind_helpers.cpp, lua_bind_pointer.cpp, and
// lua_bind_memory.cpp.

#include "lua_memory.h"

#include <cstring>
#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::lua_memory {

// --- pointer --------------------------------------------------------------

pointer::pointer(uintptr_t address) : m_address(address) {}
pointer::pointer()                   : m_address(0)       {}

pointer pointer::add(uintptr_t offset) const { return pointer(m_address + offset); }
pointer pointer::sub(uintptr_t offset) const { return pointer(m_address - offset); }

pointer pointer::rip() const {
    // Read the signed 32-bit displacement at m_address, then advance
    // past the 4-byte displacement field. Convention matches x86_64
    // CALL-rel32 / JMP-rel32 / LEA-rel32 effective-address resolution.
    return add(*(std::int32_t*)m_address).add(4);
}

pointer pointer::rip_cmp() const {
    // CMP rel32 is 5 bytes: 1 opcode + 4 disp. Skip the opcode byte
    // first, then do the rel32 walk.
    return rip().add(1);
}

std::string pointer::get_string()                                       const { return std::string((char*)m_address); }
void        pointer::set_string(const std::string& s, int max_length)   const { strncpy((char*)m_address, s.data(), (size_t)max_length); }
bool        pointer::is_null()                                          const { return m_address == 0; }
bool        pointer::is_valid()                                         const { return !is_null(); }
pointer     pointer::deref()                                            const { return pointer(*(uintptr_t*)m_address); }
uintptr_t   pointer::get_address()                                      const { return m_address; }

// --- value_wrapper_t ------------------------------------------------------

value_wrapper_t::value_wrapper_t(char* val, kcdx::rom::type_info_t type)
    : m_value(val), m_type(type) {}

// Push the wrapped value onto the Lua stack.
//
// PRECISION NOTE for the integer_ case: on KCD2, CryEngine compiled
// Lua 5.1 with LUA_NUMBER=float (24-bit mantissa). Any value > 2^24
// loses low bits when pushed via lua_pushinteger; pointer-magnitude
// values (~2^47) round to a 16MB grid. If the wrapped 64-bit integer
// is actually a pointer/VA (common in dynamic_hook register captures
// and dynamic_call return values), the Lua side will see a corrupted
// address. The right fix is a dedicated type_info_t::ptr_ variant
// that routes through PushPointer; tracked as a follow-up.
// See docs/lua-number-precision.md.
void value_wrapper_t::push_value(lua_State* L) const {
    switch (m_type.m_val) {
        case kcdx::rom::type_info_t::boolean_:
            lua_pushboolean(L, *(bool*)m_value ? 1 : 0);
            break;
        case kcdx::rom::type_info_t::string_:
            lua_pushstring(L, *(const char**)m_value);
            break;
        case kcdx::rom::type_info_t::integer_:
            lua_pushinteger(L, (lua_Integer)*(int64_t*)m_value);
            break;
        case kcdx::rom::type_info_t::float_:
            lua_pushnumber(L, (lua_Number)*(float*)m_value);
            break;
        case kcdx::rom::type_info_t::double_:
            lua_pushnumber(L, (lua_Number)*(double*)m_value);
            break;
        default:
            lua_pushnil(L);
            break;
    }
}

void value_wrapper_t::assign_from(lua_State* L, int stack_index) {
    switch (m_type.m_val) {
        case kcdx::rom::type_info_t::boolean_:
            if (lua_isboolean(L, stack_index)) {
                *(bool*)m_value = lua_toboolean(L, stack_index) != 0;
            }
            break;
        case kcdx::rom::type_info_t::string_:
            if (lua_isstring(L, stack_index)) {
                // NOTE: stores the const char* directly into the slot.
                // Lifetime risk if the Lua string is garbage-collected
                // before the native side reads it. Matches RoM's
                // upstream behavior; flag for hardening once we have
                // real callers.
                *(const char**)m_value = lua_tostring(L, stack_index);
            }
            break;
        case kcdx::rom::type_info_t::integer_:
            if (lua_isnumber(L, stack_index)) {
                *(int64_t*)m_value = (int64_t)lua_tointeger(L, stack_index);
            }
            break;
        case kcdx::rom::type_info_t::float_:
            if (lua_isnumber(L, stack_index)) {
                *(float*)m_value = (float)lua_tonumber(L, stack_index);
            }
            break;
        case kcdx::rom::type_info_t::double_:
            if (lua_isnumber(L, stack_index)) {
                *(double*)m_value = (double)lua_tonumber(L, stack_index);
            }
            break;
        default:
            break;
    }
}

}  // namespace kcdx::lua_memory
