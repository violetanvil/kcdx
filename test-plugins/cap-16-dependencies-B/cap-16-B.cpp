// CAP-16/COMP-06 Plugin B — consumer side.
//
// Declared dependency on cap-16-A. By the time Plugin_Load fires here,
// A's Plugin_Load must have completed (topo-sort guarantee).
// Verifies:
//   - GetPluginInfo("kcdx.cap-16-A") returns non-null
//   - The info struct's version matches what A's TOML declared (1.0.0 = 0x01000000)
//   - GetPluginHandle("kcdx.cap-16-A") returns a valid handle

#include <windows.h>
#include <cstdio>
#include "kcdx/Interfaces.h"

namespace { const char* kName = "kcdx.cap-16-B"; }

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);

    const kcdxPluginInfo* a = api->GetPluginInfo("kcdx.cap-16-A");
    if (!a) {
        api->ReportTestResult(self, "CAP-16", 0,
            "GetPluginInfo('kcdx.cap-16-A') returned null - A not loaded?");
        return true;
    }
    kcdxPluginHandle aHandle = api->GetPluginHandle("kcdx.cap-16-A");
    if (aHandle == kcdxInvalidPluginHandle) {
        api->ReportTestResult(self, "CAP-16", 0,
            "GetPluginHandle('kcdx.cap-16-A') returned invalid handle");
        return true;
    }

    char reason[300];
    if (a->version == 0x01000000) {  // semver 1.0.0
        snprintf(reason, sizeof(reason),
            "A loaded first (handle=%u, version=0x%08X, displayName='%s')",
            aHandle, a->version, a->displayName ? a->displayName : "?");
        api->ReportTestResult(self, "CAP-16", 1, reason);
    } else {
        snprintf(reason, sizeof(reason),
            "A loaded but version=0x%08X (expected 0x01000000 = 1.0.0)",
            a->version);
        api->ReportTestResult(self, "CAP-16", 0, reason);
    }
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
