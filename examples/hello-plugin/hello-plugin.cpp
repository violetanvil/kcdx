// hello-plugin.cpp — minimal kcdx plugin
//
// Demonstrates the smallest possible kcdx plugin: a DLL with the required
// kcdxPluginVersionData export and a kcdxPlugin_Load function that writes
// a marker to kcdx.log via OutputDebugString. Also subscribes to engine
// lifecycle messages (Phase 3) and submits a task to the main thread.
//
// Build with the example CMakeLists.txt in this folder.

#include <windows.h>
#include <cstdio>
#include <cstring>

#include "kcdx/Interfaces.h"

extern "C" __declspec(dllexport)
kcdxPluginVersionData kcdxPluginVersionData = {
    /*dataVersion=*/        kcdxPluginVersionData_CurrentVersion,
    /*pluginVersion=*/      0x00010000u,                  // 0.1.0
    /*name=*/               "violetanvil.hello-plugin",
    /*author=*/             "violetanvil",
    /*supportEmail=*/       "noreply@example.com",
    /*versionIndependenceEx=*/ 0,
    /*versionIndependence=*/ kcdxVersionIndependent_AddressLibrary,
    /*compatibleGameVersions=*/ { 0 },                    // any version (relying on AddressLibrary)
    /*kcdxVersionRequired=*/ 0x00010000u,
    /*reserved=*/           { 0 },
    /*inlinePatchesToml=*/  nullptr,
    /*dependencies=*/       nullptr,
};

// Lifecycle callback: fires for each engine-originated message we subscribed
// to. The engine fires these with sender == nullptr.
static void OnEngineMessage(kcdxMessage* msg) {
    const char* name = "?";
    switch (msg->messageType) {
        case kcdxMessage_PostLoad:      name = "PostLoad";      break;
        case kcdxMessage_PostPostLoad:  name = "PostPostLoad";  break;
        case kcdxMessage_InputLoaded:   name = "InputLoaded";   break;
        case kcdxMessage_NewGame:       name = "NewGame";       break;
        case kcdxMessage_PreLoadGame:   name = "PreLoadGame";   break;
        case kcdxMessage_PostLoadGame:  name = "PostLoadGame";  break;
        case kcdxMessage_SaveGame:      name = "SaveGame";      break;
        case kcdxMessage_DeleteGame:    name = "DeleteGame";    break;
    }
    char buf[160];
    snprintf(buf, sizeof(buf), "[hello-plugin] engine message: %s (type=%u)\n",
             name, msg->messageType);
    OutputDebugStringA(buf);
}

// Example task: prints from the main thread. Plugins extend kcdxTask, override
// Run() (which gets called on the main thread next update tick), and Dispose()
// (which typically just `delete this`).
struct HelloTask : kcdxTask {
    void Run() override {
        OutputDebugStringA("[hello-plugin] HelloTask::Run on main thread\n");
    }
    void Dispose() override {
        delete this;
    }
};

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    OutputDebugStringA("[hello-plugin] kcdxPlugin_Load called\n");

    // Look ourselves up.
    kcdxPluginHandle self = api->GetPluginHandle("violetanvil.hello-plugin");
    char buf[160];
    snprintf(buf, sizeof(buf), "[hello-plugin] my handle is %u, engine version 0x%08X, "
                               "game version 0x%08X\n",
             self, api->kcdxVersion, api->runtimeGameVersion);
    OutputDebugStringA(buf);

    // List loaded plugins.
    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    snprintf(buf, sizeof(buf), "[hello-plugin] %u plugin(s) loaded total\n", count);
    OutputDebugStringA(buf);

    // Subscribe to engine lifecycle messages.
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kcdxInterface_Messaging, kcdxMessagingInterface_Version));
    if (msg) {
        msg->RegisterListener(self, /*sender=*/nullptr, OnEngineMessage);
        OutputDebugStringA("[hello-plugin] subscribed to engine messages\n");
    } else {
        OutputDebugStringA("[hello-plugin] WARN: Messaging interface unavailable\n");
    }

    // Submit a task to the main thread.
    auto* task = static_cast<kcdxTaskInterface*>(
        api->QueryInterface(kcdxInterface_Task, kcdxTaskInterface_Version));
    if (task) {
        task->AddTask(new HelloTask());
        OutputDebugStringA("[hello-plugin] submitted a HelloTask\n");
    } else {
        OutputDebugStringA("[hello-plugin] WARN: Task interface unavailable\n");
    }

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
