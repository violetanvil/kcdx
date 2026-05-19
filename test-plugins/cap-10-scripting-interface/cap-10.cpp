// CAP-10 — kcdxScriptingInterface::RegisterFunction returns true.

#include <windows.h>
#include "kcdx/Interfaces.h"

namespace { const char* kName = "kcdx.cap-10-scripting-interface"; }

// A trivial C function. We never expect this to actually be called
// from pak Lua — the test only verifies the registration succeeds.
static int Lua_Stub(struct lua_State* /*L*/, void* /*ud*/) {
    return 0;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);

    auto* scripting = static_cast<kcdxScriptingInterface*>(
        api->QueryInterface(kcdxInterface_Scripting,
                            kcdxScriptingInterface_Version));
    if (!scripting) {
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
        api->ReportTestResult(self, "CAP-10", 1,
            "RegisterFunction('cap10test.stub') queued/applied successfully");
    } else {
        api->ReportTestResult(self, "CAP-10", 0,
            "RegisterFunction returned false");
    }
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
