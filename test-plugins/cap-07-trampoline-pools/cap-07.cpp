// CAP-07 trampoline-pool allocation test.

#include <windows.h>
#include <cstdio>
#include <cstdint>
#include "kcdx/Interfaces.h"

namespace { const char* kName = "kcdx.cap-07-trampoline-pools"; }

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    kcdxPluginHandle self = api->GetPluginHandle(kName);

    auto* tramp = static_cast<kcdxTrampolineInterface*>(
        api->QueryInterface(kcdxInterface_Trampoline,
                            kcdxTrampolineInterface_Version));
    if (!tramp) {
        api->ReportTestResult(self, "CAP-07", 0,
            "QueryInterface(Trampoline) returned null");
        return true;
    }

    void* branch = tramp->AllocateFromBranchPool(self, 64);
    if (!branch) {
        api->ReportTestResult(self, "CAP-07", 0,
            "AllocateFromBranchPool returned null");
        return true;
    }

    // Verify branch alloc is within rel32 range of WHGame.dll.
    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (whgame) {
        uintptr_t whBase = reinterpret_cast<uintptr_t>(whgame);
        uintptr_t b      = reinterpret_cast<uintptr_t>(branch);
        int64_t offset = (b > whBase) ? int64_t(b - whBase) : -int64_t(whBase - b);
        bool inRange = (offset > -int64_t(0x80000000ll))
                    && (offset < int64_t(0x7FFFFFFFll));
        if (!inRange) {
            char msg[200];
            snprintf(msg, sizeof(msg),
                "branch alloc 0x%p outside rel32 range from WHGame.dll (offset %lld)",
                branch, static_cast<long long>(offset));
            api->ReportTestResult(self, "CAP-07", 0, msg);
            return true;
        }
    }

    void* local = tramp->AllocateFromLocalPool(self, 64);
    if (!local) {
        api->ReportTestResult(self, "CAP-07", 0,
            "AllocateFromLocalPool returned null");
        return true;
    }

    char msg[200];
    snprintf(msg, sizeof(msg),
        "branch=%p (in rel32 range), local=%p", branch, local);
    api->ReportTestResult(self, "CAP-07", 1, msg);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
