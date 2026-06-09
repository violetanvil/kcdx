#include "foreign_hook_chain_selftest.h"

#include <atomic>
#include <cstdint>
#include <cstdio>   // snprintf
#include <cstring>  // memset/strlen

#include <safetyhook/inline_hook.hpp>

#include "dev.h"
#include "log.h"
#include "test.h"

// comp-19 — see foreign_hook_chain_selftest.h for the claim + why this lives in
// engine code (foreign-hook chaining is engine-internal — it falls out of the
// SafetyhookBackend install path, no kcdx.* author surface — so it self-reports
// from engine code via kcdx::test::ReportResult, like comp-18 the classifier).

namespace {

constexpr const char* kRow      = "comp-19-foreign-chaining";
constexpr const char* kCategory = "FOREIGN_HOOK";

// ABI of the stub + both detours: int __fastcall(int). safetyhook relocates the
// prologue + preserves the original ABI through the trampoline, so all three
// share one signature.
using TargetFn = int(__fastcall*)(int);

// The order-log: each detour appends its marker as it fires, in call order. The
// assertion reads this string. main-thread, single in-process call — a plain
// buffer + index is sufficient.
char g_order[8] = {0};
int  g_orderLen = 0;
void AppendOrder(char c) {
    if (g_orderLen < static_cast<int>(sizeof(g_order)) - 1) {
        g_order[g_orderLen++] = c;
        g_order[g_orderLen]   = '\0';
    }
}

// The call-original trampolines: the FOREIGN detour calls through to the real
// stub via g_foreignOrig (its own trampoline -> real stub); the KCDX detour
// calls through via g_kcdxOrig, which is kcdx's trampoline -> the RELOCATED
// foreign E9 -> the foreign detour -> the real stub. g_kcdxOrig being the
// relocated foreign jump IS the chaining (§6.2): kcdx's call-original runs the
// foreign mod's hook.
void* g_foreignOrig = nullptr;
void* g_kcdxOrig    = nullptr;

// The sentinel the real stub returns (XOR'd with the arg so the observable
// varies). The assertion checks the final return carries it through — proving
// the chain reached the real function past both detours.
constexpr int kStubSentinel = 0x6c13;  // 'l' << 8 | 0x13 (comp-19), an arbitrary marker

}  // namespace

// The hook TARGET — a kcdx-controlled stub, the "real original" (AP1: a stub the
// fixture owns, NOT a game VA). noinline + a volatile body so the optimizer
// cannot fold it away and there is a real entry point both detours can hook
// (an inlined body has no prologue to patch).
extern "C" __declspec(noinline)
int __fastcall kcdx_comp19_chain_target(int x) {
    AppendOrder('O');  // the real original ran (last in the chain)
    volatile int v = x;
    return v ^ kStubSentinel;
}

namespace {

// The synthetic FOREIGN detour — stands in for ANOTHER mod's hook (the fixture
// owns it so the test controls both sides). Installed FIRST, so its E9 sits in
// the stub's prologue; when kcdx installs over it, safetyhook relocates THIS
// detour's foreign E9 into kcdx's trampoline. Appends 'F', then calls through
// its own trampoline to the real stub.
int __fastcall Comp19_ForeignDetour(int x) {
    AppendOrder('F');
    auto orig = reinterpret_cast<TargetFn>(g_foreignOrig);
    return orig ? orig(x) : 0;
}

// kcdx's detour — installed SECOND (over the foreign hook), so kcdx runs FIRST in
// the chain (§6.3 kcdx-first load order). Appends 'K', then calls through kcdx's
// call-original (g_kcdxOrig), which IS the relocated foreign jump -> the foreign
// detour -> the real stub. So the chain is game -> kcdx -> foreign -> original.
int __fastcall Comp19_KcdxDetour(int x) {
    AppendOrder('K');
    auto orig = reinterpret_cast<TargetFn>(g_kcdxOrig);
    return orig ? orig(x) : 0;
}

}  // namespace

namespace kcdx::foreign_hook_chain_selftest {

void RunSelfTestOnce() {
    // Dev-gated: installs real safetyhook detours, so it never runs in
    // production. Latch only AFTER the gate so a boot that later enables dev mode
    // still runs it once.
    if (!kcdx::dev::IsEnabled()) return;

    static std::atomic<bool> done{false};
    bool expected = false;
    if (!done.compare_exchange_strong(expected, true,
                                      std::memory_order_acq_rel)) {
        return;
    }

    char reason[768];

    void* target = reinterpret_cast<void*>(&kcdx_comp19_chain_target);

    // --- Step 1: install the SYNTHETIC FOREIGN hook FIRST -------------------
    // A second safetyhook InlineHook standing in for another mod's hook. It
    // writes a standard 5-byte E9 rel32 into the stub's prologue (or an FF25 far
    // fallback) — exactly the foreign prologue jump kcdx will chain onto. This is
    // NOT registered in kcdx_trampoline_registry: it stands in for a FOREIGN
    // trampoline (registering it would make a classifier read it as kcdx-owned).
    auto foreign = safetyhook::InlineHook::create(
        target, reinterpret_cast<void*>(&Comp19_ForeignDetour));
    if (!foreign) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: comp-19 setup — the synthetic FOREIGN InlineHook::create "
            "failed (err type=%d). Harness fault (could not stand up the "
            "foreign-hook stand-in), not the chaining mechanism.",
            (int)foreign.error().type);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("comp-19 foreign-chaining");
        return;
    }
    safetyhook::InlineHook foreignHook = std::move(*foreign);
    if (auto en = foreignHook.enable(); !en) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: comp-19 setup — the synthetic FOREIGN hook enable() failed "
            "(err type=%d). Harness fault, not the chaining mechanism.",
            (int)en.error().type);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("comp-19 foreign-chaining");
        return;
    }
    // The foreign detour's call-original (its trampoline -> the real stub).
    g_foreignOrig = foreignHook.original<void*>();

    // --- Step 2: kcdx installs ITS hook over the foreign one ----------------
    // This is the SAME install path a Foreign-classified target takes in
    // production (SafetyhookBackend == InlineHook::create + enable). The stub's
    // prologue currently carries the foreign E9; InlineHook::create RELOCATES
    // that foreign jump into kcdx's trampoline IP-fixed (e9_hook, §6.2), so
    // g_kcdxOrig (kcdx's call-original) runs the foreign detour, which runs the
    // real stub. kcdx does NOT follow the jump by hand — the relocation captures
    // it. If the foreign jump were an unrelocatable shape, create would return
    // UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE here and the install would fail loud
    // (the §6.3 / AP14 surface-don't-mishandle path — a standard E9 IS
    // relocatable, so this fixture exercises the chaining success path).
    auto kcdxh = safetyhook::InlineHook::create(
        target, reinterpret_cast<void*>(&Comp19_KcdxDetour));
    if (!kcdxh) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: kcdx's InlineHook::create over the foreign hook failed (err "
            "type=%d). If type=4 (UNSUPPORTED_INSTRUCTION_IN_TRAMPOLINE) the "
            "foreign jump was not relocatable — but a standard E9 IS relocatable, "
            "so this is a chaining-mechanism regression: safetyhook did not "
            "relocate the foreign prologue jump into kcdx's trampoline.",
            (int)kcdxh.error().type);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("comp-19 foreign-chaining");
        return;
    }
    safetyhook::InlineHook kcdxHook = std::move(*kcdxh);
    if (auto en = kcdxHook.enable(); !en) {
        std::snprintf(reason, sizeof(reason),
            "FAIL: kcdx's hook enable() over the foreign hook failed (err "
            "type=%d) — the chaining install did not arm.", (int)en.error().type);
        kcdx::test::ReportResult(kRow, false, reason);
        kcdx::test::EmitSummaryIfChanged("comp-19 foreign-chaining");
        return;
    }
    // kcdx's call-original — kcdx's trampoline, which begins with the RELOCATED
    // foreign E9. Calling it runs the foreign detour -> the real stub. THIS is the
    // chaining: kcdx's "original" is the foreign mod's hook.
    g_kcdxOrig = kcdxHook.original<void*>();

    // --- Step 3: call the target; assert BOTH fired, in order ---------------
    g_orderLen = 0;
    g_order[0] = '\0';
    constexpr int kArg = 0x55;
    int ret = kcdx_comp19_chain_target(kArg);

    // Expect the chain game -> kcdx -> foreign -> original: 'K' then 'F' then 'O'.
    const bool orderOk    = (std::strcmp(g_order, "KFO") == 0);
    const bool passThru   = (ret == (kArg ^ kStubSentinel));

    if (orderOk && passThru) {
        std::snprintf(reason, sizeof(reason),
            "PASS — foreign-hook chaining: a synthetic FOREIGN E9 was installed "
            "on a kcdx-controlled stub, then kcdx hooked the SAME stub; BOTH "
            "detours fired in order game->kcdx->foreign->original (order-log "
            "\"%s\"==\"KFO\") and the call reached the real stub through the "
            "relocated foreign jump (ret=0x%x == arg^sentinel). safetyhook "
            "relocated the foreign prologue jump into kcdx's trampoline, so "
            "kcdx's call-original ran the foreign mod's hook — no hand-rolled "
            "jump-follower. Both mods' hooks coexist (§6.2).",
            g_order, (unsigned)ret);
        LOG_INFO_KV(kCategory, "chain_selftest_pass",
            ::kcdx::log::KV("order", g_order),
            ::kcdx::log::KV("passthru", passThru ? 1 : 0));
        kcdx::test::ReportResult(kRow, true, reason);
    } else {
        std::snprintf(reason, sizeof(reason),
            "FAIL: foreign-hook chaining mis-behaved — order-log=\"%s\" (want "
            "\"KFO\": kcdx fires first, delegates to the foreign detour via its "
            "relocated call-original, then the real stub runs). A missing 'K' = "
            "kcdx's detour never fired; a missing 'F' = kcdx's call-original did "
            "NOT run the foreign hook (chaining broke — safetyhook did not "
            "relocate the foreign jump); a missing 'O' = the real stub never ran; "
            "\"FK\"/wrong order = load order inverted. passthru=%d ret=0x%x "
            "want=0x%x (the chain reached the real stub iff passthru=1).",
            g_order, passThru ? 1 : 0, (unsigned)ret,
            (unsigned)(kArg ^ kStubSentinel));
        LOG_ERROR_KV(kCategory, "chain_selftest_fail",
            ::kcdx::log::KV("order", g_order),
            ::kcdx::log::KV("passthru", passThru ? 1 : 0),
            ::kcdx::log::KV("ret", (unsigned long long)(unsigned)ret));
        kcdx::test::ReportResult(kRow, false, reason);
    }
    kcdx::test::EmitSummaryIfChanged("comp-19 foreign-chaining");

    // The two InlineHooks (foreignHook, kcdxHook) go out of scope here, which
    // would reset() and restore the prologue — but the chain has already fired +
    // reported, so the live hooks are no longer needed (the assertion is done).
    // Leaking them (never unhooking) would also be fine (SKSE "no teardown"); the
    // RAII reset on scope-exit is harmless because nothing calls the stub again
    // this session.
}

}  // namespace kcdx::foreign_hook_chain_selftest
