// interfaces.cpp — the kcdxInterface instance plugins receive at load time.
//
// Phase 2 ships a working kcdxInterface with QueryInterface returning null
// for everything (sub-interfaces land in Phases 3-7), but the
// GetPluginInfo / GetPluginHandle / EnumeratePlugins / ResolveAddress
// functions are real.
//
// As later phases land, QueryInterface gets new cases.

#include "kcdx/Interfaces.h"
#include "plugin_loader.h"

#include <cstdint>

namespace kcdx::plugins {

namespace {

void* Thunk_QueryInterface(uint32_t interfaceID, uint32_t /*version*/) {
    // Phase 3+ fills these in. Until then, every sub-interface is unimplemented.
    (void)interfaceID;
    return nullptr;
}

const kcdxPluginVersionData* Thunk_GetPluginInfo(const char* name) {
    if (const auto* p = FindByName(name)) return p->versionData;
    return nullptr;
}

kcdxPluginHandle Thunk_GetPluginHandle(const char* name) {
    return HandleOf(name);
}

uint32_t Thunk_EnumeratePlugins(kcdxPluginHandle* out, uint32_t cap) {
    uint32_t n = static_cast<uint32_t>(g_plugins.size());
    if (out && cap > 0) {
        uint32_t copy = (n < cap) ? n : cap;
        for (uint32_t i = 0; i < copy; ++i) {
            out[i] = g_plugins[i].handle;
        }
    }
    return n;
}

uintptr_t Thunk_ResolveAddress(uint64_t /*id*/) {
    // Phase 7 fills this in. Until then, every lookup misses.
    return 0;
}

// The static instance. Initialized at program startup before any plugin code
// runs because DiscoverAndLoad fetches it.
kcdxInterface g_api = {
    /*kcdxVersion=*/        kEngineVersion,
    /*runtimeGameVersion=*/ 0,  // patched at DiscoverAndLoad time
    /*QueryInterface=*/     Thunk_QueryInterface,
    /*GetPluginInfo=*/      Thunk_GetPluginInfo,
    /*GetPluginHandle=*/    Thunk_GetPluginHandle,
    /*EnumeratePlugins=*/   Thunk_EnumeratePlugins,
    /*ResolveAddress=*/     Thunk_ResolveAddress,
};

}  // namespace

const kcdxInterface* GetEngineInterfaceImpl() {
    // Patch the runtime game version into the published instance every time
    // it's fetched. Cheap, and it means we don't have to remember to update
    // it after DetectRuntimeGameVersion runs.
    g_api.runtimeGameVersion = g_runtimeGameVersion;
    return &g_api;
}

}  // namespace kcdx::plugins
