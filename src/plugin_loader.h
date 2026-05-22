#pragma once
#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>
#include <vector>
#include <windows.h>

#include "kcdx/Interfaces.h"

namespace kcdx::plugins {

// One declared dependency from a plugin's kcdx.toml [[plugin.dependencies]]
// array.
struct ManifestDependency {
    std::string name;          // stable name of the other plugin
    uint32_t    minVersion = 0;  // packed integer; their version must be >= this
    bool        optional   = false;
};

// PluginManifest — metadata parsed from a plugin's kcdx.toml [plugin] and
// [entrypoints] sections. Replaces the old kcdxPluginVersionData C struct
// (which lived inside the DLL's data segment); kcdx.toml is now the single
// source of truth for plugin identity.
//
// One PluginManifest exists for every kcdx.toml the discovery walk found,
// regardless of whether the plugin ships a DLL.
struct PluginManifest {
    // Identity — populated from [plugin] table.
    std::string name;             // STABLE PLUGIN ID. Convention: "author.mod-name".
                                  //   Required. Must be unique across all loaded plugins.
    std::string displayName;      // optional; for UI. Defaults to `name`.
    std::string author;           // optional but recommended
    std::string description;      // optional
    std::string url;              // optional
    std::string supportEmail;     // optional

    uint32_t version = 0;         // packed semver. e.g. "1.2.3" parses to 0x00010203.
                                  //   Optional; defaults 0x00000000.
    uint32_t kcdxMinVersion = 0;  // packed semver; engine refuses to load
                                  //   if kcdx is older. Optional; defaults 0
                                  //   (= "any kcdx").

    std::vector<uint32_t> compatibleGameVersions;
                                  // KCD2 build numbers this plugin tested against.
                                  //   Empty == "any game version" — valid only when
                                  //   versionIndependent is true.
    bool versionIndependent = false;
                                  // Set true if the plugin uses kcdx::ResolveAddress
                                  //   (Address Library) for runtime offset lookups
                                  //   and doesn't pin to a specific game build.

    std::vector<ManifestDependency> dependencies;

    // For test-suite plugins (those with [kcdx] test_suite_only = true):
    // the matrix row IDs this plugin promises to report. Used by the
    // aggregator to track "PENDING" vs "reported." Empty = no
    // expectation, plugin is included in count but can report any name.
    //
    // Example: test_names = ["CAP-05", "CAP-11"]
    std::vector<std::string> testNames;

    // Set to true by config.cpp if [kcdx] test_suite_only = true in
    // this plugin's TOML.
    bool testSuiteOnly = false;

    // Log level threshold for this plugin's per-plugin log. Calls to
    // api->Log(self, level, msg) at a level BELOW this are dropped
    // before formatting. Default = Info (0). See kcdxLog_* enum in
    // Interfaces.h for the levels.
    //
    // Encoded as the kcdxLog_* enum value (info=0, warn=1, error=2,
    // debug=3). The special value 4 means "off" (drop everything).
    // Parsed from kcdx.toml [plugin] log_level = "debug|info|warn|error|off".
    uint32_t logLevel = 0;  // == kcdxLog_Info

    // Load-order author hints (parsed from [plugin] default_position +
    // default_priority). Surface the plugin's preferred placement to the
    // launcher; user can override via kcdx-engine/load_order.toml.
    //
    // defaultPosition is one of "before_game" / "after_game" (or empty,
    // meaning "let kcdx derive from capabilities"). When empty, kcdx
    // picks before_game iff the plugin has zero entries requiring
    // after_game (mid-hooks, lua-callback hooks, byte-detour hooks,
    // trampolines, commands, events). Source::Engine plugins default
    // to before_game; Source::User defaults to after_game when
    // capability-flexible.
    //
    // defaultPriority is 0..100; 0 = earliest in zone, 100 = latest.
    // Default 50 (middle of the zone). Sparse range gives author /
    // user room to insert "definitely before X" or "definitely after Y"
    // without renumbering.
    //
    // These are AUTHOR HINTS only — load_order.toml overrides win when
    // present. See docs/load-order.md for the full model.
    std::string defaultPosition;        // "before_game" / "after_game" / "" (derive)
    int         defaultPriority = 50;   // 0..100, lower = earlier

    // Entrypoints — populated from [entrypoints] table (all optional).
    std::string dllEntrypointRel; // Relative path from plugin folder. If empty,
                                  //   kcdx auto-discovers: if exactly one *.dll
                                  //   exists in the plugin folder root, that's it.
                                  //   Multi-DLL plugins must declare this explicitly.

    // [entrypoints] lua = "plugin.lua" OR lua = ["plugin.lua", "extras.lua"].
    // Relative paths from the plugin folder. Run in declared order at the
    // plugin's slot in the unified load order, after the Lua VM is up (see
    // hooks.cpp first-update-tick orchestration). This is the BEFORE-or-
    // default slot: it runs in the plugin's declared zone. Each file is
    // loaded via luaL_loadfile + lua_pcall under crash_guard so a faulty
    // plugin.lua can't take down the engine or other plugins. Empty = no
    // Lua entrypoint.
    std::vector<std::string> luaEntrypointsRel;

    // [entrypoints] lua_after = "after.lua" OR an array — the OPTIONAL
    // after-game Lua slot. Same shape + per-file load discipline as
    // luaEntrypointsRel, but these files run in the AFTER_GAME phase at the
    // plugin's load-order priority, regardless of the plugin's declared
    // zone. The phase split is DECLARED and VISIBLE: a plugin doing both
    // before-game and after-game work declares `lua` (its before/default
    // slot) AND `lua_after` (its after-game slot); each runs at the
    // plugin's single load-order priority IN ITS PHASE. lua_after is run
    // by lua_plugin_loader::RunAfterEntrypoints after ApplyZone(AfterGame)
    // (so all before-work is live) and before kcdxMessage_InputLoaded.
    // Empty = no after-game Lua entrypoint. See restructure-plan.md
    // (per-entry-zone execution model).
    std::vector<std::string> luaAfterEntrypointsRel;

    // [entrypoints] dll_after = "bin/post.dll" — the OPTIONAL after-game DLL
    // slot (kcdxPlugin_PostGameLoad). PARSED into this field now for schema
    // completeness, but its dispatch is NOT wired in this step — the
    // kcdxPlugin_PostGameLoad C++ export + its invocation are a SEPARATE
    // later step. TODO(step-4): wire dll_after dispatch (load the DLL's
    // kcdxPlugin_PostGameLoad export, fire it in the after_game phase at
    // the plugin's priority). Until then this field is recorded but unused.
    std::string dllAfterEntrypointRel;

    // Location — populated by the discovery walk.
    std::filesystem::path folderPath; // Absolute path of the plugin's install folder
    std::filesystem::path tomlPath;   // Absolute path of the kcdx.toml that yielded this manifest
};

// One discovered + loaded plugin.
struct LoadedPlugin {
    PluginManifest   manifest;           // Metadata; always populated
    std::string      filePath;           // Absolute path to the .dll (empty for TOML-only plugins)
    std::string      folderName;         // Plugin folder name (for log diagnostics)
    std::wstring     folderPath;         // Absolute path of plugin install dir (for GetPluginPath)
    HMODULE          module = nullptr;   // null for TOML-only plugins (no DLL)
    kcdxPluginHandle handle = kcdxInvalidPluginHandle;

    // Filled in during discovery, cleared after the load wave completes.
    kcdxPlugin_Preload_t preloadFn = nullptr;
    kcdxPlugin_Load_t    loadFn    = nullptr;

    // Result of the load wave. Default false; set true if loadFn returned true
    // (or if the plugin has no loadFn but is otherwise valid).
    bool loaded = false;

    // Cached read-only snapshot returned by Thunk_GetPluginInfo. Lazy-built
    // on first GetPluginInfo call. Pointers inside point into this struct's
    // own manifest.* std::string fields (stable for LoadedPlugin's lifetime,
    // i.e. the process). Held by unique_ptr so address stays put even when
    // g_plugins is grown by push_back during discovery.
    mutable std::unique_ptr<kcdxPluginInfo> infoCache;
};

// Engine state — populated by DiscoverAndLoad, read by the kcdxInterface impl.
extern std::vector<LoadedPlugin> g_plugins;

// Manifests parsed from each plugin's kcdx.toml during config::LoadAllConfigs.
// DiscoverAndLoad consumes this collection. Lives for the process lifetime.
extern std::vector<PluginManifest> g_manifests;

// Engine version we report to plugins via kcdxInterface::kcdxVersion.
// Bumped when the public ABI changes.
constexpr uint32_t kEngineVersion = 0x00010000u;  // 0.1.0

// Live KCD2 build number, populated at engine startup from the WHGame.dll
// file version. Reported via kcdxInterface::runtimeGameVersion. Plugins
// compare against their compatibleGameVersions array.
extern uint32_t g_runtimeGameVersion;

// Walk plugins/ for kcdx.toml files (recursively, kcdx.toml marks a plugin
// folder, do-not-descend-into-claimed-folders rule), parse [plugin] metadata
// from each, validate compatibility, topologically sort by dependencies, then
// LoadLibraryW each plugin's DLL (if any) in order and call kcdxPlugin_Preload
// + kcdxPlugin_Load.
//
// Plugins failing validation (missing required name, incompatible game
// version without Address Library, missing required dependency, cycles) are
// logged and skipped; other plugins still load.
//
// Safe to call once at startup, after config::LoadAllConfigs has run the
// patch/hook/trampoline parsing pass over the same TOML files.
void DiscoverAndLoad(const std::wstring& pluginsDir);

// Look up a loaded plugin by stable name. Returns null on miss.
const LoadedPlugin* FindByName(const char* name);

// Get a plugin's handle by stable name. Returns kcdxInvalidPluginHandle on miss.
kcdxPluginHandle HandleOf(const char* name);

// Get the kcdxInterface pointer that's passed to plugin entry points.
// Populated by DiscoverAndLoad. Owned by the engine.
const kcdxInterface* GetEngineInterface();

}  // namespace kcdx::plugins
