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
    std::string name;             // STABLE PLUGIN ID. Under the 2-dot
                                  //   <author>.<plugin>.<bare> namespace model
                                  //   (naming-namespaces.md, in transition),
                                  //   this is the plugin name WITHIN its
                                  //   author's namespace; the full
                                  //   shared-namespace prefix is
                                  //   "<author>.<name>" — see `author` below.
                                  //   Required. Must be unique within its
                                  //   author's namespace once the 2-dot model
                                  //   is wired through (subsequent step).
                                  //   Today the resolver still treats `name`
                                  //   itself as the namespace prefix (1-dot).
    std::string displayName;      // optional; for UI. Defaults to `name`.
    std::string author;           // Namespace prefix under the 2-dot
                                  //   <author>.<plugin>.<bare> model
                                  //   (naming-namespaces.md, in transition).
                                  //   The engine composes "<author>.<name>" as
                                  //   the plugin's identity in cross-plugin
                                  //   shared namespaces (hook/byte targets,
                                  //   kcdx.code exports, publish/on events,
                                  //   cosave records, asset overlays).
                                  //   Reserved author segment 'kcdx' belongs
                                  //   to engine-seed names (1-dot
                                  //   <kcdx>.<seedname>). Optional today; a
                                  //   subsequent step adds the validator that
                                  //   requires it under the 2-dot model.
                                  //   Display semantics ("Author Name") still
                                  //   hold pending that step.
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

    // The after-game C++ slot is NOT a separate DLL file. A C++ plugin is
    // ONE compiled DLL with optional exports (kcdxPlugin_Preload /
    // kcdxPlugin_Load / kcdxPlugin_PostGameLoad), mirroring how Preload and
    // Load coexist on the same module. The after-game work is reached via
    // an optional kcdxPlugin_PostGameLoad export ON THE EXISTING
    // dllEntrypointRel module — resolved at discovery into
    // LoadedPlugin::postGameLoadFn, dispatched by RunPostGameLoad in the
    // after_game phase at the plugin's load-order priority (the C++ mirror
    // of luaAfterEntrypointsRel). There is therefore no separate "dll_after"
    // path field; the speculative one added during schema scaffolding was
    // removed once the export model (matching Preload/Load) was settled.

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
    kcdxPlugin_Preload_t      preloadFn      = nullptr;
    kcdxPlugin_Load_t         loadFn         = nullptr;
    // Optional after-game export — the C++ mirror of lua_after. Null when
    // the plugin's DLL doesn't export kcdxPlugin_PostGameLoad (optional,
    // like Preload). Resolved off the same module as preloadFn/loadFn;
    // dispatched by RunPostGameLoad in load-order priority.
    kcdxPlugin_PostGameLoad_t postGameLoadFn = nullptr;

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
constexpr uint32_t kEngineVersion = 0x00010200u;  // 0.1.2 — kcdxPluginInfo.author
                                                  // gains its namespace-prefix
                                                  // semantic under the 2-dot
                                                  // <author>.<plugin>.<bare> model
                                                  // (naming-namespaces.md, in
                                                  // transition; resolver wiring
                                                  // lands in subsequent steps).
                                                  // 0.1.1 — kcdxInterface gained
                                                  // ResolveSymbolAs +
                                                  // ResolveAddressByNameAs
                                                  // (append-only; gates the new
                                                  // layout, AP11)

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

// Convert a plugin handle to the registering plugin's [plugin].name (the
// namespace prefix the symbol / author-target resolvers expect). The handle
// is the index into g_plugins (assigned in DiscoverAndLoad); guards on
// kcdxInvalidPluginHandle + out-of-range index. An invalid / unknown handle
// yields the empty string — the anonymous (engine-seed-only, no self tier)
// path, which is correct-but-narrower, never a mis-attribution to the wrong
// owner. Returned reference is stable for the process lifetime (backed by
// LoadedPlugin::manifest.name, which is append-only).
const std::string& NameForHandle(kcdxPluginHandle owner);

// Convert a plugin handle to the registering plugin's [plugin].author (the
// leading namespace component under the 2-dot <author>.<plugin>.<bare>
// model — see naming-namespaces.md). Same invalid-handle discipline as
// NameForHandle. An empty result is the in-progress namespace refactor's
// "legacy 1-dot row" tier (the corpus today; step 6 of the refactor
// populates [plugin].author across the manifests), which the resolver
// tolerates by walking the legacy 1-dot scope under (plugin, name).
const std::string& AuthorForHandle(kcdxPluginHandle owner);

// Get the kcdxInterface pointer that's passed to plugin entry points.
// Populated by DiscoverAndLoad. Owned by the engine.
const kcdxInterface* GetEngineInterface();

// Fire each enabled C++ plugin's optional kcdxPlugin_PostGameLoad export —
// the C++ mirror of lua_after. Runs in the after_game phase at the first
// update tick, AFTER all before-game work is applied and BEFORE
// kcdxMessage_InputLoaded. Plugins are dispatched in LOAD-ORDER PRIORITY
// (priority asc, then name asc — the SAME ordering RunAfterEntrypoints uses
// for lua_after), honoring the load_order.toml enabled gate, each call under
// crash_guard. A plugin without the export (null postGameLoadFn) is skipped.
// `api` is the engine interface published to plugins (GetEngineInterface()).
void RunPostGameLoad(const kcdxInterface* api);

}  // namespace kcdx::plugins
