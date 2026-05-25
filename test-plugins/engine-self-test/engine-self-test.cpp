// engine-self-test — DLL companion to engine-self-test/kcdx.toml.
//
// Single job: prove ReportTestResult fires end-to-end. On Plugin_Load,
// records pass + reason. The aggregator's roll-up line should show
// "suite: 1/1 passing" in kcdx.log on the next lifecycle message.

#include <windows.h>
#include "kcdx/Interfaces.h"

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle("engine_self_test");
    kcdxLogger gLog(api, self);

    gLog.Info("INIT", "kcdxPlugin_Load called");

    const char* reason = "Plugin_Load fired and ReportTestResult reached";
    gLog.Info("SELFTEST", "PASS: %s", reason);
    api->ReportTestResult(self,
                          "engine-self-test",
                          /*pass=*/1,
                          reason);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
