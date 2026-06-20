// CAP-115 — standalone log stub for compiling the engine asset_index.cpp +
// pak_reader.cpp into the test-plugin DLL.
//
// Both engine TUs log through the engine's kcdx::log API (src/log.h), whose
// symbols live in log.cpp — which pulls in the whole engine logging subsystem
// (file streams, dev-mode routing, the plugin-handle table) and cannot be linked
// standalone. This test plugin compiles asset_index.cpp + pak_reader.cpp into its
// OWN DLL (the cap-110/111/112 shape), so those log symbols are otherwise
// unresolved externals at link time.
//
// This TU supplies trivial standalone definitions of exactly the kcdx::log
// symbols the two TUs reference — keeping both BYTE-IDENTICAL between the engine
// build and this test build (no #ifdef carve-out in production source). The
// failure branches return their outError string (pak_reader) or are diagnostic-
// only (asset_index), which the test never depends on — a no-op emitter loses
// nothing the test needs. FormatTo mirrors the engine behavior (snprintf into
// the caller's buffer) so a message would still format correctly if inspected.
//
// Symbol set = the union the two TUs use: EmitEngine + EmitEngineKV +
// IsCategoryEnabled + FormatTo, and the KV constructors/factories the call sites
// reference — KV(const char*, const std::string&), KV(const char*, unsigned long
// long), and the static KV::BareStr (asset_index's root/pak-skip detail lines).
// pak_reader.cpp uses the first two; asset_index.cpp adds BareStr.

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "../../src/log.h"

namespace kcdx::log {

bool IsCategoryEnabled(const char* /*category*/) {
    // DEBUG/TRACE gate — off in the standalone test (the test reads the index's
    // returned values, never the dev log), so a diagnostic LOG_DEBUG_KV
    // short-circuits here.
    return false;
}

void EmitEngine(Level /*level*/, const char* /*category*/, const char* /*message*/) {
    // No-op: the engine log destination does not exist in the standalone plugin.
}

void EmitEngineKV(Level /*level*/, const char* /*category*/, const char* /*action*/,
                  std::initializer_list<KV> /*kvs*/) {
    // No-op (LOG_DEBUG_KV is gated off above; LOG_ERROR_KV reaches here and is
    // discarded — the test reads the index outputs, not the dev log).
}

namespace detail {
void FormatTo(char* buf, size_t bufsize, const char* fmt, ...) {
    if (!buf || bufsize == 0) return;
    va_list ap;
    va_start(ap, fmt);
    std::vsnprintf(buf, bufsize, fmt, ap);
    va_end(ap);
}
}  // namespace detail

// The KV constructors + factory file_handle.cpp's LOG_*_KV call sites use. The
// engine's full KV set lives in log.cpp; the standalone build needs only these.
KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.c_str()), svn(val.size()) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}
// file_handle.cpp's Seek logs a signed `long long` offset (the INT-kind ctor).
KV::KV(const char* key, long long val)
    : k(key), kind(INT), i(val) {}

KV KV::BareStr(const char* key, const char* val) {
    KV kv(key, val);   // delegate to the (const char*, const char*) ctor...
    kv.kind = BARE_STR;  // ...then re-tag as the unquoted identifier form.
    return kv;
}

// The (const char*, const char*) ctor BareStr delegates to (the const char*
// overload pak_reader/asset_index do not otherwise reference, but BareStr needs).
KV::KV(const char* key, const char* val)
    : k(key), kind(STR), sv(val), svn(val ? std::char_traits<char>::length(val) : 0) {}

}  // namespace kcdx::log
