// type_info_t — string-to-type-tag mapping for asmjit register-class dispatch.
//
// Adapted from ReturnOfModding by xiaoxiao921 et al.
// Source: https://github.com/xiaoxiao921/ReturnOfModdingBase/blob/master/src/lua/bindings/type_info_t.hpp
// License: MIT. Modifications for kcdx:
//   - namespace lua::memory -> kcdx::rom
//   - sol/sol_include.hpp -> sol/sol.hpp (single-header vendoring)
//   - Custom-type feeder typedef kept; takes raw lua_State* and bytes.
#pragma once

#include <sol/sol.hpp>
#include <string>

namespace kcdx::rom {

typedef sol::object (*type_info_feeder_t)(lua_State* state_, char* arg_ptr);

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
