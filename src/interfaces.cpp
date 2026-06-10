// interfaces.cpp — the kcdxInterface instance plugins receive at load time.
//
// The initial kcdxInterface shipped with QueryInterface returning null
// for everything (sub-interfaces landed later), but the
// GetPluginInfo / GetPluginHandle / EnumeratePlugins / ResolveAddress
// functions are real.
//
// As later phases land, QueryInterface gets new cases.

#include "kcdx/Interfaces.h"
#include "address_library.h"
#include "asset_interface.h"
#include "bytes_interface.h"
#include "conflict_engine.h"
#include "console.h"
#include "declare_interface.h"
#include "dll_interface.h"
#include "functions_interface.h"
#include "hook_chain.h"
#include "hook_interface.h"
#include "load_order.h"
#include "log.h"
#include "messaging.h"
#include "patch_engine.h"
#include "plugin_loader.h"
#include "refdb.h"
#include "scripting_interface.h"
#include "serialization.h"
#include "symbols.h"
#include "task.h"
#include "test.h"
#include "trampoline.h"

#include <algorithm>
#include <vector>

#include <windows.h>

#include <cstdint>
#include <cstring>
#include <string>

namespace kcdx::plugins {

namespace {

// Forward decl — defined below alongside the kcdxMemoryInterface
// thunks.
const kcdxMemoryInterface* GetMemoryInterface();

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

    case kcdxInterface_Memory:
        if (version > kcdxMemoryInterface_Version) return nullptr;
        return const_cast<kcdxMemoryInterface*>(GetMemoryInterface());

    case kcdxInterface_Serialization:
        if (version > kcdxSerializationInterface_Version) return nullptr;
        return const_cast<kcdxSerializationInterface*>(
            kcdx::serialization::GetInterface());

    case kcdxInterface_Console:
        if (version > kcdxConsoleInterface_Version) return nullptr;
        return const_cast<kcdxConsoleInterface*>(
            kcdx::console::GetInterface());

    case kcdxInterface_Hook:
        if (version > kcdxHookInterface_Version) return nullptr;
        return const_cast<kcdxHookInterface*>(
            kcdx::hook_interface::GetInterface());

    case kcdxInterface_Bytes:
        if (version > kcdxBytesInterface_Version) return nullptr;
        return const_cast<kcdxBytesInterface*>(
            kcdx::bytes_interface::GetInterface());

    case kcdxInterface_Declare:
        if (version > kcdxDeclareInterface_Version) return nullptr;
        return const_cast<kcdxDeclareInterface*>(
            kcdx::declare_interface::GetInterface());

    case kcdxInterface_Assets:
        if (version > kcdxAssetInterface_Version) return nullptr;
        return const_cast<kcdxAssetInterface*>(
            kcdx::asset_interface::GetInterface());

    case kcdxInterface_Functions:
        if (version > kcdxFunctionsInterface_Version) return nullptr;
        return const_cast<kcdxFunctionsInterface*>(
            kcdx::functions_interface::GetInterface());

    case kcdxInterface_Dll:
        if (version > kcdxDllInterface_Version) return nullptr;
        return const_cast<kcdxDllInterface*>(
            kcdx::dll_interface::GetInterface());

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
        // Vestigial ABI fields from the integer version-scheme (now the
        // <supports> string-wildcard model). runtimeCompatibleGameVersion is
        // dead (always 0); versionIndependent is re-derived — an empty
        // `supports` list means the plugin pins no specific version. Both
        // fields stay in kcdxPluginInfo until a future interface-versioned
        // cleanup retires them (kept now to preserve the fixed-offset ABI shape).
        info->runtimeCompatibleGameVersion = 0;
        info->versionIndependent           = p->manifest.supports.empty() ? 1 : 0;
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

uintptr_t Thunk_ResolveAddress(uint64_t id) {
    // Plugin-supplied numeric IDs are kcdx_ids in the refdb cache. A plugin
    // that hardcoded a legacy seed ID (pre-flatten 1xxx / 2xxx / 3xxx) gets 0
    // back if the id is unknown — the call site's existing error path fires.
    return kcdx::refdb::ResolveAddrById(id);
}

uintptr_t Thunk_ResolveAddressByName(const char* name) {
    if (!name) return 0;
    // ANONYMOUS by-name resolution: this thunk receives only `name` and so
    // resolves the ENGINE-SEED + EXPLICIT-PREFIX path only — NO self tier.
    // A single shared g_api is handed to every plugin (GetEngineInterfaceImpl),
    // so there is no per-call C++ identity to read here, and this may be called
    // from any context (Load, PostLoad, a hook callback) — a "current loading
    // plugin" static would mis-attribute every later call (a wrong owner
    // silently picks the wrong precedence tier), which is worse than "".
    // Passing "" / "" for (author, plugin) keeps an explicit-form reference
    // exact and a bare name on the engine-seed path. The C++/Lua parity gap
    // is CLOSED by the sibling Thunk_ResolveAddressByNameAs(owner, name)
    // below: a plugin that wants full self > engine > other precedence passes
    // its OWN handle, which threads the self tier.
    return kcdx::address_library::ResolveByName(name, "", "");
}

uintptr_t Thunk_ResolveSymbol(const char* name) {
    if (!name) return 0;
    // Anonymous (no owner) — symbols::Lookup with both author and plugin
    // empty walks the bare/anonymous path: explicit-prefix references
    // resolve exactly; bare references skip the self tier (no owner to
    // be self-equal to) and fall through to other. This is correct-by-
    // design for a C++ caller with no per-call identity; plugins that
    // want self > other precedence call the sibling
    // Thunk_ResolveSymbolAs(handle, name).
    auto v = kcdx::symbols::Lookup(name, "", "");
    return v.value_or(0);
}

uintptr_t Thunk_ResolveSymbolAs(kcdxPluginHandle owner, const char* name) {
    if (!name) return 0;
    // Step 4 of the 2-dot namespace refactor: the thunk threads the
    // real (author, plugin) pair off the calling plugin's manifest, so
    // the symbol resolver sees the full identity for self > other
    // precedence. When [plugin].author is still empty (the corpus
    // state before step 6) the resolver walks the legacy 1-dot scope
    // by (plugin, name), preserving observable behavior. NameForHandle
    // / AuthorForHandle moved to plugin_loader.{h,cpp} (qualified
    // namespace lookup here so
    // kcdxHookInterface thunks in src/hook_interface.cpp share the
    // same accessors).
    auto v = kcdx::symbols::Lookup(name,
                                   kcdx::plugins::AuthorForHandle(owner),
                                   kcdx::plugins::NameForHandle(owner));
    return v.value_or(0);
}

uintptr_t Thunk_ResolveAddressByNameAs(kcdxPluginHandle owner,
                                       const char* name) {
    if (!name) return 0;
    // Same real (author, plugin) threading as Thunk_ResolveSymbolAs.
    return kcdx::address_library::ResolveByName(
        name, kcdx::plugins::AuthorForHandle(owner).c_str(),
        kcdx::plugins::NameForHandle(owner).c_str());
}

// Plugin-side accessor for the engine's bootstrap-classifier state. Returns
// 1 iff log::IsGameMainThread() returns true on the caller's thread —
// which requires log::g_gameMainThreadId to have been captured by
// SetGameMainThread (called from hook_chain::SetLuaState's first non-null
// L call; the engine bootstrap pump that the dead-classifier regression
// at cap-59 broke + the carve-out at hook_chain.cpp:1075/1209/1341 fixes).
// See docs/known-issues/cap-59-fires...md §Reframe 2026-05-29c.
uint32_t Thunk_IsGameMainThread() {
    return kcdx::log::IsGameMainThread() ? 1u : 0u;
}

// Level filter for plugin-side Log calls.
//
// kcdxLog_* enum is now strictly ordered by verbosity:
//   Trace(0) < Debug(1) < Info(2) < Warn(3) < Error(4)
//
// `threshold` is the plugin manifest's log_level (parsed from TOML).
// 5 is a synthetic "off" sentinel that drops everything.
//
// A Log call passes iff callLevel >= threshold and threshold != off.
static constexpr uint32_t kLogLevelOff = 5;
static bool PluginLogLevelPasses(uint32_t threshold, uint32_t callLevel) {
    if (threshold >= kLogLevelOff) return false;
    return callLevel >= threshold;
}

void Thunk_Log(kcdxPluginHandle self, uint32_t level,
               const char* category, const char* msg) {
    if (!msg) return;
    if (!category || !*category) category = "PLUGIN";

    // Look up the plugin's manifest threshold. Unknown handle -> Info.
    uint32_t threshold = kcdxLog_Info;
    if (self != kcdxInvalidPluginHandle) {
        size_t idx = static_cast<size_t>(self);
        if (idx < g_plugins.size()) {
            threshold = g_plugins[idx].manifest.logLevel;
        }
    }
    if (!PluginLogLevelPasses(threshold, level)) return;

    log::Level lv;
    switch (level) {
        case kcdxLog_Trace: lv = log::Level::Trace; break;
        case kcdxLog_Debug: lv = log::Level::Debug; break;
        case kcdxLog_Warn:  lv = log::Level::Warn;  break;
        case kcdxLog_Error: lv = log::Level::Error; break;
        case kcdxLog_Info:
        default:            lv = log::Level::Info;  break;
    }
    log::EmitPlugin(lv, self, category, msg);
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
    // Collect matching entries across the legacy g_patches surface (empty
    // now — kept guarded, scoped to a later cycle) and the LIVE sources
    // below (hook_chain participants + applied kcdx.bytes patches). Names are
    // stable for the process lifetime (PatchEntry strings, Chain-stable
    // storage), so we can hand out raw c_str() / aliased pointers.
    struct TempEntry {
        const char* name;
        int         priority;
        int         kind;
        bool        applied;
    };
    std::vector<TempEntry> hits;

    // Patches: target lies within [patchAddr, patchAddr + replacement.size())
    // Skip entries whose plugin is disabled via load_order.toml — they
    // never applied and shouldn't appear in conflict reports.
    for (size_t i = 0; i < kcdx::patch::g_patches.size(); ++i) {
        const auto& p = kcdx::patch::g_patches[i];
        if (!kcdx::load_order::IsPluginEnabled(p.pluginName)) continue;
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

    // (The legacy g_hooks loop that once joined here was removed in the
    // apply-consolidation cut alongside conflict_engine::g_resolvedHooks —
    // g_hooks had no populator after the TOML behavior tables were removed,
    // so it contributed nothing. The
    // LIVE hook source is the hook_chain enumeration below; the LIVE byte
    // source is the kcdx.bytes GetAppliedBytesPatchesAtTarget enumeration.)

    // kcdx.hook (hook_chain): the new function-interception surface. Both
    // chain winners (installed, applied=true) and CanCoexist-rejected losers
    // (applied=false) at this resolved runtime VA — same VA space as `target`
    // (FindChain's targetVa == what ResolveLocator produced == rh.targetAddr).
    // The returned `name` pointers live in Chain-stable storage for the
    // process lifetime, identical to the legacy c_str() aliases above, so
    // capturing them into TempEntry (an alias, not a copy) is safe.
    for (const auto& pp : kcdx::hook_chain::GetParticipantsAtTarget(target)) {
        hits.push_back({
            pp.name, pp.priority,
            kcdxConflictEntryKind_Hook, pp.applied
        });
    }

    // kcdx.bytes (kcdxBytesInterface::Register): the new byte-rewrite surface.
    // These route through lua_registry Kind::Bytes, NOT the legacy g_patches
    // above, so they have their own source. Reported when the queried `target`
    // falls within an applied bytes patch's write range — same range-match
    // semantics as the legacy g_patches loop, kind=Patch. The `name` pointers
    // live in PatchEntry storage for the process lifetime (the accessor
    // documents this lifetime guarantee), so capturing them as aliases is safe.
    for (const auto& bp : kcdx::patch::GetAppliedBytesPatchesAtTarget(target)) {
        hits.push_back({
            bp.name, bp.priority,
            kcdxConflictEntryKind_Patch, bp.applied
        });
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

// ---------------------------------------------------------------------
// kcdxMemoryInterface impl
// ---------------------------------------------------------------------

static std::wstring Utf8ToWide(const char* s) {
    if (!s) return {};
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, nullptr, 0);
    std::wstring w(n > 0 ? n - 1 : 0, L'\0');
    if (n > 0) MultiByteToWideChar(CP_UTF8, 0, s, -1, w.data(), n);
    return w;
}

uintptr_t Mem_ScanPattern(const char* moduleName, const char* pattern) {
    if (!pattern) return 0;
    std::wstring modw = moduleName ? Utf8ToWide(moduleName) : std::wstring();
    auto va = kcdx::patch::ScanModuleFirst(modw, std::string(pattern));
    return va.value_or(0);
}

uintptr_t Mem_GetModuleBase(const char* moduleName) {
    std::wstring modw = moduleName ? Utf8ToWide(moduleName) : std::wstring();
    HMODULE h = modw.empty() ? GetModuleHandleW(nullptr)
                              : GetModuleHandleW(modw.c_str());
    return reinterpret_cast<uintptr_t>(h);
}

int Mem_WriteBytes(uintptr_t addr, const void* bytes, size_t size) {
    if (!bytes || size == 0) return 0;
    DWORD oldProtect = 0;
    if (!VirtualProtect(reinterpret_cast<LPVOID>(addr), size,
                        PAGE_EXECUTE_READWRITE, &oldProtect)) {
        return 0;
    }
    std::memcpy(reinterpret_cast<void*>(addr), bytes, size);
    DWORD ignored = 0;
    VirtualProtect(reinterpret_cast<LPVOID>(addr), size, oldProtect, &ignored);
    FlushInstructionCache(GetCurrentProcess(),
                          reinterpret_cast<LPCVOID>(addr), size);
    return 1;
}

int Mem_ReadBytes(uintptr_t addr, void* out, size_t size) {
    if (!out || size == 0) return 0;
    MEMORY_BASIC_INFORMATION mbi{};
    if (VirtualQuery(reinterpret_cast<LPCVOID>(addr), &mbi, sizeof(mbi)) == 0) {
        return 0;
    }
    if (mbi.State != MEM_COMMIT) return 0;
    // Check readability via Protect bits.
    DWORD readableBits = PAGE_READONLY | PAGE_READWRITE | PAGE_WRITECOPY
                       | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE
                       | PAGE_EXECUTE_WRITECOPY;
    if (!(mbi.Protect & readableBits)) return 0;
    std::memcpy(out, reinterpret_cast<const void*>(addr), size);
    return 1;
}

kcdxMemoryInterface g_memory = {
    /*ScanPattern=*/   Mem_ScanPattern,
    /*GetModuleBase=*/ Mem_GetModuleBase,
    /*WriteBytes=*/    Mem_WriteBytes,
    /*ReadBytes=*/     Mem_ReadBytes,
};

const kcdxMemoryInterface* GetMemoryInterface() {
    return &g_memory;
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
    /*ResolveSymbol=*/      Thunk_ResolveSymbol,
    /*Log=*/                Thunk_Log,
    /*GetPluginPath=*/      Thunk_GetPluginPath,
    /*ReportTestResult=*/   Thunk_ReportTestResult,
    /*GetConflictReport=*/  Thunk_GetConflictReport,
    // Append-only (see Interfaces.h): new pointers go at the END so
    // existing-compiled plugins keep their offsets.
    /*ResolveAddressByName=*/   Thunk_ResolveAddressByName,
    /*ResolveSymbolAs=*/        Thunk_ResolveSymbolAs,
    /*ResolveAddressByNameAs=*/ Thunk_ResolveAddressByNameAs,
    /*IsGameMainThread=*/       Thunk_IsGameMainThread,
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
