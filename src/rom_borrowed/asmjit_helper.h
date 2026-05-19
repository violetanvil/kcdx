// asmjit_helper — asmjit type/register/calling-convention conversion utilities.
//
// Adapted from ReturnOfModding by xiaoxiao921 et al.
// Source: https://github.com/xiaoxiao921/ReturnOfModdingBase/blob/master/src/lua/bindings/asmjit_helper.hpp
// License: MIT. Modifications for kcdx:
//   - namespace lua::memory -> kcdx::rom
//   - dropped <string/string.hpp> (project's local string utils, unused here)
//   - parse_address_component signature matches the .cpp definition (iterator&)
#pragma once

#include <asmjit/asmjit.h>
#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace kcdx::rom {

// Does a given type fit in a general purpose register (i.e. is it an integer type)?
bool is_general_register(const asmjit::TypeId type_id);

// Float / double — go in XMM registers per the SysV/MS x64 ABI.
bool is_XMM_register(const asmjit::TypeId type_id);

asmjit::CallConvId get_call_convention(const std::string& conv);

asmjit::TypeId get_type_id(const std::string& type);

std::optional<asmjit::x86::Gp> get_gp_from_name(const std::string& name);

std::optional<asmjit::x86::Vec> get_xmm_from_name(const std::string& name);

// Walks `name` from `index` until it hits one of +, -, *, ]. Returns the
// substring (with spaces stripped) and advances `index` to one before the
// terminator.
std::string parse_address_component(std::string_view name, std::string_view::iterator& index);

std::optional<uint64_t> parse_number_from_string(std::string_view str);

// Parse `[rcx + 0x10]` / `[rsp+rax*4+0x20]` style memory addresses.
std::optional<asmjit::x86::Mem> get_addr_from_name(std::string_view name, const int64_t rsp_offset = 0);

// Return the list of general-purpose register IDs that are NOT used by the
// memory expression `name`. Used by mid-function hooks to pick a scratch
// register that won't clobber a captured value.
std::vector<uint32_t> get_useable_gp_id_from_name(std::string_view name);

}  // namespace kcdx::rom
