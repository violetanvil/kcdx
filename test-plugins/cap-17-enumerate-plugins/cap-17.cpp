// CAP-17 EnumeratePlugins test.

#include <windows.h>
#include <cstdio>
#include <vector>
#include "kcdx/Interfaces.h"

namespace { const char* kName = "kcdx.cap-17-enumerate-plugins"; }

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);

    // Query count
    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    if (count == 0) {
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
        api->ReportTestResult(self, "CAP-17", 0, msg);
        return true;
    }

    // Check self is in the list
    bool foundSelf = false;
    for (kcdxPluginHandle h : handles) {
        if (h == self) { foundSelf = true; break; }
    }
    if (!foundSelf) {
        api->ReportTestResult(self, "CAP-17", 0,
            "self handle not present in EnumeratePlugins result");
        return true;
    }

    char msg[160];
    snprintf(msg, sizeof(msg),
        "count=%u, self handle (%u) present in list", count, self);
    api->ReportTestResult(self, "CAP-17", 1, msg);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
