// CAP-16/COMP-06 Plugin A — producer side.
//
// Loads first (no dependencies declared). Registers a Lua function
// kcdx.cap16producer.greet via the scripting interface, then reports
// PASS on Plugin_Load. Plugin B verifies A loaded before it.

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "cap_16_a";
kcdxLogger  gLog;
}

static int Lua_Greet(struct lua_State* /*L*/, void* /*ud*/) {
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
        api->ReportTestResult(self, "CAP-16-A", 0,
            "QueryInterface(Scripting) returned null");
        return true;
    }
    if (!scripting->RegisterFunction(self, "cap16producer", "greet",
                                     Lua_Greet, nullptr)) {
        gLog.Error("INIT", "RegisterFunction failed");
        api->ReportTestResult(self, "CAP-16-A", 0,
            "RegisterFunction failed");
        return true;
    }
    gLog.Info("DEPS", "PASS: A loaded; registered kcdx.cap16producer.greet");
    api->ReportTestResult(self, "CAP-16-A", 1,
        "A loaded; registered kcdx.cap16producer.greet");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
