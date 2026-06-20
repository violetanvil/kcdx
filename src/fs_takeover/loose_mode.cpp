#include "loose_mode.h"

#include <cstring>  // std::strchr

namespace kcdx::fs_takeover {

std::string SanitizeLooseMode(const char* mode, bool& changed) {
    changed = false;
    const char* m = (mode && mode[0]) ? mode : "rb";
    const char base = m[0];
    // A write base (w/a) OR any `+` (r+/w+/a+ are all read-WRITE) makes the
    // stream writable, so the exclusive-create `x` flag is valid then.
    const bool writeBase = (base == 'w' || base == 'a' ||
                            std::strchr(m, '+') != nullptr);
    std::string out;
    out.reserve(8);
    for (const char* p = m; *p; ++p) {
        // Drop `x` (exclusive-create) on a read-only base — invalid for the strict
        // UCRT, inert to a read (KI-0026: the "rbx" settings.xml fast-fail). Keep
        // it on any writable base; keep every other flag verbatim.
        if (*p == 'x' && !writeBase) { changed = true; continue; }
        out.push_back(*p);
    }
    if (out.empty()) out = "rb";  // defensive — never hand the CRT an empty mode
    return out;
}

}  // namespace kcdx::fs_takeover
