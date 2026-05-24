// CAP-17 EnumeratePlugins test.

#include <windows.h>
#include <cstdio>
#include <vector>
#include "kcdx/Interfaces.h"

namespace {
const char* kName = "ts_cap_17_enumerate_plugins";
kcdxLogger  gLog;
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);
    gLog = kcdxLogger(api, self);
    gLog.Info("INIT", "kcdxPlugin_Load called");

    // Query count
    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    if (count == 0) {
        gLog.Error("ENUMERATE", "FAIL: EnumeratePlugins count == 0; expected >= 1 (self)");
        api->ReportTestResult(self, "CAP-17", 0,
            "EnumeratePlugins count == 0; expected >= 1 (self)");
        return true;
    }

    // Query handles
    std::vector<kcdxPluginHandle> handles(count);
    uint32_t got = api->EnumeratePlugins(handles.data(), count);
    if (got != count) {
        char msg[160];
        snprintf(msg, sizeof(msg),
            "EnumeratePlugins returned %u handles but count was %u",
            got, count);
        gLog.Error("ENUMERATE", "FAIL: %s", msg);
        api->ReportTestResult(self, "CAP-17", 0, msg);
        return true;
    }

    // Check self is in the list
    bool foundSelf = false;
    for (kcdxPluginHandle h : handles) {
        if (h == self) { foundSelf = true; break; }
    }
    if (!foundSelf) {
        gLog.Error("ENUMERATE", "FAIL: self handle not present in EnumeratePlugins result");
        api->ReportTestResult(self, "CAP-17", 0,
            "self handle not present in EnumeratePlugins result");
        return true;
    }

    char msg[160];
    snprintf(msg, sizeof(msg),
        "count=%u, self handle (%u) present in list", count, self);
    gLog.Info("ENUMERATE", "PASS: %s", msg);
    api->ReportTestResult(self, "CAP-17", 1, msg);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
