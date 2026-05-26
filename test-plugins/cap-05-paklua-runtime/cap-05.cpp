// CAP-05 / CAP-11 companion DLL.
//
// Registers ONE trivial, genuinely-callable C function under cap-05's
// OWN namespace (kcdx.cap05.probe) via kcdxScriptingInterface::RegisterFunction
// (mirrors cap-10-scripting-interface/cap-10.cpp). The pak-Lua test
// (scripts/mods/kcdx_test_paklua.lua) then:
//   * CAP-11 — takes its address via kcdx.lua.cfunction_address
//   * CAP-05 — installs a dynamic_hook on that address and CALLS it,
//              asserting the pre_callback fired.
//
// This makes the pak test self-owned: it no longer depends on the
// archived hello-plugin sample (which is not a suite plugin and isn't
// deployed with the suite). A regression test owns its fixtures
// (test-suite.md).
//
// The function is a real registered Lua C function the pak-Lua can take
// the address of, hook, and call. Its body is a trivial counter that
// returns nothing to Lua — the test only needs the call to reach the
// detour, not a return value.

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_05_paklua_sidecar";
kcdxLogger  gLog;
volatile long gCallCount = 0;
}  // namespace

// kcdx.cap05.probe(...) — a no-op counter. Genuinely callable from pak
// Lua; the pak test calls it to trigger the dynamic_hook it installed on
// this function's address. Returns 0 values to Lua.
static int Lua_Cap05Probe(struct lua_State* /*L*/, void* /*user_data*/) {
    InterlockedIncrement(&gCallCount);
    return 0;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);
    gLog = kcdxLogger(api, self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (!scripting) {
        gLog.Error("INIT", "QueryInterface(Scripting) returned null");
        // The pak-Lua guards on kcdx.cap05.probe being present and will
        // report the real FAIL if this registration never happened.
        return true;
    }

    // Register under cap-05's own namespace (kcdx.cap05.probe) so the pak
    // test targets a cfunction it owns, not hello-plugin's.
    bool ok = scripting->RegisterFunction(self,
                                          "cap05",
                                          "probe",
                                          Lua_Cap05Probe,
                                          nullptr);
    if (ok) {
        gLog.Info("SCRIPTING", "registered kcdx.cap05.probe");
    } else {
        gLog.Error("SCRIPTING", "RegisterFunction('cap05.probe') failed");
    }
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
