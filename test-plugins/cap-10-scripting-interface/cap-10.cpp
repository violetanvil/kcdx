// CAP-10 — kcdxScriptingInterface::RegisterFunction returns true.

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "ts_cap_10_scripting_interface";
kcdxLogger  gLog;
}  // namespace

// A trivial C function. We never expect this to actually be called
// from pak Lua — the test only verifies the registration succeeds.
static int Lua_Stub(struct lua_State* /*L*/, void* /*ud*/) {
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
        api->ReportTestResult(self, "CAP-10", 0,
            "QueryInterface(Scripting) returned null");
        return true;
    }

    // Register under our own namespace so we don't collide with
    // hello-plugin's hello.* entries.
    bool ok = scripting->RegisterFunction(self,
                                          "cap10test",
                                          "stub",
                                          Lua_Stub,
                                          nullptr);
    if (ok) {
        const char* reason = "RegisterFunction('cap10test.stub') queued/applied successfully";
        gLog.Info("SCRIPTING", "PASS: %s", reason);
        api->ReportTestResult(self, "CAP-10", 1, reason);
    } else {
        const char* reason = "RegisterFunction returned false";
        gLog.Error("SCRIPTING", "FAIL: %s", reason);
        api->ReportTestResult(self, "CAP-10", 0, reason);
    }
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
