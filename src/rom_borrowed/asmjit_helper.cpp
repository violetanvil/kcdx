// Adapted from ReturnOfModding (https://github.com/xiaoxiao921/ReturnOfModdingBase),
// MIT. See asmjit_helper.h for the adaptation rationale.
#include "asmjit_helper.h"

#include <charconv>
#include <iterator>
#include <unordered_map>

#include "../log.h"

namespace kcdx::rom {

bool is_general_register(const asmjit::TypeId type_id) {
    switch (type_id) {
    case asmjit::TypeId::kInt8:
    case asmjit::TypeId::kUInt8:
    case asmjit::TypeId::kInt16:
    case asmjit::TypeId::kUInt16:
    case asmjit::TypeId::kInt32:
    case asmjit::TypeId::kUInt32:
    case asmjit::TypeId::kInt64:
    case asmjit::TypeId::kUInt64:
    case asmjit::TypeId::kIntPtr:
    case asmjit::TypeId::kUIntPtr: return true;
    default:                       return false;
    }
}

bool is_XMM_register(const asmjit::TypeId type_id) {
    switch (type_id) {
    case asmjit::TypeId::kFloat32:
    case asmjit::TypeId::kFloat64: return true;
    default:                       return false;
    }
}

asmjit::CallConvId get_call_convention(const std::string& conv) {
    if (conv == "cdecl")    return asmjit::CallConvId::kCDecl;
    if (conv == "stdcall")  return asmjit::CallConvId::kStdCall;
    if (conv == "fastcall") return asmjit::CallConvId::kFastCall;
    // asmjit removed kHost; kCDecl is the host default on x64 (gets
    // replaced with the correct host convention by asmjit internally).
    return asmjit::CallConvId::kCDecl;
}

asmjit::TypeId get_type_id(const std::string& type) {
    if (type.find('*') != std::string::npos) {
        return asmjit::TypeId::kUIntPtr;
    }

#define TYPEID_MATCH_STR_IF(var, T)                                      \
    if (var == #T) {                                                     \
        return asmjit::TypeId(asmjit::TypeUtils::TypeIdOfT<T>::kTypeId); \
    }
#define TYPEID_MATCH_STR_ELSEIF(var, T)                                  \
    else if (var == #T) {                                                \
        return asmjit::TypeId(asmjit::TypeUtils::TypeIdOfT<T>::kTypeId); \
    }

    TYPEID_MATCH_STR_IF(type, signed char)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned char)
    TYPEID_MATCH_STR_ELSEIF(type, short)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned short)
    TYPEID_MATCH_STR_ELSEIF(type, int)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned int)
    TYPEID_MATCH_STR_ELSEIF(type, long)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned long)
    TYPEID_MATCH_STR_ELSEIF(type, __int64)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned __int64)
    TYPEID_MATCH_STR_ELSEIF(type, long long)
    TYPEID_MATCH_STR_ELSEIF(type, unsigned long long)
    TYPEID_MATCH_STR_ELSEIF(type, char)
    TYPEID_MATCH_STR_ELSEIF(type, char16_t)
    TYPEID_MATCH_STR_ELSEIF(type, char32_t)
    TYPEID_MATCH_STR_ELSEIF(type, wchar_t)
    TYPEID_MATCH_STR_ELSEIF(type, uint8_t)
    TYPEID_MATCH_STR_ELSEIF(type, int8_t)
    TYPEID_MATCH_STR_ELSEIF(type, uint16_t)
    TYPEID_MATCH_STR_ELSEIF(type, int16_t)
    TYPEID_MATCH_STR_ELSEIF(type, int32_t)
    TYPEID_MATCH_STR_ELSEIF(type, uint32_t)
    TYPEID_MATCH_STR_ELSEIF(type, uint64_t)
    TYPEID_MATCH_STR_ELSEIF(type, int64_t)
    TYPEID_MATCH_STR_ELSEIF(type, float)
    TYPEID_MATCH_STR_ELSEIF(type, double)
    TYPEID_MATCH_STR_ELSEIF(type, bool)
    TYPEID_MATCH_STR_ELSEIF(type, void)
    else if (type == "intptr_t") {
        return asmjit::TypeId::kIntPtr;
    }
    else if (type == "uintptr_t") {
        return asmjit::TypeId::kUIntPtr;
    }

#undef TYPEID_MATCH_STR_IF
#undef TYPEID_MATCH_STR_ELSEIF

    return asmjit::TypeId::kVoid;
}

std::optional<asmjit::x86::Gp> get_gp_from_name(const std::string& name) {
    // clang-format off
    static const std::unordered_map<std::string, asmjit::x86::Gp> reg_map = {
        // 64-bit
        {"rax", asmjit::x86::rax}, {"rbx", asmjit::x86::rbx}, {"rcx", asmjit::x86::rcx}, {"rdx", asmjit::x86::rdx},
        {"rsi", asmjit::x86::rsi}, {"rdi", asmjit::x86::rdi}, {"rbp", asmjit::x86::rbp}, {"rsp", asmjit::x86::rsp},
        {"r8", asmjit::x86::r8}, {"r9", asmjit::x86::r9}, {"r10", asmjit::x86::r10}, {"r11", asmjit::x86::r11},
        {"r12", asmjit::x86::r12}, {"r13", asmjit::x86::r13}, {"r14", asmjit::x86::r14}, {"r15", asmjit::x86::r15},
        // 32-bit
        {"eax", asmjit::x86::eax}, {"ebx", asmjit::x86::ebx}, {"ecx", asmjit::x86::ecx}, {"edx", asmjit::x86::edx},
        {"esi", asmjit::x86::esi}, {"edi", asmjit::x86::edi}, {"ebp", asmjit::x86::ebp}, {"esp", asmjit::x86::esp},
        {"r8d", asmjit::x86::r8d}, {"r9d", asmjit::x86::r9d}, {"r10d", asmjit::x86::r10d}, {"r11d", asmjit::x86::r11d},
        {"r12d", asmjit::x86::r12d}, {"r13d", asmjit::x86::r13d}, {"r14d", asmjit::x86::r14d}, {"r15d", asmjit::x86::r15d},
        // 16-bit
        {"ax", asmjit::x86::ax}, {"bx", asmjit::x86::bx}, {"cx", asmjit::x86::cx}, {"dx", asmjit::x86::dx},
        {"si", asmjit::x86::si}, {"di", asmjit::x86::di}, {"bp", asmjit::x86::bp}, {"sp", asmjit::x86::sp},
        {"r8w", asmjit::x86::r8w}, {"r9w", asmjit::x86::r9w}, {"r10w", asmjit::x86::r10w}, {"r11w", asmjit::x86::r11w},
        {"r12w", asmjit::x86::r12w}, {"r13w", asmjit::x86::r13w}, {"r14w", asmjit::x86::r14w}, {"r15w", asmjit::x86::r15w},
        // 8-bit
        {"al", asmjit::x86::al}, {"ah", asmjit::x86::ah}, {"bl", asmjit::x86::bl}, {"bh", asmjit::x86::bh},
        {"cl", asmjit::x86::cl}, {"ch", asmjit::x86::ch}, {"dl", asmjit::x86::dl}, {"dh", asmjit::x86::dh},
        {"sil", asmjit::x86::sil}, {"dil", asmjit::x86::dil}, {"bpl", asmjit::x86::bpl}, {"spl", asmjit::x86::spl},
        {"r8b", asmjit::x86::r8b}, {"r9b", asmjit::x86::r9b}, {"r10b", asmjit::x86::r10b}, {"r11b", asmjit::x86::r11b},
        {"r12b", asmjit::x86::r12b}, {"r13b", asmjit::x86::r13b}, {"r14b", asmjit::x86::r14b}, {"r15b", asmjit::x86::r15b},
    };
    // clang-format on

    const auto it = reg_map.find(name);
    if (it != reg_map.end()) return it->second;
    return std::nullopt;
}

std::optional<asmjit::x86::Vec> get_xmm_from_name(const std::string& name) {
    static const std::unordered_map<std::string, asmjit::x86::Vec> reg_map = {
        {"xmm0", asmjit::x86::xmm0},   {"xmm1", asmjit::x86::xmm1},   {"xmm2", asmjit::x86::xmm2},
        {"xmm3", asmjit::x86::xmm3},   {"xmm4", asmjit::x86::xmm4},   {"xmm5", asmjit::x86::xmm5},
        {"xmm6", asmjit::x86::xmm6},   {"xmm7", asmjit::x86::xmm7},   {"xmm8", asmjit::x86::xmm8},
        {"xmm9", asmjit::x86::xmm9},   {"xmm10", asmjit::x86::xmm10}, {"xmm11", asmjit::x86::xmm11},
        {"xmm12", asmjit::x86::xmm12}, {"xmm13", asmjit::x86::xmm13}, {"xmm14", asmjit::x86::xmm14},
        {"xmm15", asmjit::x86::xmm15},
    };

    const auto it = reg_map.find(name);
    if (it != reg_map.end()) return it->second;
    return std::nullopt;
}

std::string parse_address_component(std::string_view name, std::string_view::iterator& index) {
    auto end = std::find_if(index, name.end(),
        [](char c) { return c == '+' || c == '-' || c == '*' || c == ']'; });

    std::string sub_str;
    std::remove_copy_if(index, end, std::back_inserter(sub_str),
        [](char c) { return c == ' '; });

    index = --end;
    return sub_str;
}

std::optional<uint64_t> parse_number_from_string(std::string_view str) {
    int base;
    if (str.size() > 2 && str[0] == '0' && (str[1] == 'x' || str[1] == 'X')) {
        str.remove_prefix(2);
        base = 16;
    } else {
        switch (str.back()) {
        case 'b': str.remove_suffix(1); base = 2;  break;
        case 'o': str.remove_suffix(1); base = 8;  break;
        case 'd': str.remove_suffix(1); base = 10; break;
        case 'h': str.remove_suffix(1); base = 16; break;
        default:  base = 10;
        }
    }
    uint64_t num{};
    auto [ptr, ec] = std::from_chars(str.data(), str.data() + str.size(), num, base);
    if (ec == std::errc()) return num;
    return std::nullopt;
}

std::optional<asmjit::x86::Mem> get_addr_from_name(std::string_view name, const int64_t rsp_offset) {
    std::string base_str;
    std::string index_str;
    int32_t  offset = 0;
    uint32_t shift  = 0;
    for (auto it = name.begin(); it != name.end(); ++it) {
        switch (*it) {
        case '[': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (num.has_value()) {
                return asmjit::x86::ptr(*num);
            } else {
                base_str = sub_str;
            }
            break;
        }
        case '+': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (num.has_value()) {
                offset += (int32_t)*num;
            } else {
                index_str = sub_str;
            }
            break;
        }
        case '-': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (num.has_value()) {
                offset -= (int32_t)*num;
            } else {
                // I'm not sure this should happen. May need a register to solve it.
                log::Error("rom::get_addr_from_name: can't sub a register");
                return std::nullopt;
            }
            break;
        }
        case '*': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (num.has_value()) {
                uint64_t temp_num = *num;
                while (temp_num) {
                    temp_num >>= 1;
                    shift++;
                }
                shift--;
            } else {
                log::Error("rom::get_addr_from_name: can't parse the shift");
                return std::nullopt;
            }
            break;
        }
        }
    }

    auto base = get_gp_from_name(base_str);
    if (!base.has_value()) {
        log::ErrorF("rom::get_addr_from_name: failed to get base reg from: %s",
                    base_str.c_str());
        return std::nullopt;
    }

    if (*base == asmjit::x86::rsp) {
        offset += (int32_t)rsp_offset;
    }

    if (auto idx = get_gp_from_name(index_str); idx.has_value()) {
        return asmjit::x86::ptr(*base, *idx, shift, offset);
    } else {
        return asmjit::x86::ptr(*base, offset);
    }
}

std::vector<uint32_t> get_useable_gp_id_from_name(std::string_view name) {
    std::vector<uint32_t> useable_gp_id_list = {
        asmjit::x86::Gp::kIdAx,  asmjit::x86::Gp::kIdBx,  asmjit::x86::Gp::kIdCx,
        asmjit::x86::Gp::kIdDx,  asmjit::x86::Gp::kIdBp,  asmjit::x86::Gp::kIdSi,
        asmjit::x86::Gp::kIdDi,  asmjit::x86::Gp::kIdR8,  asmjit::x86::Gp::kIdR9,
        asmjit::x86::Gp::kIdR10, asmjit::x86::Gp::kIdR11, asmjit::x86::Gp::kIdR12,
        asmjit::x86::Gp::kIdR13, asmjit::x86::Gp::kIdR14, asmjit::x86::Gp::kIdR15,
    };
    auto delete_gp = [&](const std::string& str) {
        if (auto idx = get_gp_from_name(str); idx.has_value()) {
            auto it = std::find(useable_gp_id_list.begin(), useable_gp_id_list.end(), idx->id());
            if (it != useable_gp_id_list.end()) {
                useable_gp_id_list.erase(it);
            }
        }
    };
    for (auto it = name.begin(); it != name.end(); ++it) {
        switch (*it) {
        case '[': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (num.has_value()) {
                return useable_gp_id_list;
            } else {
                delete_gp(sub_str);
            }
            break;
        }
        case '+': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (!num.has_value()) {
                delete_gp(sub_str);
            }
            break;
        }
        case '-': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (!num.has_value()) {
                log::Error("rom::get_useable_gp_id_from_name: can't sub a register");
                return std::vector<uint32_t>();
            }
            break;
        }
        case '*': {
            std::string sub_str = parse_address_component(name, ++it);
            auto num = parse_number_from_string(sub_str);
            if (!num.has_value()) {
                log::Error("rom::get_useable_gp_id_from_name: can't parse the shift");
                return std::vector<uint32_t>();
            }
            break;
        }
        }
    }
    return useable_gp_id_list;
}

}  // namespace kcdx::rom
