// engine-self-test — DLL companion to engine-self-test/kcdx.toml.
//
// Single job: prove ReportTestResult fires end-to-end. On Plugin_Load,
// records pass + reason. The aggregator's roll-up line should show
// "Test suite: 1/1 passing" in kcdx.log on the next lifecycle message.

#include <windows.h>
#include "kcdx/Interfaces.h"

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle("kcdx.engine-self-test");
    api->ReportTestResult(self,
                          "engine-self-test",
                          /*pass=*/1,
                          "Plugin_Load fired and ReportTestResult reached");
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
