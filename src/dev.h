// dev — kcdx dev mode (a diagnostic logging feature for plugin authors).
//
// Enable per kcdx/docs/dev-mode.md. When ON, every interesting kcdx
// action emits a structured line to kcdx-dev.log. When OFF (the
// default), each KCDX_DEV(...) call site costs one relaxed atomic
// load (~sub-ns). Off-state has zero allocation, zero format work,
// no log file open.
//
// Usage at a call site:
//
//   KCDX_DEV("PATCH", "RESOLVE",
//       kcdx::dev::KV("name",            entry.name),
//       kcdx::dev::KV("pattern_matches", hits),
//       kcdx::dev::KV("addr",            resolved_va));
//
// Produces:
//
//   [01:23:45.678 T:54276] PATCH.RESOLVE name="outfit_swap_in_combat" pattern_matches=1 addr=0x7FFC9D9E1759
//
// Categories + actions live in docs/dev-mode.md. There is intentionally
// no enum for them — plugin authors greppin the log are looking for
// strings, and adding a new category is a one-string change at the
// call site.

#pragma once

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>

namespace kcdx::dev {

// ---------------------------------------------------------------------
// Lifecycle
// ---------------------------------------------------------------------

// Called by config.cpp once it knows whether ANY plugin requested
// dev mode. After this returns, IsEnabled() is stable for the session
// (no plugin can disable mid-session; this matches the v0.1 simple model).
//
// First call to SetEnabled(true) opens kcdx-dev.log in the plugins
// directory. Subsequent SetEnabled calls are no-ops.
void SetEnabled(bool on);

// Apply caps before SetEnabled. Either may be skipped (defaults: 50 MB,
// 20 files). max_files = 0 means "never delete rotated files."
void SetCapBytes (size_t cap_bytes);
void SetMaxFiles (int    max_files);

// Hot path: branch predictor takes not-taken when dev mode is off.
inline bool IsEnabled() {
    extern std::atomic<bool> g_enabled;
    return g_enabled.load(std::memory_order_relaxed);
}

// ---------------------------------------------------------------------
// KV: a name + typed value, emitted as `name=val` in the trace.
//
// Overloaded ctors keep call sites short for common types. We do NOT
// support arbitrary T — only the types kcdx actually wants to log. If
// you need a new type at a call site, add an overload here.
// ---------------------------------------------------------------------

struct KV {
    const char* k;

    // Tag for which constructor was called; format() switches on this.
    enum Kind {
        STR,       // const char* / std::string / std::string_view (quoted)
        BARE_STR,  // const char* (NOT quoted; for identifier-like values)
        INT,       // signed long long, formatted decimal
        UINT,      // unsigned long long, formatted decimal
        HEX,       // uintptr_t, formatted as 0x...
        BOOL,      // true/false
        DOUBLE,    // double, formatted %.17g
        BYTES,     // const uint8_t* + size, formatted as `XX XX XX`
    };

    Kind kind = STR;

    // Tagged-union storage. The unused members are fine: the format()
    // dispatcher only touches the field matching `kind`.
    const char*    sv  = nullptr;
    size_t         svn = 0;
    long long      i   = 0;
    unsigned long long u = 0;
    uintptr_t      hex = 0;
    bool           b   = false;
    double         d   = 0.0;
    const uint8_t* bp  = nullptr;
    size_t         bn  = 0;

    // --- ctors per type ---
    KV(const char* key, const char* val);
    KV(const char* key, const std::string& val);
    KV(const char* key, std::string_view val);
    KV(const char* key, int val);
    KV(const char* key, long val);
    KV(const char* key, long long val);
    KV(const char* key, unsigned int val);
    KV(const char* key, unsigned long val);
    KV(const char* key, unsigned long long val);
    KV(const char* key, bool val);
    KV(const char* key, double val);
    KV(const char* key, float val);

    // void* / const void* — formatted as hex address.
    KV(const char* key, const void* val);
    KV(const char* key, void* val);

    // Byte buffer — formatted as space-separated hex pairs (e.g. "48 8B 01").
    static KV Bytes(const char* key, const uint8_t* data, size_t size);

    // BareStr — an unquoted string (for enum-like identifiers).
    // Use sparingly; quoted is safer for arbitrary content.
    static KV BareStr(const char* key, const char* val);
};

// ---------------------------------------------------------------------
// Emit — the cold path. Only called when IsEnabled() returned true.
//
// Building the formatted line is the bulk of the work; the file write
// is amortized via a per-call mutex (no async queue in v0.1 — keep
// implementation simple, accept synchronous I/O cost since dev mode
// authors accept the overhead).
// ---------------------------------------------------------------------

void Emit(const char* category, const char* action,
          std::initializer_list<KV> kvs);

}  // namespace kcdx::dev

// ---------------------------------------------------------------------
// Macro
// ---------------------------------------------------------------------

// Use at call sites. Variadic so call sites pass any number of KV's.
//
//   KCDX_DEV("PATCH", "RESOLVE",
//       kcdx::dev::KV("name", entry.name),
//       kcdx::dev::KV("addr", resolved_addr));
//
// Compiles to:
//
//   do { if (IsEnabled()) Emit("PATCH", "RESOLVE", { KV(...), KV(...) }); } while (0)
//
// The initializer_list materialization only happens inside the
// IsEnabled() branch — the not-taken path doesn't construct the KVs.
#define KCDX_DEV(category, action, ...) \
    do { \
        if (::kcdx::dev::IsEnabled()) \
            ::kcdx::dev::Emit((category), (action), { __VA_ARGS__ }); \
    } while (0)

// Convenience: KCDX_DEV with zero KVs (action-only events like
// boundary markers).
#define KCDX_DEV0(category, action) \
    do { \
        if (::kcdx::dev::IsEnabled()) \
            ::kcdx::dev::Emit((category), (action), {}); \
    } while (0)
