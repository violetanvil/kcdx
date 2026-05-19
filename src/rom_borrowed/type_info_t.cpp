// Adapted from ReturnOfModding (https://github.com/xiaoxiao921/ReturnOfModdingBase),
// MIT. See type_info_t.h for the adaptation rationale.
#include "type_info_t.h"

#include <unordered_map>

namespace kcdx::rom {

static std::unordered_map<std::string, type_info_t> string_to_type_info_id;

type_info_t get_type_info_from_string(const std::string& s) {
    const auto it = string_to_type_info_id.find(s);
    if (it != string_to_type_info_id.end()) {
        return it->second;
    }

    if ((s.find("const") != std::string::npos &&
         s.find("char")  != std::string::npos &&
         s.find('*')     != std::string::npos) ||
        s.find("string") != std::string::npos) {
        return {type_info_t::string_};
    } else if (s.find("bool") != std::string::npos) {
        return {type_info_t::boolean_};
    } else if (s.find("ptr")     != std::string::npos ||
               s.find("pointer") != std::string::npos ||
               s.find('*')       != std::string::npos) {
        // passing kcdx::rom::pointer
        return {type_info_t::ptr_};
    } else if (s.find("float") != std::string::npos) {
        return {type_info_t::float_};
    } else if (s.find("double") != std::string::npos) {
        return {type_info_t::double_};
    } else {
        return {type_info_t::integer_};
    }
}

void add_type_info_from_string(const std::string& s, type_info_feeder_t type_info_feed) {
    string_to_type_info_id[s] = type_info_t{
        /*m_val=*/    type_info_t::custom_type_start_,
        /*m_custom=*/ type_info_feed,
    };
}

}  // namespace kcdx::rom
