// CAP-04 — mid-hook (kcdx.hook mode=mid) on kcdx.code-ALLOCATED memory.
//
// Companion DLL for the mid-on-code composition. The NOVEL axis vs cap-21:
// cap-21's stub is allocated via the C++ raw AllocateFromBranchPool floor and
// its address handed to Lua as a lightuserdata; here the stub is allocated by
// the AUTHOR-FACING Lua kcdx.code verb (plugin.lua) and the mid hook targets
// `region:add(3)` (a kcdx.code pointer userdata). cap-30/cap-40 allocate via
// kcdx.code but never hook the result. Composing the two author surfaces —
// kcdx.code allocate + kcdx.hook mid on the allocation — is the row neither
// covers.
//
// Division of labor (and WHY): plugin.lua OWNS the hook because skip is
// Lua-only — the C++ kcdxHookInterface::Mid callback (void ABI) has no
// return-skip primitive (src/hook_chain.cpp MidCDispatch: "C mid does not yet
// expose a return-skip primitive"); only the Lua mid callback's `return "skip"`
// reaches the skip-original codegen. So the hook MUST install from Lua. Lua
// cannot CALL the allocated region with a seed arg (kcdx.memory.pointer has no
// call method — src/lua_bind_pointer.cpp kMethods), so this companion does the
// call. The two halves are bridged by the kcdx.code export -> ResolveSymbolAs
// handshake: plugin.lua publishes each stub's base via export="stub_<sub>";
// this DLL resolves it via ResolveSymbolAs(self, "stub_<sub>").
//
// The 9-byte stub (allocated in plugin.lua, verified-safe self-contained):
//   +0:  48 89 C8        mov rax, rcx     ; rax = seed (arg in rcx, win64)
//   +3:  48 83 C0 64     add rax, 0x64    ; rax += 100   <-- MID HOOK at +3
//   +7:  90              nop
//   +8:  C3              ret
//
// The stub is `int fn(int seed)` (seed in ECX/RCX under the MS x64 ABI; the
// stub's `mov rax,rcx` consumes it, returns in EAX). Calling with seed=10:
//   CAP-04-mid-on-code-run   callback returns nothing -> add runs -> 110
//   CAP-04-mid-on-code-skip  callback returns "skip"  -> add skipped -> 10
//
// Test mode: boot-only. Both rows self-verify at kcdxMessage_InputLoaded
// (AFTER ApplyZone installs the mid detours). A guard reports FAIL if the
// export never resolved (kcdx.code alloc or mid install failed in Lua), so no
// row sits silent-PENDING.

#include <windows.h>

#include <cstdint>
#include <cstdio>

#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_04_midhook";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;
kcdxLogger           gLog;
bool                 g_reported = false;

// The stub is int fn(int seed) -> seed + 100 (mod the mid hook's run/skip).
using StubFn = int (*)(int);

void Check(const char* sub, bool pass, const char* reason) {
    if (pass) {
        gLog.Info("VERIFY", "PASS %s: %s", sub, reason);
        g_api->ReportTestResult(g_self, sub, 1, reason);
    } else {
        gLog.Error("VERIFY", "FAIL %s: %s", sub, reason);
        g_api->ReportTestResult(g_self, sub, 0, reason);
    }
}

// Resolve the kcdx.code export plugin.lua published, call the hooked stub with
// seed=10, and assert the result. ResolveSymbolAs threads g_self so the bare
// "stub_*" resolves on the SELF tier to our own plugin's <author>.<plugin>.stub_*
// export (a bare ResolveSymbol with no owner would miss it — cap-40 doc).
void ResolveCallAndCheck(const char* sym, const char* sub, int expected) {
    uintptr_t va = g_api->ResolveSymbolAs(g_self, sym);
    char reason[320];
    if (!va) {
        snprintf(reason, sizeof(reason),
            "ResolveSymbolAs(self, '%s') returned 0 — the kcdx.code stub was "
            "not allocated/exported (or the mid hook failed to install) in "
            "plugin.lua; mid-on-code not proven", sym);
        Check(sub, false, reason);
        return;
    }
    StubFn fn = reinterpret_cast<StubFn>(va);
    int result = fn(10);
    bool pass = (result == expected);
    snprintf(reason, sizeof(reason),
        "%s — called kcdx.code stub '%s' (resolved via export) with seed=10, "
        "got %d (expected %d); kcdx.hook mode=mid at +3 took effect on the "
        "kcdx.code-allocated region",
        pass ? "mid-on-code ok" : "mid-on-code WRONG",
        sym, result, expected);
    Check(sub, pass, reason);
}

void OnMessage(kcdxMessage* msg) {
    if (msg->messageType != kcdxMessage_InputLoaded) return;
    if (g_reported) return;
    g_reported = true;

    gLog.Info("VERIFY",
        "InputLoaded — calling the kcdx.code stubs the Lua mid hooks target");

    // run: callback returns nothing -> the captured `add rax,0x64` runs -> 110.
    ResolveCallAndCheck("stub_run", "CAP-04-mid-on-code-run", 110);
    // skip: callback returns "skip" -> the captured `add` never runs -> 10.
    ResolveCallAndCheck("stub_skip", "CAP-04-mid-on-code-skip", 10);
}
}  // namespace

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    gLog   = kcdxLogger(api, g_self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        gLog.Error("INIT", "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-04-mid-on-code-run", 0,
            "QueryInterface(Messaging) returned null");
        api->ReportTestResult(g_self, "CAP-04-mid-on-code-skip", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    gLog.Info("INIT",
        "listener registered; verify runs on InputLoaded after ApplyZone "
        "installs the Lua mid detours on the kcdx.code stubs");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
