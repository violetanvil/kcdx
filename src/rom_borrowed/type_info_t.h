// type_info_t — string-to-type-tag mapping for asmjit register-class dispatch.
//
// Adapted from ReturnOfModding by xiaoxiao921 et al.
// Source: https://github.com/xiaoxiao921/ReturnOfModdingBase/blob/master/src/lua/bindings/type_info_t.hpp
// License: MIT. Modifications for kcdx:
//   - namespace lua::memory -> kcdx::rom
//   - Custom-type feeder typedef returns void; contract is
//     "push exactly one value onto the Lua stack" via raw Lua C API.
//     Caller checks stack-top to consume the pushed value.
#pragma once

#include <string>

extern "C" {
#include "lua.h"
}

namespace kcdx::rom {

// Custom marshaler: given a lua_State* and a byte pointer into the
// runtime_func_t arg/return slots, push exactly one Lua value onto
// the stack. Stack effect: +1.
typedef void (*type_info_feeder_t)(lua_State* state_, char* arg_ptr);

struct type_info_t {
    enum type_info_id_t {
        none_,
        boolean_,
        string_,
        integer_,
        ptr_,
        float_,
        double_,
        custom_type_start_,
    };

    type_info_id_t      m_val    = none_;
    type_info_feeder_t  m_custom = nullptr;
};

type_info_t get_type_info_from_string(const std::string& s);

void add_type_info_from_string(const std::string& s, type_info_feeder_t type_info_feed);

}  // namespace kcdx::rom
