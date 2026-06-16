// CAP-114 — standalone dependency stub for compiling the engine asset_index.cpp
// into the test-plugin DLL.
//
// asset_index.cpp is engine source. To compile it standalone into this test DLL
// (the cap-110 shape — a self-contained artifact built against include/ + the
// one engine source under test, NOT linked against the engine), the external
// symbols it references must be supplied here so they are not unresolved at link
// time. asset_index.cpp references exactly:
//   - kcdx::asset_overlay::NormalizeVPath  — the key fold (REAL impl below, so
//     the test's inserts + ResolveVPath's lookups agree on the same fold the
//     engine uses);
//   - kcdx::asset_overlay::GetOverlayMap   — only reached by BuildAssetIndex
//     (which the test does NOT call); a trivial empty-map stub suffices;
//   - kcdx::fs_takeover::ParsePakCentralDirectory — only reached by
//     BuildAssetIndex; a trivial "no entries" stub suffices;
//   - the kcdx::log symbols its LOG_*_KV diagnostics expand to (no-op, mirroring
//     cap-110's log stub — the test reads ResolveVPath's RETURN, never a log).
//
// The test exercises ResolveVPath (which needs only NormalizeVPath); the other
// three are link-completeness stubs for the BuildAssetIndex code path the test
// does not drive. asset_index.cpp stays BYTE-IDENTICAL between the engine build
// and this test build (no #ifdef carve-out in production source).

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <vector>

#include "../../src/asset_overlay.h"
#include "../../src/fs_takeover/pak_reader.h"
#include "../../src/log.h"

// === asset_overlay::NormalizeVPath — the REAL fold (lowercase + '\' -> '/'). ==
// Must match src/asset_overlay.cpp's definition exactly, since the test keys its
// inserts with it and ResolveVPath looks up with it (a divergent fold would make
// the test's (d) normalization assertion meaningless). Lowercase ASCII + convert
// backslashes to forward slashes — the documented contract in asset_overlay.h.
namespace kcdx::asset_overlay {

std::string NormalizeVPath(const std::string& vpath) {
    std::string out;
    out.reserve(vpath.size());
    for (char c : vpath) {
        if (c == '\\') c = '/';
        else if (c >= 'A' && c <= 'Z') c = static_cast<char>(c - 'A' + 'a');
        out.push_back(c);
    }
    return out;
}

// Link-completeness only — BuildAssetIndex reads this; the test never calls
// BuildAssetIndex, so an always-empty overlay map is correct for the stub.
const OverlayMap& GetOverlayMap() {
    static const OverlayMap kEmpty;
    return kEmpty;
}

}  // namespace kcdx::asset_overlay

// === ParsePakCentralDirectory — link-completeness stub. =====================
// Reached only by BuildAssetIndex (not driven by the test). Returns "no entries"
// so a stray call is well-defined rather than unresolved.
namespace kcdx::fs_takeover {

bool ParsePakCentralDirectory(const std::wstring& /*pakPath*/,
                              std::vector<PakEntry>& outEntries,
                              std::string& /*outError*/) {
    outEntries.clear();
    return true;
}

}  // namespace kcdx::fs_takeover

// === kcdx::log — no-op emitters (mirrors cap-110's stub). ===================
namespace kcdx::log {

bool IsCategoryEnabled(const char* /*category*/) { return false; }

void EmitEngine(Level, const char*, const char*) {}

void EmitEngineKV(Level, const char*, const char*,
                  std::initializer_list<KV>) {}

namespace detail {
void FormatTo(char* buf, size_t bufsize, const char* fmt, ...) {
    if (!buf || bufsize == 0) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, bufsize, fmt, ap);
    va_end(ap);
}
}  // namespace detail

// The KV constructors asset_index.cpp's LOG_*_KV call sites reference: the
// unsigned-count form ((uint64_t) casts), the std::string form (data_dir /
// error / pak), and BareStr (the detail strings).
KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.c_str()), svn(val.size()) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}
KV KV::BareStr(const char* key, const char* val) {
    KV kv(key, val);          // delegates to the const char* ctor below
    kv.kind = BARE_STR;
    return kv;
}
KV::KV(const char* key, const char* val)
    : k(key), kind(STR), sv(val), svn(val ? std::char_traits<char>::length(val) : 0) {}

}  // namespace kcdx::log
