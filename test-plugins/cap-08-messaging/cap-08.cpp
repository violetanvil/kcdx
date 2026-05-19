// CAP-08 — engine messaging delivery.
//
// Subscribes to engine messages (sender=null). On each message:
//   - tracks which of {PostLoad, PostPostLoad, InputLoaded} have fired
//   - when InputLoaded fires, reports pass/fail based on whether all 3
//     observed.

#include <windows.h>
#include <cstdio>
#include "kcdx/Interfaces.h"

namespace {
const char* kName  = "kcdx.cap-08-messaging";

const kcdxInterface* g_api  = nullptr;
kcdxPluginHandle     g_self = kcdxInvalidPluginHandle;

bool g_sawPostLoad     = false;
bool g_sawPostPostLoad = false;
bool g_sawInputLoaded  = false;
bool g_reported        = false;

void OnMessage(kcdxMessage* msg) {
    switch (msg->messageType) {
    case kcdxMessage_PostLoad:     g_sawPostLoad     = true; break;
    case kcdxMessage_PostPostLoad: g_sawPostPostLoad = true; break;
    case kcdxMessage_InputLoaded:  g_sawInputLoaded  = true; break;
    default: return;  // ignore others
    }

    // Once InputLoaded fires, all three should be set.
    if (msg->messageType == kcdxMessage_InputLoaded && !g_reported) {
        g_reported = true;
        char reason[200];
        if (g_sawPostLoad && g_sawPostPostLoad && g_sawInputLoaded) {
            snprintf(reason, sizeof(reason),
                "received PostLoad+PostPostLoad+InputLoaded in order");
            g_api->ReportTestResult(g_self, "CAP-08", 1, reason);
        } else {
            snprintf(reason, sizeof(reason),
                "missed message(s) before InputLoaded: PostLoad=%d, PostPostLoad=%d",
                g_sawPostLoad ? 1 : 0, g_sawPostPostLoad ? 1 : 0);
            g_api->ReportTestResult(g_self, "CAP-08", 0, reason);
        }
    }
}
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_api  = api;
    g_self = api->GetPluginHandle(kName);
    auto* m = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging,
                            kcdxMessagingInterface_Version));
    if (!m) {
        api->ReportTestResult(g_self, "CAP-08", 0,
            "QueryInterface(Messaging) returned null");
        return true;
    }
    m->RegisterListener(g_self, nullptr, OnMessage);
    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
