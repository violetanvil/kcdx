// interfaces.cpp — the kcdxInterface instance plugins receive at load time.
//
// Phase 2 ships a working kcdxInterface with QueryInterface returning null
// for everything (sub-interfaces land in Phases 3-7), but the
// GetPluginInfo / GetPluginHandle / EnumeratePlugins / ResolveAddress
// functions are real.
//
// As later phases land, QueryInterface gets new cases.

#include "kcdx/Interfaces.h"
#include "log.h"
#include "messaging.h"
#include "plugin_loader.h"
#include "scripting_interface.h"
#include "task.h"
#include "trampoline.h"

#include <cstdint>

namespace kcdx::plugins {

namespace {

void* Thunk_QueryInterface(uint32_t interfaceID, uint32_t version) {
    switch (interfaceID) {
    case kcdxInterface_Messaging:
        if (version > kcdxMessagingInterface_Version) return nullptr;
        return const_cast<kcdxMessagingInterface*>(messaging::GetInterface());

    case kcdxInterface_Task:
        if (version > kcdxTaskInterface_Version) return nullptr;
        return const_cast<kcdxTaskInterface*>(task::GetInterface());

    case kcdxInterface_Trampoline:
        if (version > kcdxTrampolineInterface_Version) return nullptr;
        return const_cast<kcdxTrampolineInterface*>(trampoline::GetInterface());

    case kcdxInterface_Scripting:
        if (version > kcdxScriptingInterface_Version) return nullptr;
        return const_cast<kcdxScriptingInterface*>(
            scripting_interface::GetInterface());

    // Phase 6 fills this in.
    case kcdxInterface_Serialization:
    default:
        return nullptr;
    }
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

void Thunk_Log(kcdxPluginHandle self, uint32_t level, const char* msg) {
    if (!msg) return;
    std::string s(msg);
    switch (level) {
    case kcdxLog_Warn:  log::PluginWarn (self, s); break;
    case kcdxLog_Error: log::PluginError(self, s); break;
    case kcdxLog_Debug: log::PluginDebug(self, s); break;
    case kcdxLog_Info:
    default:            log::PluginInfo (self, s); break;
    }
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
    /*Log=*/                Thunk_Log,
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
