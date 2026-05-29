// CAP-65 — engine bootstrap classifier regression row.
//
// Single row, CAP-65-classifier-bootstrapped: at kcdxMessage_InputLoaded
// (which fires on the game main thread by construction, post-bootstrap),
// asserts that kcdxInterface::IsGameMainThread() returns 1. The accessor
// returns 1 iff log::g_gameMainThreadId has been captured by
// log::SetGameMainThread, which is called from hook_chain::SetLuaState's
// first non-null L call, which is gated on HookedUpdate's first-tick latch
// crossing if (g_L != null), which is gated on the engine's chain C-Before
// callback HookedLuaPcall_Engine writing g_L on at least one lua_pcall
// fire. This is hops 1-3 of the bootstrap loop named in
// docs/known-issues/cap-59-fires...md §Reframe 2026-05-29c.
//
// Goes RED specifically if the dead-classifier chicken-and-egg returns
// (the carve-out at hook_chain.cpp DispatchPre/DispatchPost/MidDispatch
// regresses, or a future engine entry trips the gate). A "cap-59 plugin.lua
// ran" assertion is not sufficient — the plugin could run for unrelated
// reasons. This row keys on the load-bearing fact directly.

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"

namespace {

constexpr const char* kName = "cap_65_classifier_bootstrap";
constexpr const char* kRow  = "CAP-65-classifier-bootstrapped";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           g_log;

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;

    // InputLoaded fires on the game main thread post-bootstrap. If the
    // classifier bootstrapped (hops 1-3 of the bootstrap loop all ran),
    // IsGameMainThread() returns 1 here. If the chicken-and-egg returned,
    // returns 0 — the load-bearing red signal.
    if (!g_api || !g_api->IsGameMainThread) {
        g_api->ReportTestResult(g_self, kRow, 0,
            "kcdxInterface::IsGameMainThread is null — plugin DLL was built "
            "against a pre-CAP-65 engine header; rebuild the plugin DLL "
            "against an engine with the IsGameMainThread thunk appended");
        return;
    }
    const uint32_t isMain = g_api->IsGameMainThread();
    if (isMain == 1) {
        g_api->ReportTestResult(g_self, kRow, 1,
            "engine bootstrap classifier bootstrapped: "
            "kcdxInterface::IsGameMainThread()==1 at InputLoaded (the chain "
            "C-Before L-capture callback HookedLuaPcall_Engine ran on a "
            "lua_pcall fire → HookedUpdate's first-tick latch crossed "
            "if(L) → hook_chain::SetLuaState captured "
            "log::g_gameMainThreadId)");
    } else {
        char reason[640];
        std::snprintf(reason, sizeof(reason),
            "ENGINE BOOTSTRAP CLASSIFIER DEAD: "
            "kcdxInterface::IsGameMainThread()==%u at InputLoaded but "
            "InputLoaded fires on the game main thread by construction. "
            "log::g_gameMainThreadId was never captured by "
            "log::SetGameMainThread, which means hook_chain::SetLuaState was "
            "never called with a non-null L, which means HookedUpdate's "
            "first-tick latch never crossed if(L), which means "
            "hooks.cpp::g_L stayed null forever, which means the engine's "
            "chain C-Before callback HookedLuaPcall_Engine never ran on a "
            "lua_pcall fire. This is the dead-classifier chicken-and-egg in "
            "the chain dispatcher (the off-thread filter blocking the engine "
            "bootstrap callback that the off-thread filter depends on). The "
            "carve-out at hook_chain.cpp:1075 (DispatchPre) / :1209 "
            "(DispatchPost) / :1341 (MidDispatch) regressed, OR a future "
            "engine entry tripped the gate. See "
            "docs/known-issues/cap-59-fires...md § Reframe 2026-05-29c "
            "for the three-hop loop the carve-out breaks.",
            isMain);
        g_api->ReportTestResult(g_self, kRow, 0, reason);
    }
}

}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    g_log  = kcdxLogger(api, g_self);
    g_log.Info("INIT", "kcdxPlugin_Load called");

    auto* messaging = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!messaging) {
        api->ReportTestResult(g_self, kRow, 0,
            "QueryInterface(Messaging) returned null at Plugin_Load — "
            "cannot subscribe to InputLoaded; cannot assert classifier "
            "bootstrap");
        return true;
    }
    messaging->RegisterListener(g_self, nullptr, OnMessage);
    g_log.Info("INIT",
        "InputLoaded listener registered; will assert "
        "kcdxInterface::IsGameMainThread()==1 at InputLoaded");
    return true;
}
