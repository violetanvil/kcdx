#include "early_hook_selftest.h"

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf

#include "dev.h"
#include "early_hook.h"
#include "log.h"
#include "test.h"

// cap-80 self-test — see early_hook_selftest.h for why this lives in engine
// code (early_hook::Install is engine-internal, not a plugin export). The test
// drives the GENERALIZED install on a dedicated exported target in kcdx's own
// (always-mapped) DLL, then calls the export and asserts the detour fired — a
// falsifiable claim: it FAILS if Install returns false, if the detour never
// fired, or if pass-through did not reach the original.

namespace {

constexpr const char* kRow      = "cap-80-early-hook";
constexpr const char* kCategory = "EARLY_HOOK";

// The detour-fired flag + the original (trampoline) the detour calls through.
std::atomic<bool> g_detourFired{false};
void*             g_origTarget = nullptr;

// Sentinels the test asserts pass-through preserved: the target returns a known
// value; the detour passes the call through unchanged, so the call still
// returns the sentinel — proving the trampoline reached the real body.
constexpr int kTargetSentinel = 0x6c80;  // 'l' << 8 | 0x80, an arbitrary marker

}  // namespace

// The hook TARGET — a dedicated exported no-op the self-test owns. Exported so
// early_hook::Install can resolve it via GetProcAddress (a non-exported static
// is unreachable by name). This is a TEST symbol on the engine DLL, NOT a
// plugin-facing interface — it adds nothing to include/kcdx/Interfaces.h and
// no plugin links it. noinline so the call site is a real call MinHook can
// detour (an inlined body would have no entry point to hook).
extern "C" __declspec(dllexport) __declspec(noinline)
int kcdx_cap80_early_hook_target(int x) {
    // volatile so the optimizer cannot fold this away; returns the sentinel
    // XOR'd with the arg so the test can vary the observable.
    volatile int v = x;
    return v ^ kTargetSentinel;
}

namespace {

// The detour: records the fire, then passes through to the original unchanged
// (the same log-only, mutate-nothing shape the BugSplat consumer uses). ABI
// MUST match the target: int __fastcall(int).
using TargetFn = int(__fastcall*)(int);

int __fastcall Cap80_Detour(int x) {
    g_detourFired.store(true, std::memory_order_release);
    auto orig = reinterpret_cast<TargetFn>(g_origTarget);
    if (!orig) return 0;  // should never happen post-Install; the assert catches it
    return orig(x);
}

}  // namespace

namespace kcdx::early_hook_selftest {

void RunSelfTestOnce() {
    // Dev-gated: the test installs a real MinHook detour, so it does not run in
    // production (where the suite reports nothing anyway). Unlike a pure-CPU
    // self-test (cap-66's malloc/free), an install touches the process hook
    // table — keep it dev-only. Latch only AFTER the gate so a production boot
    // that later enables dev mode still runs it once.
    if (!kcdx::dev::IsEnabled()) return;

    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    char reason[512];

    // Resolve kcdx's OWN module base name at runtime (robust to a rename of the
    // engine DLL) — the test's own function address identifies the module.
    HMODULE selfMod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&kcdx_cap80_early_hook_target),
            &selfMod) || !selfMod) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: self-test setup — could not resolve kcdx's own module handle "
            "(GetModuleHandleExW gle=%lu). A null here is a harness fault, not "
            "the primitive.", GetLastError());
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-80 early-hook");
        return;
    }
    wchar_t modPathW[MAX_PATH] = {0};
    DWORD pn = GetModuleFileNameW(selfMod, modPathW, MAX_PATH);
    if (pn == 0 || pn >= MAX_PATH) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: self-test setup — GetModuleFileNameW failed (gle=%lu). "
            "Harness fault, not the primitive.", GetLastError());
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-80 early-hook");
        return;
    }
    // Base name = the slice after the last path separator.
    const wchar_t* baseName = modPathW;
    for (const wchar_t* p = modPathW; *p; ++p) {
        if (*p == L'\\' || *p == L'/') baseName = p + 1;
    }

    // Build the author-parameterized request — the exact shape any consumer
    // passes: module name + exported symbol name + signature + detour +
    // trampoline slot. This is the generalized path under test, not the baked
    // BugSplat target.
    kcdx::early_hook::InstallRequest req = {};
    req.module       = baseName;
    req.exportName   = "kcdx_cap80_early_hook_target";
    req.detour       = reinterpret_cast<void*>(&Cap80_Detour);
    req.trampoline   = &g_origTarget;
    req.signature    = "int __fastcall(int)";
    req.inventoryTag = "cap80_early_hook_target";

    bool installed = kcdx::early_hook::Install(req);
    if (!installed) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: early_hook::Install returned false for an already-mapped "
            "module (%ls) + exported target — the author-parameterized install "
            "did not resolve the module/export or MinHook rejected it. See the "
            "EARLY_HOOK engine log for the specific reason.", baseName);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("cap-80 early-hook");
        return;
    }

    // Call the target. Post-install the detour MUST run first (recording the
    // fire) and pass through to the original, so the return is the original's
    // sentinel-XOR — proving both that the detour fired AND that pass-through
    // reached the real body.
    constexpr int kArg = 0x1234;
    int ret = kcdx_cap80_early_hook_target(kArg);
    bool fired      = g_detourFired.load(std::memory_order_acquire);
    bool passThru   = (ret == (kArg ^ kTargetSentinel));

    if (fired && passThru) {
        std::snprintf(reason, sizeof(reason),
            "PASS — early_hook::Install(module=\"%ls\", export=\"%s\", "
            "sig=\"int __fastcall(int)\") on an already-mapped module installed "
            "AND the detour fired on the next call, passing through to the "
            "original (ret=0x%x == arg^sentinel). The AUTHOR-PARAMETERIZED "
            "install works, not just the one baked target.",
            baseName, req.exportName, (unsigned)ret);
        LOG_INFO_KV(kCategory, "selftest_pass",
            ::kcdx::log::KV("module", "kcdx-own"),
            ::kcdx::log::KV("fired", fired ? 1 : 0),
            ::kcdx::log::KV("passthru", passThru ? 1 : 0));
        kcdx::test::ReportResult(kRow, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: early-hook install reported success but the detour did NOT "
            "behave — fired=%d (want 1: the detour callback ran on the post-"
            "install call), passthru=%d ret=0x%x want=0x%x (want 1: the "
            "trampoline reached the original body). fired==0 means the install "
            "did not actually arm the MinHook; passthru==0 means the trampoline "
            "is wrong.",
            fired ? 1 : 0, passThru ? 1 : 0, (unsigned)ret,
            (unsigned)(kArg ^ kTargetSentinel));
        LOG_ERROR_KV(kCategory, "selftest_fail",
            ::kcdx::log::KV("fired", fired ? 1 : 0),
            ::kcdx::log::KV("passthru", passThru ? 1 : 0),
            ::kcdx::log::KV("ret", (unsigned long long)(unsigned)ret));
        kcdx::test::ReportResult(kRow, false, reason);
    }
    kcdx::test::EmitSummaryIfChanged("cap-80 early-hook");
}

}  // namespace kcdx::early_hook_selftest
