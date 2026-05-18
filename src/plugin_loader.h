#pragma once
#include <cstdint>
#include <string>
#include <vector>
#include <windows.h>

#include "kcdx/Interfaces.h"

namespace kcdx::plugins {

// One discovered + loaded plugin.
struct LoadedPlugin {
    std::string                  filePath;        // absolute path to the .dll
    std::string                  folderName;      // plugin folder name (for log diagnostics)
    HMODULE                      module = nullptr; // null for TOML-only plugins (no DLL)
    const kcdxPluginVersionData* versionData = nullptr;  // null for TOML-only plugins
    kcdxPluginHandle             handle = kcdxInvalidPluginHandle;

    // Filled in during discovery, cleared after the load wave completes.
    kcdxPlugin_Preload_t preloadFn = nullptr;
    kcdxPlugin_Load_t    loadFn    = nullptr;

    // Result of the load wave. Default false; set true if loadFn returned true
    // (or if the plugin has no loadFn but is otherwise valid).
    bool loaded = false;
};

// Engine state — populated by DiscoverAndLoad, read by the kcdxInterface impl.
extern std::vector<LoadedPlugin> g_plugins;

// Engine version we report to plugins via kcdxInterface::kcdxVersion.
// Bumped when the public ABI changes.
constexpr uint32_t kEngineVersion = 0x00010000u;  // 0.1.0

// Live KCD2 build number, populated at engine startup from the WHGame.dll
// file version. Reported via kcdxInterface::runtimeGameVersion. Plugins
// compare against their compatibleGameVersions array.
extern uint32_t g_runtimeGameVersion;

// Walk <pluginsDir>/*/<*.dll> and <pluginsDir>/<*.dll>, read each one's
// kcdxPluginVersionData, validate compatibility, topologically sort by
// dependencies, and call kcdxPlugin_Preload + kcdxPlugin_Load in order.
//
// Plugins failing validation (wrong dataVersion, incompatible game version
// without AddressLibrary flag, missing required dependency, cycles) are
// logged and skipped; other plugins still load.
//
// Safe to call once at startup, after config::LoadAllConfigs has run.
void DiscoverAndLoad(const std::wstring& pluginsDir);

// Look up a loaded plugin by stable name. Returns null on miss.
const LoadedPlugin* FindByName(const char* name);

// Get a plugin's handle by stable name. Returns kcdxInvalidPluginHandle on miss.
kcdxPluginHandle HandleOf(const char* name);

// Get the kcdxInterface pointer that's passed to plugin entry points.
// Populated by DiscoverAndLoad. Owned by the engine.
const kcdxInterface* GetEngineInterface();

}  // namespace kcdx::plugins
