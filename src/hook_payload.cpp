// kcdx::hook_payload — mode token <-> enum helpers. Data-only; see
// hook_payload.h for the struct contract.

#include "hook_payload.h"

namespace kcdx::hook_payload {

bool ParseMode(const std::string& token, Mode& out) {
    if (token == "before")   { out = Mode::Before;   return true; }
    if (token == "after")    { out = Mode::After;     return true; }
    if (token == "around")   { out = Mode::Around;    return true; }
    if (token == "replace")  { out = Mode::Replace;   return true; }
    if (token == "mid")      { out = Mode::Mid;       return true; }
    if (token == "callsite") { out = Mode::Callsite;  return true; }
    return false;
}

const char* ModeToken(Mode m) {
    switch (m) {
        case Mode::Before:   return "before";
        case Mode::After:    return "after";
        case Mode::Around:   return "around";
        case Mode::Replace:  return "replace";
        case Mode::Mid:      return "mid";
        case Mode::Callsite: return "callsite";
    }
    return "<unknown>";
}

}  // namespace kcdx::hook_payload
