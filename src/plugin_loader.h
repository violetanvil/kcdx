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
                                  //   (in transition), this is the plugin
                                  //   name WITHIN its
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
                                  //   (in transition).
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

    std::vector<std::string> supports;
                                  // Game-version compatibility patterns, parsed from
                                  //   [plugin] supports = ["1.5*", ...]. The UNIFIED
                                  //   <supports> model shared with pak mods (mod.manifest
                                  //   <supports>) — see docs/mod-loader-absorb.md "Version
                                  //   gate UNIFICATION". Each pattern is string-compared
                                  //   against g_runtimeGameVersionString (wh_sys_version,
                                  //   e.g. "1.5.5"): a TRAILING '*' is a PREFIX wildcard
                                  //   ("1.5*" matches "1.5", "1.5.5", "1.5.1164953");
                                  //   no '*' = exact string match. EMPTY (key absent) =
                                  //   "any game version" = version-independent by absence
                                  //   (the migrated meaning of the old version_independent
                                  //   flag). Evaluated by
                                  //   version_compat::DecideGameVersionCompatString in
                                  //   ValidateManifest.

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

    // Load-order author hints (parsed from the per-plugin [load_order] table's
    // zone + priority keys). Surface the plugin's preferred placement to the
    // launcher; user can override via kcdx-engine/load_order.toml.
    //
    // NOTE: these internal fields keep the names defaultPosition /
    // defaultPriority (their consumers in load_order.cpp read them unchanged);
    // only the TOML keys the parser reads FROM were renamed (Phase-7 zone
    // rework: [plugin].default_position/_priority -> [load_order].zone/priority).
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

    // Per-plugin posture for the per-version survival check: what the apply
    // pass does when a function this plugin targets has CHANGED in the running
    // game binary (its on-disk [rva,rva+length) bytes no longer match the
    // verified content_hash the reference DB recorded). Parsed from
    // [plugin].on_changed_function.
    //
    //   WarnAndTry   — proceed with the binding anyway, emitting a warning log
    //                  line in author terms (the DEFAULT when the key is absent).
    //   RefuseEntry  — skip the affected binding with a teaching error; other
    //                  bindings in the same plugin still apply (intra-plugin
    //                  failure isolation).
    //
    // The pass RECORDS this posture alongside each entry's survival result; the
    // ACTUAL apply-time enforcement is wired in a later step. An unknown string
    // is a HARD manifest rejection (fail loud — not a silent default-to-warn).
    enum class OnChangedFunction : uint8_t {
        WarnAndTry  = 0,  // default
        RefuseEntry = 1,
    };
    OnChangedFunction onChangedFunction = OnChangedFunction::WarnAndTry;

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
                                                  // (in transition; resolver
                                                  // wiring lands in subsequent
                                                  // steps).
                                                  // 0.1.1 — kcdxInterface gained
                                                  // ResolveSymbolAs +
                                                  // ResolveAddressByNameAs
                                                  // (append-only; gates the new
                                                  // layout, fixed-offset ABI)

// Live KCD2 build number, populated at engine startup from the WHGame.dll
// file version. Reported via kcdxInterface::runtimeGameVersion. (The plugin
// game-version gate now compares the STRING g_runtimeGameVersionString below
// against each plugin's `supports` patterns; this integer remains the ABI
// field plugins read via kcdxInterface::runtimeGameVersion.)
extern uint32_t g_runtimeGameVersion;

// Live KCD2 version STRING, populated at engine startup from
// wh_sys_version in <game-root>/system.cfg (the source the vanilla mod
// version gate compares against — see docs/mod-loader-absorb.md "Version
// gate UNIFICATION"). This is the value the unified <supports> string-
// prefix-wildcard gate (version_compat::DecideGameVersionCompatString)
// matches mod/plugin `supports` patterns against. Empty "" if system.cfg is
// absent/unreadable or has no wh_sys_version line — the gate then yields
// UnknownGameVersion and the caller loads anyway with a WARN (graceful-
// degrade, mirroring the integer path's "couldn't determine version,
// loading anyway"). Set alongside g_runtimeGameVersion at the same init
// point (dllmain.cpp worker thread, after WaitForGameDll).
extern std::string g_runtimeGameVersionString;

// Detect the running KCD2 build by reading kcd_launcher.log's build header,
// falling back to WHGame.dll's VS_VERSIONINFO. Returns 0 if neither source
// yields a version (logged WARN inside). Requires ONLY that WHGame.dll is
// MAPPED (GetModuleHandleW("WHGame.dll") must be non-null) — no engine init.
// Called once at WHGame-mapped time (ctx B, right after WaitForGameDll) by the
// worker thread; the caller stores the result into g_runtimeGameVersion. The
// per-plugin version-compat gate in DiscoverAndLoad then READS that value.
uint32_t DetectRuntimeGameVersion();

// Detect the running KCD2 version STRING by reading the `wh_sys_version`
// setting from <game-root>/system.cfg (located via paths::GameRootDirPath()).
// system.cfg is a CryEngine cfg text file: lines of `name = value` or
// `name=value`, value optionally double-quoted. The lookup is
// case-insensitive on the key, tolerates whitespace around '=', and strips
// surrounding double quotes from the value. Returns the value string (e.g.
// "1.5.5"), or "" if system.cfg is absent/unreadable or has no
// wh_sys_version line (logged WARN naming system.cfg — graceful-degrade, NOT
// a hard fail; mirrors DetectRuntimeGameVersion's "loading anyway" behavior).
// Called once at WHGame-mapped time (ctx B, right after WaitForGameDll)
// alongside DetectRuntimeGameVersion; the caller stores the result into
// g_runtimeGameVersionString. Does NOT require WHGame mapped — it reads a
// file — but is co-located with the integer detect for one version-detect
// site.
std::string DetectRuntimeGameVersionString();

// Extract the value of `key` from CryEngine cfg text (the body of a
// system.cfg / *.cfg file passed as one string). Scans line by line for
// `key = value` / `key=value` (case-insensitive on the key; whitespace
// around '=' tolerated; a leading-'--' commented line is ignored), strips
// surrounding double quotes from the value, and returns it. Returns "" if
// the key is not present. Pure string function (no file I/O) so it is
// unit-testable from a literal cfg string — DetectRuntimeGameVersionString
// reads system.cfg into a string and calls this with "wh_sys_version".
std::string ExtractCfgValue(const std::string& cfgText, const char* key);

// Walk plugins/ for kcdx.toml files (recursively, kcdx.toml marks a plugin
// folder, do-not-descend-into-claimed-folders rule), parse [plugin] metadata
// from each, validate compatibility, topologically sort by dependencies, then
// LoadLibraryW each plugin's DLL (if any) in order and call kcdxPlugin_Preload
// + kcdxPlugin_Load.
//
// Plugins failing validation (missing required name, game version not matched
// by any `supports` pattern, missing required dependency, cycles) are
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
// model). Same invalid-handle discipline as NameForHandle. An empty
// result is the in-progress namespace refactor's "legacy 1-dot row" tier
// (the corpus today; a later step populates [plugin].author across the
// manifests), which the resolver
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
