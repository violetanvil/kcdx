// dev — compatibility shim over the unified log API.
//
// Before unification, kcdx::dev was a separate logging subsystem aimed
// at structured developer traces (kcdx-dev.log). After unification it
// became a façade over kcdx::log; all routing, formatting, and KV
// machinery now live in log.{h,cpp}.
//
// New code should prefer LOG_DEBUG_KV / LOG_TRACE_KV from log.h
// directly. KCDX_DEV is preserved as an alias so existing call sites
// keep compiling.

#pragma once

#include "log.h"

#include <cstdint>
#include <initializer_list>
#include <string>
#include <vector>

namespace kcdx::dev {

// KV is now the same struct as kcdx::log::KV. Use it via the alias.
using KV = ::kcdx::log::KV;

// Lifecycle — thin forwards to the unified API.
inline void SetEnabled(bool on) {
    ::kcdx::log::SetDevMode(on);
}

inline void SetCategoryFilter(const std::vector<std::string>& categories) {
    ::kcdx::log::SetCategoryFilter(categories);
}

inline bool IsEnabled() {
    return ::kcdx::log::IsDevModeEnabled();
}

inline bool IsCategoryEnabled(const char* category) {
    return ::kcdx::log::IsCategoryEnabled(category);
}

// Structured emit — forwards to EmitEngineKV at DEBUG severity, which
// is the closest match to the original kcdx::dev semantic ("verbose
// developer-facing structured trace, never reaches engine log").
inline void Emit(const char* category, const char* action,
                 std::initializer_list<KV> kvs) {
    ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Debug,
                              category, action, kvs);
}

}  // namespace kcdx::dev

// KCDX_DEV stays as a macro shim — call sites continue to compile.
// Routes through the unified router; only emits when dev mode is on
// AND category passes the filter. Cost when dev mode is off: one
// atomic load + branch-predicted skip.
#define KCDX_DEV(category, action, ...) \
    LOG_DEBUG_KV((category), (action), ##__VA_ARGS__)

#define KCDX_DEV0(category, action) \
    LOG_DEBUG_KV((category), (action))
