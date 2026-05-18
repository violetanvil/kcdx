// hello-plugin.cpp — minimal kcdx plugin
//
// Demonstrates the smallest possible kcdx plugin: a DLL with the required
// kcdxPluginVersionData export and a kcdxPlugin_Load function that writes
// a marker to kcdx.log via OutputDebugString.
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

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    OutputDebugStringA("[hello-plugin] kcdxPlugin_Load called\n");

    // Demonstrate the plugin API: look ourselves up.
    kcdxPluginHandle self = api->GetPluginHandle("violetanvil.hello-plugin");
    char buf[128];
    snprintf(buf, sizeof(buf), "[hello-plugin] my handle is %u, engine version 0x%08X\n",
             self, api->kcdxVersion);
    OutputDebugStringA(buf);

    // List loaded plugins.
    uint32_t count = api->EnumeratePlugins(nullptr, 0);
    snprintf(buf, sizeof(buf), "[hello-plugin] %u plugin(s) loaded total\n", count);
    OutputDebugStringA(buf);

    return true;
}

BOOL APIENTRY DllMain(HMODULE, DWORD reason, LPVOID) {
    if (reason == DLL_PROCESS_ATTACH) {
        DisableThreadLibraryCalls(GetModuleHandleW(nullptr));
    }
    return TRUE;
}
