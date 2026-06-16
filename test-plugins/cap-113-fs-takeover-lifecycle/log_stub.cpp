// CAP-113 — standalone log stub for compiling the engine open/read slot impls +
// the asset index + the pak reader into the test-plugin DLL.
//
// Every engine TU compiled here (open_slots.cpp, read_slots.cpp,
// file_handle.cpp, asset_index.cpp, pak_reader.cpp) logs through the engine's
// kcdx::log API (src/log.h), whose symbols live in log.cpp — which pulls in the
// whole engine logging subsystem (file streams, dev-mode routing, the plugin-
// handle table) and cannot be linked standalone. This test plugin compiles those
// TUs into its OWN DLL (the cap-110/111/112 shape), so those log symbols are
// otherwise unresolved externals at link time.
//
// This TU supplies trivial standalone definitions of exactly the kcdx::log
// symbols the compiled TUs reference — keeping each one BYTE-IDENTICAL between
// the engine build and this test build (no test carve-out in production source).
// The failure branches return their outError string (pak_reader) or are
// diagnostic-only (the slot impls / asset_index), which the test never depends
// on — a no-op emitter loses nothing the test needs. FormatTo mirrors the engine
// behavior (snprintf into the caller's buffer) so a message would still format
// correctly if inspected.
//
// Symbol set = the union the compiled TUs use: EmitEngine + EmitEngineKV +
// IsCategoryEnabled + FormatTo, and the KV constructors/factories the call sites
// reference — KV(const char*, const std::string&) (vpath/disk/error details),
// KV(const char*, unsigned long long) (uint64_t handle/size/count widths on
// Win64), KV(const char*, long long) (the signed offset/origin/errno/cap details
// the open/read/seek slots log), and the static KV::BareStr (slot/mode/kind/
// detail identifier lines). The signed-long-long overload is the one the open/
// read slot TUs add beyond the cap-112 index/reader stub set.

#include <cstdarg>
#include <cstdio>
#include <initializer_list>
#include <string>
#include <string_view>

#include "../../src/log.h"

namespace kcdx::log {

bool IsCategoryEnabled(const char* /*category*/) {
    // DEBUG/TRACE gate — off in the standalone test (the test reads the slot
    // impls' returned values + the bytes they produce, never the dev log), so a
    // diagnostic LOG_DEBUG_KV short-circuits here.
    return false;
}

void EmitEngine(Level /*level*/, const char* /*category*/, const char* /*message*/) {
    // No-op: the engine log destination does not exist in the standalone plugin.
}

void EmitEngineKV(Level /*level*/, const char* /*category*/, const char* /*action*/,
                  std::initializer_list<KV> /*kvs*/) {
    // No-op (LOG_DEBUG_KV is gated off above; LOG_ERROR_KV/LOG_WARN_KV reach here
    // and are discarded — the test reads the slot outputs + bytes, not the log).
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

// The KV constructors + factory the compiled TUs' LOG_*_KV call sites use. The
// engine's full KV set lives in log.cpp; the standalone build needs only these.
KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.c_str()), svn(val.size()) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}
// The open/read/seek slot impls log signed offset/origin/errno/cap details as
// `long long` (file_handle.cpp Seek/Tell, open_slots.cpp errno/cap) — the one
// overload the cap-112 stub did not need (the index/reader logged only unsigned
// widths + strings).
KV::KV(const char* key, long long val)
    : k(key), kind(INT), i(val) {}

KV KV::BareStr(const char* key, const char* val) {
    KV kv(key, val);   // delegate to the (const char*, const char*) ctor...
    kv.kind = BARE_STR;  // ...then re-tag as the unquoted identifier form.
    return kv;
}

// The (const char*, const char*) ctor BareStr delegates to (the const char*
// overload the TUs do not otherwise reference, but BareStr needs).
KV::KV(const char* key, const char* val)
    : k(key), kind(STR), sv(val), svn(val ? std::char_traits<char>::length(val) : 0) {}

}  // namespace kcdx::log
