#pragma once

// crash_guard — uniform SEH wrapper for every foreign-code entry into
// kcdx, plus a process-level unhandled-exception backstop.
//
// Why centralize: the "any crash, no matter what, gets logged" promise
// only holds if every callback invocation point is wrapped identically.
// Doing it inline per call site would (a) require sprinkling __try /
// __except everywhere — which conflicts with C++ unwinding in those
// translation units — and (b) drift in formatting over time.
//
// All call sites do:
//
//   guard::Call("site-name", "plugin-name", []() { cb(&msg); });
//
// On entry: a structured log line "GUARD enter site=... plugin=...".
// On clean return: "GUARD exit site=... plugin=...".
// On SEH fault: "GUARD FAULTED site=... plugin=... code=0x... rip=0x...".
// The fault is then swallowed and `false` is returned — callers can
// decide whether to abort the broadcast loop or continue. Fault
// information is also recorded for the process-level filter so it can
// correlate "which guarded site held the fault."
//
// Two compile-time invariants must hold for this to work:
//   1. crash_guard.cpp is compiled with /EHa (async exceptions
//      enabled), set per-file in CMakeLists.txt. The rest of kcdx
//      stays on /EHs.
//   2. Callers pass a function-pointer + void* userdata. Lambdas that
//      capture by value go via a small adapter that takes a pointer
//      to a closure object on the caller's stack. This keeps the SEH
//      frame inside crash_guard.cpp and avoids the
//      "C2712: __try in a function with object unwinding" error.

#include <cstdint>

namespace kcdx::guard {

// Function-pointer signature for the guarded body. The void* is opaque
// userdata supplied by the caller; the body casts it back to its real
// type.
using GuardedFn = void (*)(void* userdata);

// Run `fn(userdata)` inside an SEH guard. Logs structured enter/exit/
// FAULTED lines. Returns true on clean exit, false on fault.
//
// `site` should be a stable identifier ("messaging.broadcast",
// "console.cmd", "plugin.load", etc.). `pluginName` should be the
// plugin's stable name when known, or nullptr for engine-internal
// guards.
bool Call(const char* site,
          const char* pluginName,
          GuardedFn   fn,
          void*       userdata);

// Lightweight breadcrumb — record the current site for the calling
// thread so the unhandled-exception filter can attribute an
// unguarded fault. Use this where wrapping the whole function in
// guard::Call would be too invasive (hot-path JIT-detour callees,
// large stateful functions with C++ destructors) but where you
// still want the crash log to name a site.
//
// Returns a token that the caller passes to ClearBreadcrumb when
// the site is exited. Stack-allocate a BreadcrumbScope for RAII.
struct Breadcrumb {
    const char* prevSite   = nullptr;
    const char* prevPlugin = nullptr;
};

Breadcrumb SetBreadcrumb(const char* site, const char* pluginName);
void       ClearBreadcrumb(const Breadcrumb& prev);

// RAII helper. Constructor sets, destructor restores.
struct BreadcrumbScope {
    Breadcrumb prev;
    BreadcrumbScope(const char* site, const char* pluginName)
        : prev(SetBreadcrumb(site, pluginName)) {}
    ~BreadcrumbScope() { ClearBreadcrumb(prev); }

    BreadcrumbScope(const BreadcrumbScope&) = delete;
    BreadcrumbScope& operator=(const BreadcrumbScope&) = delete;
};

// Install the process-level UnhandledExceptionFilter. Anything that
// escapes a guarded call (or runs in code that isn't guarded at all)
// hits this and produces one final structured log line before BugSplat
// takes over. Safe to call multiple times; only the first call
// actually installs.
//
// The previous filter is saved and chained — BugSplat's own filter
// (installed by KCD2 at startup) keeps its job of shipping the dump.
void InstallUnhandledExceptionFilter();

}  // namespace kcdx::guard
