// interfaces.cpp — the kcdxInterface instance plugins receive at load time.
//
// Phase 2 ships a working kcdxInterface with QueryInterface returning null
// for everything (sub-interfaces land in Phases 3-7), but the
// GetPluginInfo / GetPluginHandle / EnumeratePlugins / ResolveAddress
// functions are real.
//
// As later phases land, QueryInterface gets new cases.

#include "kcdx/Interfaces.h"
#include "conflict_engine.h"
#include "hook_engine.h"
#include "log.h"
#include "messaging.h"
#include "patch_engine.h"
#include "plugin_loader.h"
#include "scripting_interface.h"
#include "task.h"
#include "test.h"
#include "trampoline.h"

#include <algorithm>
#include <vector>

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

// Lazy-build a kcdxPluginInfo snapshot for a LoadedPlugin. The returned
// pointer references the plugin's own manifest fields (stable for the
// process lifetime), so it remains valid until kcdx is unloaded.
const kcdxPluginInfo* Thunk_GetPluginInfo(const char* name) {
    const LoadedPlugin* p = FindByName(name);
    if (!p) return nullptr;
    if (!p->infoCache) {
        auto info = std::make_unique<kcdxPluginInfo>();
        info->name                         = p->manifest.name.c_str();
        info->displayName                  = p->manifest.displayName.c_str();
        info->author                       = p->manifest.author.c_str();
        info->description                  = p->manifest.description.c_str();
        info->url                          = p->manifest.url.c_str();
        info->supportEmail                 = p->manifest.supportEmail.c_str();
        info->version                      = p->manifest.version;
        info->kcdxMinVersion               = p->manifest.kcdxMinVersion;
        info->runtimeCompatibleGameVersion = 0;
        // The matched game version isn't stored on LoadedPlugin currently;
        // it lives in the Candidate during discovery and gets discarded.
        // If we find that consumers need it, we can plumb it through.
        info->versionIndependent           = p->manifest.versionIndependent ? 1 : 0;
        p->infoCache = std::move(info);
    }
    return p->infoCache.get();
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

// Level priority for filtering. Higher number = louder. log_level=4 (off)
// means "drop everything." Within the kcdxLog_* enum the ordering is
// Info(0) < Warn(1) < Error(2) < Debug(3), which is NOT a strict
// severity order — Debug is the most verbose but the lowest priority
// gate. Map both to a comparable severity scale for filtering.
//
// Mapping (manifest log_level threshold) -> "what passes":
//   debug (3) — everything (incl. Debug)
//   info  (0) — Info + Warn + Error (drops Debug)
//   warn  (1) — Warn + Error (drops Info + Debug)
//   error (2) — Error only (drops Info + Warn + Debug)
//   off   (4) — drop all
static bool PluginLogLevelPasses(uint32_t threshold, uint32_t callLevel) {
    if (threshold == 4) return false;  // off
    // threshold=3 (debug) lets everything pass.
    if (threshold == 3) return true;
    // For thresholds 0/1/2: only let calls of severity >= threshold pass
    // in the "info < warn < error" sense. Debug (callLevel=3) is treated
    // as below Info — only passes when threshold=debug.
    if (callLevel == kcdxLog_Debug) return false;
    return callLevel >= threshold;
}

void Thunk_Log(kcdxPluginHandle self, uint32_t level, const char* msg) {
    if (!msg) return;

    // Look up the plugin's log_level threshold from its manifest.
    // Unknown handle -> default Info (0) -> drops only Debug.
    uint32_t threshold = kcdxLog_Info;
    if (self != kcdxInvalidPluginHandle) {
        size_t idx = static_cast<size_t>(self);
        if (idx < g_plugins.size()) {
            threshold = g_plugins[idx].manifest.logLevel;
        }
    }
    if (!PluginLogLevelPasses(threshold, level)) return;

    std::string s(msg);
    switch (level) {
    case kcdxLog_Warn:  log::PluginWarn (self, s); break;
    case kcdxLog_Error: log::PluginError(self, s); break;
    case kcdxLog_Debug: log::PluginDebug(self, s); break;
    case kcdxLog_Info:
    default:            log::PluginInfo (self, s); break;
    }
}

void Thunk_ReportTestResult(kcdxPluginHandle /*self*/,
                            const char* testName,
                            int pass,
                            const char* reason) {
    if (!testName) return;
    kcdx::test::ReportResult(testName, pass != 0, reason ? reason : "");
}

uint32_t Thunk_GetConflictReport(uintptr_t target,
                                 kcdxConflictEntry* out,
                                 uint32_t cap) {
    // Collect matching entries from both patches and hooks. Names are
    // stable for the process lifetime (live in PatchEntry/HookEntry
    // strings inside the static g_patches/g_hooks vectors), so we can
    // hand out raw c_str() pointers.
    struct TempEntry {
        const char* name;
        int         priority;
        int         kind;
        bool        applied;
    };
    std::vector<TempEntry> hits;

    // Patches: target lies within [patchAddr, patchAddr + replacement.size())
    for (size_t i = 0; i < kcdx::patch::g_patches.size(); ++i) {
        const auto& p = kcdx::patch::g_patches[i];
        if (i >= kcdx::conflict_engine::g_resolvedPatches.size()) break;
        const auto& r = kcdx::conflict_engine::g_resolvedPatches[i];
        if (!r.ok) continue;
        uintptr_t begin = r.patchAddr;
        uintptr_t end   = begin + p.replacement.size();
        if (target >= begin && target < end) {
            hits.push_back({
                p.name.c_str(), p.priority,
                kcdxConflictEntryKind_Patch, p.appliedOK
            });
        }
    }

    // Hooks: target matches resolved function entry exactly
    for (size_t i = 0; i < kcdx::hook_engine::g_hooks.size(); ++i) {
        const auto& h = kcdx::hook_engine::g_hooks[i];
        if (i >= kcdx::conflict_engine::g_resolvedHooks.size()) break;
        const auto& rh = kcdx::conflict_engine::g_resolvedHooks[i];
        if (!rh.ok) continue;
        if (rh.targetAddr == target) {
            hits.push_back({
                h.name.c_str(), h.priority,
                kcdxConflictEntryKind_Hook, h.appliedOK
            });
        }
    }

    // Sort by (priority asc, name asc)
    std::sort(hits.begin(), hits.end(), [](const TempEntry& a, const TempEntry& b) {
        if (a.priority != b.priority) return a.priority < b.priority;
        return std::string(a.name) < std::string(b.name);
    });

    // Fill out buffer
    if (out && cap > 0) {
        uint32_t n = (hits.size() < cap) ? (uint32_t)hits.size() : cap;
        for (uint32_t i = 0; i < n; ++i) {
            out[i].name     = hits[i].name;
            out[i].priority = hits[i].priority;
            out[i].kind     = hits[i].kind;
            out[i].applied  = hits[i].applied ? 1 : 0;
        }
    }
    return static_cast<uint32_t>(hits.size());
}

// Resolve a plugin's install folder path. Returned pointer is owned by the
// engine and remains valid for the process lifetime — backed by the
// LoadedPlugin::folderPath wstring which lives as long as g_plugins.
const wchar_t* Thunk_GetPluginPath(kcdxPluginHandle handle) {
    if (handle == kcdxInvalidPluginHandle) return nullptr;
    size_t idx = static_cast<size_t>(handle);
    if (idx >= g_plugins.size()) return nullptr;
    const auto& p = g_plugins[idx];
    if (p.folderPath.empty()) return nullptr;
    return p.folderPath.c_str();
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
    /*GetPluginPath=*/      Thunk_GetPluginPath,
    /*ReportTestResult=*/   Thunk_ReportTestResult,
    /*GetConflictReport=*/  Thunk_GetConflictReport,
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
