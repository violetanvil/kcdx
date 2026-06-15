// CAP-111 — standalone log stub for compiling the engine pak_reader.cpp into the
// test-plugin DLL.
//
// pak_reader.cpp is engine source: it logs its failure/diagnostic branches
// through the engine's kcdx::log API (src/log.h), whose symbols live in the
// engine's log.cpp. This test plugin compiles pak_reader.cpp into its OWN DLL
// (the cap-109/cap-110 shape — a self-contained artifact built against include/
// + the vendored miniz, not linked against the engine), so those log symbols
// are otherwise unresolved externals at link time. log.cpp itself pulls in the
// whole engine logging subsystem (file streams, dev-mode routing, the
// plugin-handle table) and cannot be linked standalone.
//
// This TU supplies trivial standalone definitions of exactly the kcdx::log
// symbols pak_reader.cpp references — keeping pak_reader.cpp BYTE-IDENTICAL
// between the engine build and this test build (no #ifdef carve-out in
// production source). The read path's failure branches return their outError
// string, which the test reads directly — the test never depends on these log
// lines firing, so a no-op emitter loses nothing the test needs. FormatTo
// mirrors the engine behavior (snprintf into the caller's buffer) so a failure
// message would still format correctly if ever inspected.
//
// Symbol set is identical to cap-110's stub: pak_reader.cpp's read path uses
// LOG_ERROR (printf → EmitEngine + FormatTo) and LOG_DEBUG_KV (→ EmitEngineKV,
// gated by IsCategoryEnabled) with KV(const char*, const std::string&) and
// KV(const char*, unsigned long long) — the same two overloads the CDR parser
// uses.

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "../../src/log.h"

namespace kcdx::log {

bool IsCategoryEnabled(const char* /*category*/) {
    // DEBUG/TRACE gate — off in the standalone test (the test reads the read
    // path's returned values + outError, never the dev log), so the diagnostic
    // LOG_DEBUG_KV in pak_reader.cpp short-circuits here.
    return false;
}

void EmitEngine(Level /*level*/, const char* /*category*/, const char* /*message*/) {
    // No-op: the engine log destination does not exist in the standalone plugin.
}

void EmitEngineKV(Level /*level*/, const char* /*category*/, const char* /*action*/,
                  std::initializer_list<KV> /*kvs*/) {
    // No-op (only reached when IsCategoryEnabled is true, which it never is here).
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

// The two KV constructors pak_reader.cpp's LOG_DEBUG_KV call sites use. The
// engine's full KV-constructor set lives in log.cpp; the standalone build needs
// only the overloads this TU's caller references.
KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.c_str()), svn(val.size()) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}

}  // namespace kcdx::log
