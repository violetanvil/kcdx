// CAP-04 — [[mid_hook]] call_original semantics self-test.
//
// Tests the THREE call_original modes added by Phase 5g closeout:
//
//   true   (default)   original instruction runs after the callback
//   false              original instruction NEVER runs (compile-time)
//   "auto"             callback decides via args._skip = true
//
// For v0.1, this test does NOT exercise register MUTATION inside the
// Lua callback — kcdxLuaApi lacks Call/Pcall (see design-gaps.md
// item #11), so the callback cannot invoke args[1]:set(...). That's
// a separate v0.1.0 follow-up. CAP-04 verifies the harder problem:
// skip-original codegen works correctly.
//
// Each sub-test uses its own [[trampoline]] target with body:
//
//   +0:  48 89 C8        mov rax, rcx           ; rax = seed
//   +3:  48 83 C0 64     add rax, 0x64          ; HOOK HERE, adds 100
//   +7:  90              nop                    ; consumed by MinHook patch
//   +8:  C3              ret
//
// Invoking with seed=10:
//   CAP-04a  call_original = true   → 110 (original runs, rax += 100)
//   CAP-04b  call_original = false  →  10 (original skipped, rax unchanged)
//   CAP-04c  call_original = "auto", _skip = true → 10 (skipped)
//   CAP-04d  call_original = "auto", no _skip      → 110 (runs)

#include <windows.h>

#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "kcdx.cap-04-midhook";

const kcdxInterface*           g_api    = nullptr;
const kcdxScriptingInterface*  g_script = nullptr;
const kcdxLuaApi*              g_lua    = nullptr;
kcdxPluginHandle               g_self   = kcdxInvalidPluginHandle;
kcdxLogger                     gLog;
bool g_reported_a = false, g_reported_b = false;
bool g_reported_c = false, g_reported_d = false;

// -----------------------------------------------------------------
// Lua callbacks: receive the captures table at stack[1]. Each
// callback returns 1 result (the dispatcher reads it as either a
// resume-address override OR nil/0). We return nil for "no override"
// in all cases; the call_original=false path is handled by
// codegen-time decision (no Lua signaling required).
// -----------------------------------------------------------------

int OnA_callback(lua_State* L, void* /*ud*/) {
    // call_original = true, no mutation. Original runs.
    g_lua->PushNil(L);
    return 1;
}

int OnB_callback(lua_State* L, void* /*ud*/) {
    // call_original = false. Codegen skips the original regardless
    // of what Lua does. Lua doesn't need to do anything.
    g_lua->PushNil(L);
    return 1;
}

int OnC_callback(lua_State* L, void* /*ud*/) {
    // call_original = "auto" with _skip. Set args._skip = true on
    // the captures table to signal the dispatcher.
    if (g_lua->IsTable(L, 1)) {
        g_lua->PushBoolean(L, 1);
        g_lua->SetField(L, 1, "_skip");
    }
    g_lua->PushNil(L);
    return 1;
}

int OnD_callback(lua_State* L, void* /*ud*/) {
    // call_original = "auto", no _skip. Original runs.
    g_lua->PushNil(L);
    return 1;
}

// -----------------------------------------------------------------
// kInputLoaded handler: resolve targets, invoke, verify, report.
// -----------------------------------------------------------------

using Cap04TargetFn = int(__fastcall*)(int seed);

void InvokeAndReport(const char* sym, const char* sub_test_name,
                     int seed, int expected) {
    // Resolve via the SELF tier: under the <pluginname>.<name> namespace
    // model our own [[trampoline]] export "target_a" is stored as
    // "<thisplugin>.target_a", so a bare ResolveSymbol("target_a") with no
    // owner would miss it. ResolveSymbolAs threads our own handle so the bare
    // name resolves to our own export (naming-namespaces.md).
    uintptr_t va = g_api->ResolveSymbolAs(g_self, sym);
    char reason[256];
    if (!va) {
        snprintf(reason, sizeof(reason),
            "ResolveSymbolAs(self, '%s') returned 0 — trampoline didn't "
            "register its export", sym);
        gLog.Error("VERIFY", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, sub_test_name, 0, reason);
        return;
    }
    auto fn = reinterpret_cast<Cap04TargetFn>(va);
    int result = fn(seed);
    if (result != expected) {
        snprintf(reason, sizeof(reason),
            "target_symbol='%s' seed=%d returned %d (expected %d)",
            sym, seed, result, expected);
        gLog.Error("VERIFY", "FAIL: %s", reason);
        g_api->ReportTestResult(g_self, sub_test_name, 0, reason);
        return;
    }
    snprintf(reason, sizeof(reason),
        "target_symbol='%s' seed=%d returned %d as expected",
        sym, seed, result);
    gLog.Info("VERIFY", "PASS: %s", reason);
    g_api->ReportTestResult(g_self, sub_test_name, 1, reason);
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;

    gLog.Info("VERIFY", "InputLoaded received; invoking 4 mid_hook target symbols");

    if (!g_reported_a) {
        g_reported_a = true;
        InvokeAndReport("target_a", "CAP-04a", 10, 110);
    }
    if (!g_reported_b) {
        g_reported_b = true;
        InvokeAndReport("target_b", "CAP-04b", 10, 10);
    }
    if (!g_reported_c) {
        g_reported_c = true;
        InvokeAndReport("target_c", "CAP-04c", 10, 10);
    }
    if (!g_reported_d) {
        g_reported_d = true;
        InvokeAndReport("target_d", "CAP-04d", 10, 110);
    }
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    g_script = static_cast<const kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (!g_script || !g_script->lua) {
        gLog.Error("INIT", "QueryInterface(Scripting) returned null");
        api->ReportTestResult(g_self, "CAP-04a", 0,
            "QueryInterface(Scripting) returned null");
        return true;
    }
    g_lua = g_script->lua;

    // Register the 4 Lua callbacks. They'll show up at
    // kcdx.Cap04Test.OnA..OnD — the mid-hook TOML's lua_callback
    // field must reference them by that full path.
    g_script->RegisterFunction(g_self, "Cap04Test", "OnA", &OnA_callback, nullptr);
    g_script->RegisterFunction(g_self, "Cap04Test", "OnB", &OnB_callback, nullptr);
    g_script->RegisterFunction(g_self, "Cap04Test", "OnC", &OnC_callback, nullptr);
    g_script->RegisterFunction(g_self, "Cap04Test", "OnD", &OnD_callback, nullptr);

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-04a", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
