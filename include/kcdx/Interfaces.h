// kcdx/Interfaces.h — public plugin API
//
// SKSE-shaped extender interface for Kingdom Come: Deliverance II.
// Plugin authors:
//   1. Ship a `kcdx.toml` in your plugin folder. The [plugin] table
//      carries identity, version, compatibility, and dependencies.
//      Required for every plugin (C++ or pure-TOML).
//   2. (C++ plugins only) #include this header.
//   3. (C++ plugins only) Export `kcdxPlugin_Load` as a function the
//      engine calls at load time, after every plugin's metadata has
//      been parsed and the dependency graph topologically sorted.
//   4. (C++ plugins only) Optionally export `kcdxPlugin_Preload` for
//      early-phase setup.
//   5. (C++ plugins only) Optionally export `kcdxPlugin_PostGameLoad` to
//      run after-game work at load-order priority — the C++ mirror of the
//      Lua `lua_after` entrypoint slot. Fires in the after_game phase,
//      after all before-game work is applied, before kcdxMessage_InputLoaded.
//
// See the kcdx documentation for the full design spec, schema,
// and worked examples.

#pragma once

#include <cstdint>

#ifdef __cplusplus
extern "C" {
#endif

// -----------------------------------------------------------------------------
// Basic types
// -----------------------------------------------------------------------------

// Opaque handle assigned to each plugin at load time. Used as the "self" ID
// in every API that takes a sender/owner. Stable for the lifetime of the
// process; never reused. Plugins can fetch their own handle via
// kcdxInterface::GetPluginHandle(yourName).
typedef uint32_t kcdxPluginHandle;

// Sentinel for "no plugin" / lookup miss.
#define kcdxInvalidPluginHandle ((kcdxPluginHandle)0xFFFFFFFFu)

// -----------------------------------------------------------------------------
// Plugin metadata — lives in kcdx.toml, NOT in the DLL
// -----------------------------------------------------------------------------
//
// Every plugin (C++ DLL or pure-TOML) declares its identity in a kcdx.toml
// file at its plugin folder root. The engine reads this BEFORE loading any
// DLL — name, version, dependencies, and compatibility are decided up front
// so the dependency topo-sort runs against parsed metadata, not against
// late-bound symbols.
//
// Schema for kcdx.toml [plugin]:
//
//     [plugin]
//     name              = "author.mod-name"   # required. stable ID, unique across loaded plugins.
//     display_name      = "My Mod"            # optional; for UI. defaults to `name`.
//     author            = "Author Name"
//     version           = "1.0.0"             # semver. parsed to packed integer 0xMMmmpp00.
//     description       = "..."
//     url               = "https://..."
//     support_email     = "..."
//
//     kcdx_min_version  = "0.1.0"             # engine refuses to load if kcdx is older.
//                                              # default 0 (= "any kcdx").
//
//     # KCD2 builds this plugin has been tested against. Encoded by
//     # kcdxMakeGameVersion(major, minor, build) — see end of this header.
//     # Default empty == "any version", VALID ONLY if version_independent = true.
//     compatible_game_versions = [
//         "1.5.1164953",
//     ]
//
//     # Set true if your plugin uses kcdx::ResolveAddress (Address Library)
//     # and doesn't pin to a specific game build. Required if
//     # compatible_game_versions is empty.
//     version_independent = false
//
//     # Dependencies on other plugins. Loader topo-sorts these.
//     [[plugin.dependencies]]
//     name        = "other-author.lib"
//     min_version = "0.3.0"
//     optional    = false        # if true, load this plugin even if dep missing
//
//     [[plugin.dependencies]]
//     name        = "yet.another"
//     min_version = "1.0.0"
//     optional    = true
//
// Schema for kcdx.toml [entrypoints] (all optional):
//
//     [entrypoints]
//     dll = "bin/my-plugin.dll"   # relative to plugin folder. If omitted,
//                                  # kcdx auto-discovers: exactly one *.dll
//                                  # in the plugin folder root. Multi-DLL
//                                  # plugins MUST set this explicitly.

// Lightweight read-only snapshot of a plugin's identity. Returned by
// kcdxInterface::GetPluginInfo. All char* fields are owned by the engine
// and live for the process lifetime. Empty string (not null) for fields
// the plugin did not declare.
typedef struct kcdxPluginInfo {
    const char* name;              // Stable plugin ID. Never null. Under the
                                   // 2-dot <author>.<plugin>.<bare> namespace
                                   // model (in transition),
                                   // this is the plugin name WITHIN its author's
                                   // namespace (the second dot-segment); the
                                   // full shared-namespace prefix is
                                   // "<author>.<name>" — see `author` below.
                                   // Current resolver still uses bare `name`
                                   // as the namespace prefix (1-dot model);
                                   // subsequent refactor steps wire the 2-dot
                                   // form into the resolver and validator.
    const char* displayName;       // UI name. Never null; falls back to `name` if not declared.
    const char* author;            // Namespace prefix under the 2-dot
                                   // <author>.<plugin>.<bare> model
                                   // (in transition).
                                   // The engine composes "<author>.<name>" as
                                   // the plugin's identity in cross-plugin
                                   // shared namespaces (hook/byte targets,
                                   // `kcdx.code{export=}` symbols, publish/on
                                   // events, cosave records, asset overlays).
                                   // The author segment 'kcdx' is reserved for
                                   // engine-seed names (1-dot <kcdx>.<seedname>).
                                   // Never null; empty string when not declared.
                                   // Display semantics ("Author Name" for UI)
                                   // still hold today — the namespace-role
                                   // transition lands across subsequent steps.
    const char* description;
    const char* url;
    const char* supportEmail;
    uint32_t    version;           // Packed semver (0xMMmmpp00)
    uint32_t    kcdxMinVersion;
    uint32_t    runtimeCompatibleGameVersion;  // The matched entry from compatible_game_versions,
                                                // or 0 if version_independent
    int         versionIndependent;            // 0/1

    // --- APPEND-ONLY BELOW THIS LINE ---------------------------------------
    // New fields MUST be appended at the END of this struct, never inserted
    // in the middle: inserting shifts every subsequent field's offset, and
    // any plugin DLL compiled against the older header then reads through
    // the wrong offset → ACCESS_VIOLATION.
    // (Same ABI discipline as kcdxInterface / kcdxSerializationInterface;
    // pre-positioned here ahead of the 2-dot namespace refactor adding
    // namespace-aware fields.)
} kcdxPluginInfo;

// -----------------------------------------------------------------------------
// kcdxInterface — the root API a plugin receives at load
// -----------------------------------------------------------------------------

// Sub-interface identifiers passed to kcdxInterface::QueryInterface.
//
// IDs are APPEND-ONLY: a new sub-interface gets the next free integer at the
// END of the enum and is never renumbered. A plugin DLL compiled against the
// old header passes the old integer; the engine's QueryInterface switch
// recognises it at the same slot. Inserting a value in the middle would
// renumber every subsequent ID and break every previously-compiled plugin's
// QueryInterface call by silently routing it to the wrong interface.
enum kcdxInterfaceID {
    kcdxInterface_Messaging      = 1,
    kcdxInterface_Trampoline     = 2,
    kcdxInterface_Task           = 3,
    kcdxInterface_Scripting      = 4,
    kcdxInterface_Serialization  = 5,
    kcdxInterface_Memory         = 6,  // DLL-facing memory I/O
    kcdxInterface_Console        = 7,  // IConsole::AddCommand thunk
    kcdxInterface_Hook           = 8,  // C++ kcdx.hook mirror
    kcdxInterface_Bytes          = 9,  // C++ kcdx.bytes mirror
    kcdxInterface_Declare        = 10, // C++ kcdx.declare / kcdx.declared mirror
    kcdxInterface_Assets         = 11, // C++ kcdx.assets.* mirror
};

// Log levels passed to kcdxInterface::Log. Match the severities the engine
// itself uses.
//
// Engine routes plugin Log calls as follows (always tagged with the
// plugin's stable name as the SOURCE field and `category` as the
// CATEGORY field):
//   Info / Warn / Error    — engine log AND plugin's own log; always on.
//   Debug / Trace          — plugin's own log only when dev mode is on
//                            AND `category` passes dev_categories filter.
enum kcdxLogLevel {
    kcdxLog_Trace = 0,  // ultra-verbose; dev-mode only
    kcdxLog_Debug = 1,  // verbose;       dev-mode only
    kcdxLog_Info  = 2,  // always-on
    kcdxLog_Warn  = 3,  // always-on
    kcdxLog_Error = 4,  // always-on
};

// Per-entry record returned by kcdxInterface::GetConflictReport. Describes
// one byte-patch entry (kcdx.bytes), hook entry (kcdx.hook), or higher-level
// entry that overlaps a queried
// target address. For kcdx.hook (the new function-interception surface),
// this reports BOTH the live-chain winners (applied != 0) AND the
// CanCoexist-rejected losers (applied == 0) at the target.
//
// Scope note: mid-function hook conflicts (kcdx.hook mode=mid) are NOT reported
// (mid hooks reject via sole-ownership, not the chain's CanCoexist path;
// the legacy path never reported mid conflicts either — same contract).
//
// All char* fields are owned by the engine and remain valid for the
// process lifetime.
enum kcdxConflictEntryKind {
    kcdxConflictEntryKind_Patch = 0,
    kcdxConflictEntryKind_Hook  = 1,
};

typedef struct kcdxConflictEntry {
    const char* name;       // entry's TOML name (e.g. "outfit_swap_in_combat")
    int         priority;   // resolved load-order position (lower = earlier)
    int         kind;       // kcdxConflictEntryKind_Patch or _Hook
    int         applied;    // 0 = aborted (lost a conflict), nonzero = applied
} kcdxConflictEntry;

// Root accessor. The engine passes a const pointer to one of these to your
// kcdxPlugin_Preload, kcdxPlugin_Load, and kcdxPlugin_PostGameLoad
// functions. Treat as read-only.
//
// All function pointers are non-null on a valid kcdxInterface*. Sub-interfaces
// fetched via QueryInterface may be null if the requested interface or version
// is not implemented in the current kcdx engine.
typedef struct kcdxInterface {
    uint32_t kcdxVersion;              // Engine version (e.g. 0x00010000 = 0.1.0)
    uint32_t runtimeGameVersion;       // Live KCD2 build the engine is hooked into

    // Fetch a typed sub-interface. Returns null if the interface ID is unknown
    // or the requested version is newer than the engine supports. The returned
    // pointer is owned by the engine; do not free it.
    void* (*QueryInterface)(uint32_t interfaceID, uint32_t version);

    // Look up another plugin by stable name. Returns null if not loaded.
    // The returned pointer is owned by the engine and remains valid for the
    // lifetime of the process.
    const kcdxPluginInfo* (*GetPluginInfo)(const char* name);

    // Get a plugin's handle by stable name. Returns kcdxInvalidPluginHandle on miss.
    kcdxPluginHandle (*GetPluginHandle)(const char* name);

    // List all loaded plugin handles. Fills `out` up to `cap` entries; returns
    // the actual count. Pass out=nullptr, cap=0 to query the count only.
    uint32_t (*EnumeratePlugins)(kcdxPluginHandle* out, uint32_t cap);

    // Look up a known address by Address Library ID. Returns 0 (== nullptr) if
    // the ID is unknown for the running game version, or if the Address
    // Library has it as `removed` for this version.
    uintptr_t (*ResolveAddress)(uint64_t id);

    // Look up a cross-plugin symbol by name. Symbols are registered by
    // code exports (kcdx.code{export=...}) and hook entries (kcdx.hook{export=...}) with an `export = "..."`
    // field — the cross-plugin symbol table. Returns
    // 0 (== nullptr) if the symbol is not registered.
    //
    // The returned address is whatever was registered (a trampoline
    // base, a hook's call-original entry, etc.). The caller is
    // responsible for knowing the ABI of the resolved address.
    //
    // Lookup is constant-time over a hash map and safe to call from
    // any plugin context after kcdxMessage_InputLoaded fires (the
    // symbol table is populated during apply, which runs before
    // kInputLoaded).
    uintptr_t (*ResolveSymbol)(const char* name);

    // Write a categorized line to the kcdx logging system. Lines are
    // tagged with this plugin's stable name (as SOURCE) and `category`
    // (as CATEGORY) and routed by the engine to:
    //
    //   - this plugin's per-session log at
    //     <plugins>/<folder>/logs/<folder>_<timestamp>.log
    //   - the engine's per-session log at
    //     <kcdx-engine>/logs/kcdx_<timestamp>.log (Info/Warn/Error only)
    //   - the dev log at <kcdx-engine>/logs/kcdx-dev_<timestamp>.log
    //     (when engine.toml sets dev_mode = true AND `category` passes
    //      the dev_categories filter)
    //
    // `category` is a short identifier the plugin chooses for grouping
    // (e.g. "INIT", "HOOK", "TASK"). Pass null and the engine
    // substitutes "PLUGIN". Categories are stable strings, not enums,
    // so they're greppable.
    //
    // `msg` should be a UTF-8 null-terminated C string. Newlines are
    // added by the engine — don't append your own.
    //
    // Safe to call from any thread.
    void (*Log)(kcdxPluginHandle self, uint32_t level,
                const char* category, const char* msg);

    // Return the absolute filesystem path of a plugin's install folder.
    // For self-introspection, pass your own handle (the one returned by
    // GetPluginHandle in your Plugin_Load). Returns null for an unknown
    // handle. The returned pointer is owned by the engine and remains
    // valid for the process lifetime.
    //
    // Use this to address files your plugin ships alongside its DLL —
    // sub-DLLs you LoadLibraryW dynamically, config files (.ini, .toml,
    // .json, .lua), models, textures, or any other static asset that
    // lives under your plugin's install folder. kcdx does NOT scan or
    // load any of these for you; you own that. Convention is to keep
    // them in a subfolder (e.g. `data/`, `extras/`) so they don't get
    // mistaken for a kcdx.toml-marked plugin.
    //
    // Example (C++):
    //   const wchar_t* root = api->GetPluginPath(g_self);
    //   std::wstring cfg = std::wstring(root) + L"\\data\\config.ini";
    //   FILE* fp = _wfopen(cfg.c_str(), L"r");
    //
    // Note: kcdx's recursive discovery walk STOPS once it finds a
    // kcdx.toml in a folder. Subfolders of a plugin folder are
    // invisible to discovery — they're yours to use however you like.
    const wchar_t* (*GetPluginPath)(kcdxPluginHandle handle);

    // Report a regression-test result. testName should be the matrix row
    // ID (e.g. "CAP-01"); reason is a short freeform explanation of
    // pass/fail. Last call for a given testName wins (a plugin may
    // re-report on later lifecycle messages).
    //
    // No-op when dev mode is off — production users never see test-suite
    // output. The aggregator emits a "suite: X/Y passing as of <msg>"
    // roll-up to kcdx.log on each engine lifecycle message.
    //
    // See kcdx/docs/dev-mode.md (Test suite section) for the contract +
    // kcdx/test-plugins/README.md for the matrix.
    void (*ReportTestResult)(kcdxPluginHandle self,
                             const char*      testName,
                             int              pass,    // 0 = fail, nonzero = pass
                             const char*      reason); // nullable

    // Conflict introspection: query which patch/hook entries resolve to a
    // given target address, and how the conflict engine handled them.
    // Used by test plugins (COMP-01/02/03 and friends) to assert that a
    // declared conflict was detected + resolved as expected.
    //
    // For byte-patch entries (kcdx.bytes) the "target" matches if the patch's write
    // range contains the queried address (write footprint covers
    // [patchVa, patchVa + replacement_len)).
    //
    // For hook entries (kcdx.hook) the "target" matches if the hook's resolved
    // function entry address equals the queried address.
    //
    // ALSO reports kcdx.hook (the new function-interception surface)
    // entries whose resolved target VA equals the queried address — both
    // the live-chain winners (applied != 0) AND the CanCoexist-rejected
    // losers (applied == 0). Mid-function hook conflicts (kcdx.hook mode=mid)
    // are NOT reported (mid hooks reject via sole-ownership, not the
    // chain's CanCoexist path; the legacy path never reported mid
    // conflicts either — same contract).
    //
    // Pass out=null, cap=0 to query the count only; the function returns
    // how many entries would have been written. With a real buffer, fills
    // up to cap entries (truncates if there are more).
    //
    // Order: entries are sorted by (priority asc, name asc) — the same
    // order kcdx uses for apply.
    uint32_t (*GetConflictReport)(uintptr_t              target,
                                  kcdxConflictEntry*     out,
                                  uint32_t               cap);

    // --- APPEND-ONLY BELOW THIS LINE ---------------------------------------
    // New function pointers MUST be appended at the END of this struct,
    // never inserted in the middle: inserting shifts every subsequent
    // pointer's offset, and any plugin DLL compiled against the older
    // header then calls through the wrong offset → ACCESS_VIOLATION.
    // (Learned the hard way: inserting ResolveAddressByName mid-struct
    // crashed every pre-built test plugin on load.)

    // Look up a known address by Address Library NAME (the human-readable
    // label, e.g. "lua_pcall"). Same resolution rules as ResolveAddress
    // (0 if unknown / wrong game_version / unverified). The C++ mirror of
    // the Lua `address_id = "name"` hook locator — names are friendlier
    // than numeric ids; numeric ids remain the stable cross-version ref.
    //
    // ANONYMOUS form: this overload carries no caller identity, so a BARE
    // name resolves on the engine-seed-only path (no self / no other-plugin
    // tier). Use ResolveAddressByNameAs (below) to get full self > engine >
    // other precedence; or pass an explicit "<plugin>.<name>" here, which is
    // unambiguous from any caller.
    uintptr_t (*ResolveAddressByName)(const char* name);

    // Resolve a cross-plugin SYMBOL with `owner` as the "self" tier — the
    // identity-carrying mirror of ResolveSymbol. Symbols resolve self >
    // other (there is NO engine-seed tier for symbols — the engine seed is
    // the Address Library, a separate surface). Pass YOUR OWN handle (the
    // one from GetPluginHandle in your Plugin_Load) as `owner` so a BARE
    // name resolves to your own kcdx.code{export=} / kcdx.hook{export=} symbols
    // first; an explicit "<plugin>.<name>" resolves directly regardless of
    // owner. Pass kcdxInvalidPluginHandle (or a handle the engine doesn't
    // know) to resolve anonymously (equivalent to ResolveSymbol). Returns
    // 0 if the symbol is not registered for that resolution.
    //
    // This closes the self-tier gap that ResolveSymbol alone cannot: under
    // the <pluginname>.<name> namespace model a plugin's own export is
    // stored as "<yourname>.<bare>", so a bare ResolveSymbol("bare") with no
    // owner misses your own export. ResolveSymbolAs threads the owner so the
    // self tier resolves it (self > engine > other precedence).
    uintptr_t (*ResolveSymbolAs)(kcdxPluginHandle owner, const char* name);

    // Resolve an author-target / Address Library NAME with `owner` as the
    // "self" tier — the identity-carrying mirror of ResolveAddressByName and
    // the C++ equivalent of the Lua `target = "<name>"` resolution path. A
    // BARE name resolves self > engine > other; an explicit "<plugin>.<name>"
    // resolves directly. Pass YOUR OWN handle as `owner`; pass
    // kcdxInvalidPluginHandle to resolve anonymously (engine-seed-only path,
    // equivalent to ResolveAddressByName). Returns 0 if unresolved.
    //
    // Closes the tracked C++ parity debt with the Lua target= path: a single
    // shared kcdxInterface is handed to every plugin, so ResolveAddressByName
    // alone has no per-call identity to read; this overload threads it.
    uintptr_t (*ResolveAddressByNameAs)(kcdxPluginHandle owner, const char* name);

    // Returns 1 if the calling thread is the engine-captured game main
    // thread — the thread that first wrote a non-null `lua_State` into the
    // engine's bootstrap latch. Returns 0 otherwise, AND returns 0 BEFORE
    // bootstrap (`g_gameMainThreadId == 0` is the "not yet captured"
    // sentinel; no real Windows thread has tid 0, so this is a clean
    // negative).
    //
    // Surfaced to plugins so a regression test can falsifiably assert the
    // "engine bootstrap classifier has bootstrapped" property — calling
    // this from a known-main-thread context (e.g. inside a `kcdx.on("ready")`
    // / kcdxMessage_LuaReady callback) returns 1 only if the chain
    // dispatcher's main-thread classifier has captured a real thread id.
    // A return of 0 from such a context is the chicken-and-egg dead-
    // classifier signature; see docs/known-issues/cap-59-fires...md §
    // Reframe 2026-05-29c for the bootstrap-loop mechanism.
    //
    // Cheap: one TLS read + one int compare, no lock.
    uint32_t (*IsGameMainThread)();
} kcdxInterface;

// -----------------------------------------------------------------------------
// kcdxMessagingInterface — pub/sub message bus between plugins and engine
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Messaging,
// kcdxMessagingInterface_Version).
//
// The engine fires lifecycle messages (kcdxMessage_PostLoad,
// kcdxMessage_PostLoadGame, etc.) with sender = null. Plugins broadcast their
// own messages with sender = their own stable name. Listeners filter by
// sender — null-sender listeners see only engine messages; specific-sender
// listeners see only messages from that plugin.

#define kcdxMessagingInterface_Version 1u

// One message delivered to an EventCallback. Pointer to this struct is
// valid only for the duration of the callback; copy any data you need to
// keep.
typedef struct kcdxMessage {
    const char* sender;        // Stable plugin name; null = engine-originated
    uint32_t    messageType;   // kcdxMessage_* catalog OR plugin-defined (>= 0x10000)
    const void* data;          // Message-specific payload, null if none
    uint32_t    dataLen;       // Payload byte length, 0 if data is null
} kcdxMessage;

typedef void (*kcdxMessagingCallback)(kcdxMessage* msg);

typedef struct kcdxMessagingInterface {
    // Subscribe `callback` to messages from `sender`. Pass sender = null to
    // receive engine-originated messages (the kcdxMessage_* lifecycle catalog).
    // Multiple listeners for the same sender are allowed; each gets a copy.
    // Returns true on success, false on invalid arguments (e.g. unknown listener
    // handle).
    bool (*RegisterListener)(kcdxPluginHandle listener,
                             const char* sender,
                             kcdxMessagingCallback callback);

    // Send a message. If receiver != null, only listeners that subscribed
    // specifically to `sender` will see it. If receiver == null, broadcast
    // to all listeners that subscribed to this sender.
    // Returns true if at least one listener fired.
    bool (*Dispatch)(kcdxPluginHandle sender,
                     uint32_t messageType,
                     const void* data,
                     uint32_t dataLen,
                     const char* receiver);
} kcdxMessagingInterface;

// Engine-originated message types. Plugin-defined message types should use
// values >= 0x10000 to avoid collision.
//
// All engine messages have sender == null in the kcdxMessage struct.
enum kcdxMessageType {
    // After every plugin's kcdxPlugin_Load returned. Plugin B can now look up
    // plugin A via GetPluginInfo / GetPluginHandle.
    kcdxMessage_PostLoad      = 1,

    // After every kMessage_PostLoad handler returned. The "plugin wave is
    // settled" moment — anything plugin B's PostLoad handler registered is
    // now safe to depend on.
    kcdxMessage_PostPostLoad  = 2,

    // After KCD2's input subsystem init, before the main menu appears.
    // Approximated by the first `update` tick.
    kcdxMessage_InputLoaded   = 3,

    // New game started, before the first cell loads.
    kcdxMessage_NewGame       = 4,

    // The engine is about to start a load — fires at C_SaveGameManager's
    // LoadGame_wrapper entry. `data` is currently NULL; use
    // kcdxMessage_LoadGameSelected (below) to get the filename.
    // Fires multiple times per user-visible load (engine bootstraps the
    // load through this path more than once); not deduplicated.
    kcdxMessage_PreLoadGame   = 5,

    // Save finished loading, world is interactive. Fires at
    // C_SaveGameManager::PostLoadGame entry. `data` is currently NULL.
    // Use the kcdxMessage_LoadGameSelected basename captured earlier
    // if you need to know WHICH save was loaded.
    kcdxMessage_PostLoadGame  = 6,

    // Game being saved (manual, quicksave, autosave, or save-and-quit).
    // `data` = const char* save basename, e.g. "save561.whs",
    // "autosave560.whs", "exit.whs". The full path is
    // %USER%/saves/playline<N>/<basename>.
    kcdxMessage_SaveGame      = 7,

    // A save plus its .kcdx co-save being deleted. `data` = const char*
    // save basename. Hook is installed but no UI surface in vanilla
    // KCD2 currently triggers it.
    kcdxMessage_DeleteGame    = 8,

    // kcdx's Lua surface (_G.kcdx and its sub-tables) is now populated and
    // safe to call. Fires once per process, on the first update tick after
    // RegisterKcdxTable runs.
    //
    // Why this exists: pak Lua test plugins (and any plugin Lua that wants
    // to call kcdx.* from pak-init time) face a chicken-and-egg — pak
    // scripts load early, but kcdx.* isn't installed until first-update-
    // tick. Listening for this message via a kcdxMessagingInterface
    // subscription gets you a one-shot "kcdx is ready" signal at exactly
    // the right moment.
    //
    // For pak Lua specifically, kcdx.dev.on_ready(fn) is a more
    // convenient wrapper that handles both the "already ready" and
    // "subscribe and wait" cases.
    kcdxMessage_LuaReady      = 9,

    // The user has selected a specific savegame to load and the engine
    // has resolved its on-disk filename. Fires once per user load
    // action, BEFORE deserialization begins. `data` = const char* save
    // basename (e.g. "save561.whs", "autosave560.whs", "exit.whs"); the
    // saves directory itself is %USER%/saves/playline<N>/ where <N> is
    // the active playline (use kcdxSerializationInterface accessors
    // when they land).
    //
    // Distinct from kcdxMessage_PreLoadGame:
    //  - kcdxMessage_PreLoadGame fires at every internal LoadGame
    //    invocation (including engine bootstraps that don't carry a
    //    user-chosen file).
    //  - kcdxMessage_LoadGameSelected fires only when the user
    //    explicitly picked a save from the menu AND the engine has
    //    resolved it to a real on-disk filename.
    //
    // For sidecar serialization workflows, prefer this message — it
    // gives you the filename early enough to parse a .kcdx sidecar
    // before kcdxMessage_PostLoadGame fires.
    kcdxMessage_LoadGameSelected = 10,

    // First plugin-defined message ID. Use anything >= this for your own
    // message types.
    kcdxMessage_FirstUserDefined = 0x10000,
};

// -----------------------------------------------------------------------------
// kcdxTaskInterface — schedule work onto the game's main thread
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Task,
// kcdxTaskInterface_Version).
//
// Most CryEngine state is NOT thread-safe. If your plugin is fired from a
// non-main thread (e.g. inside a MinHook detour on a function called from
// the game's worker pool), defer any game-state mutation onto the next
// update tick via AddTask.

#define kcdxTaskInterface_Version 1u

// A scheduled unit of work. Plugin authors derive from this and override
// Run + Dispose, then submit an instance to AddTask. The engine calls Run()
// on the main thread on the next update tick, then calls Dispose() to give
// the plugin a chance to free the object (typically `delete this;` inside
// Dispose).
//
// Defined as a C++ virtual class to match SKSE's interface shape. Plugin
// DLLs and the engine share an MSVC vtable ABI; this is safe.
#ifdef __cplusplus
struct kcdxTask {
    virtual ~kcdxTask() = default;

    // Called on the main thread next update tick. Do your work here.
    virtual void Run() = 0;

    // Called after Run() completes. Free the object — typical implementation
    // is `delete this;`.
    virtual void Dispose() = 0;
};
#else
// Opaque to C plugins.
typedef struct kcdxTask kcdxTask;
#endif

typedef struct kcdxTaskInterface {
    // Schedule `task` for execution on the main thread next update tick.
    // Safe to call from any thread. After Run() completes the engine calls
    // task->Dispose().
    void (*AddTask)(kcdxTask* task);
} kcdxTaskInterface;

// -----------------------------------------------------------------------------
// kcdxTrampolineInterface — allocate executable memory the plugin owns
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Trampoline,
// kcdxTrampolineInterface_Version).
//
// Two pools with different proximity guarantees:
//
//   AllocateFromBranchPool — executable memory within ±2 GB of WHGame.dll's
//     .text section, so a 5-byte E9 rel32 jump can reach it. Use this for
//     trampolines that the original game code branches into via 5-byte
//     jumps. Budget is limited (default 64 KB across all plugins). Returns
//     null on exhaustion.
//
//   AllocateFromLocalPool — executable memory anywhere VirtualAlloc places
//     it. Effectively unlimited budget. Plugins calling into this region
//     must use abs-64 jumps (14-byte FF 25 + 8 bytes of target) or a
//     register-indirect call. Use this when proximity doesn't matter.
//
// Both functions return memory marked PAGE_EXECUTE_READWRITE and zero-filled.
// Tag with `owner` so kcdx's pre-flight conflict detector knows which plugin
// owns which byte range — used when two plugins' trampolines would collide.
//
// Version 2 adds the high-level peers of the raw AllocateFrom*Pool floor:
//
//   Allocate(opts) — the all-in-one alloc+fill+pad+export call. The C++ mirror
//     of Lua kcdx.code{...}: allocate per opts.pool, copy opts.bytes to the
//     region head, NOP-pad the tail out to opts.size, register opts.exportName
//     if set, and return the region. The author declares intent in one
//     kcdxCodeOptions struct instead of hand-sequencing AllocateFrom*Pool +
//     memcpy + memset + Export.
//
//   Export(owner, bareName, addr) — the standalone symbol-table publish. The
//     C++ mirror of kcdx.code's export=, usable for an address the plugin
//     already holds (no allocation needed).

#define kcdxTrampolineInterface_Version 2u

// Which executable-memory pool kcdxCodeOptions::Allocate draws from. Mirrors
// the proximity guarantees of the raw AllocateFrom*Pool methods above and the
// Lua kcdx.code `pool = "branch"|"local"` field. Default 0 = branch.
typedef uint8_t kcdxCodePool;
// Branch pool — executable memory within ±2 GB of WHGame.dll's .text, so a
// 5-byte E9 rel32 jump can reach it. Budget is limited (default 64 KB across
// all plugins); Allocate returns null on exhaustion. The default (0).
#define kcdxCodePool_Branch 0u
// Local pool — executable memory anywhere VirtualAlloc places it. Effectively
// unlimited budget. Callers branching into this region must use abs-64 jumps
// (14-byte FF 25 + 8-byte target) or a register-indirect call. Use when
// rel32 proximity does not matter.
#define kcdxCodePool_Local  1u

// Options for the all-in-one Allocate call. Mirrors the Lua kcdx.code named
// table EXACTLY (src/lua_bind_code.cpp Lua_Code). POD, C-ABI shape (no
// std::string / std::vector — sentinel values for unset: null for strings,
// 0 for numerics).
//
// CONTRACT — must set `bytes` (with `bytesSize`) OR `size` (or both). `name`
// and `owningPlugin` are REQUIRED. `pool` defaults to branch (the 0 value).
// `exportName` is optional — a BARE name; the engine derives the
// <author>.<plugin> prefix. When `size` > `bytesSize`
// the tail is NOP-padded (0x90) so another plugin can patch into the unused
// space; `size` must be >= `bytesSize`.
typedef struct kcdxCodeOptions {
    // --- Owning plugin identity ------------------------------------------
    // REQUIRED. Drives the <author>.<plugin> export prefix and the pool
    // attribution (which plugin owns the allocated byte range). The author's
    // own plugin handle (from kcdxInterface::GetPluginHandle). Same role as
    // kcdxBytesOptions::owningPlugin. Pass kcdxInvalidPluginHandle (or 0) for
    // the anonymous path (the region still works; attribution is recorded as
    // invalid). The planned wrapper threads this for you; raw-interface
    // callers pass their own handle directly.
    kcdxPluginHandle owningPlugin;

    // --- Identity ---------------------------------------------------------
    // REQUIRED — the name used in logs and export diagnostics (e.g.
    // "outfit_gate_logic"), mirroring the Lua kcdx.code `name` field. Not a
    // shared name itself; see `exportName` to publish the region's address.
    const char* name;

    // --- Initial machine code (OPTIONAL) ---------------------------------
    // `bytes` + `bytesSize` are the C-pointer+length idiom (matching
    // kcdxMemoryInterface::WriteBytes(addr, bytes, size)). The engine copies
    // `bytesSize` bytes from `bytes` to the head of the allocated region.
    // null `bytes` / 0 `bytesSize` = no initial code (a bare NOP region sized
    // by `size`). Must set this OR `size` (or both).
    const void* bytes;
    size_t      bytesSize;

    // --- Total allocation (OPTIONAL) -------------------------------------
    // Total bytes to allocate. If > `bytesSize` the tail beyond the copied
    // bytes is NOP-padded (0x90) so another plugin can patch into the unused
    // space. 0 = default to `bytesSize` (allocate exactly the initial code).
    // Must be >= `bytesSize`. Mirrors the Lua kcdx.code `size` semantics and
    // the Lua "must declare bytes OR size" rule. Must set this OR `bytes`.
    size_t size;

    // --- Pool selection --------------------------------------------------
    // Which pool to allocate from. kcdxCodePool_Branch is the default (0), so
    // a zero-initialized kcdxCodeOptions allocates from the branch pool.
    kcdxCodePool pool;

    // --- Export (OPTIONAL) -----------------------------------------------
    // A BARE symbol name to publish the region's address under, resolvable by
    // a later hook/byte target_symbol lookup. null = no export. The author
    // writes ONLY the bare name; the engine derives the <author>.<plugin>
    // prefix from `owningPlugin` and publishes <author>.<plugin>.<exportName>.
    // A DOTTED `exportName` is an author error (the
    // engine supplies the prefix) and is rejected.
    const char* exportName;

    // --- APPEND-ONLY BELOW ---------------------------------------------
    // New options fields go HERE, never mid-struct. Same append-only discipline as
    // kcdxBytesOptions / kcdxHookOptions: a mid-struct insert shifts every
    // subsequent field's offset; a plugin DLL compiled against the older
    // header would read through the wrong offset → ACCESS_VIOLATION.
} kcdxCodeOptions;

typedef struct kcdxTrampolineInterface {
    // Allocate `size` bytes of executable memory within ±2 GB of WHGame.dll's
    // .text. Returns null if `size` is zero, the pool is exhausted, or no
    // free region within range exists. Plugin owns the returned pointer for
    // the lifetime of the process — no Free function (matches SKSE's model).
    void* (*AllocateFromBranchPool)(kcdxPluginHandle owner, size_t size);

    // Allocate `size` bytes of executable memory anywhere. Returns null on
    // failure (typically: out of memory).
    void* (*AllocateFromLocalPool)(kcdxPluginHandle owner, size_t size);

    // --- APPEND-ONLY BELOW (Version >= 2) ------------------------------
    // New methods go HERE, never inserted above. A v1 plugin compiled against
    // the 2-method struct finds AllocateFromBranchPool / AllocateFromLocalPool
    // at the SAME offsets and never reads these slots (append-only ABI).

    // The all-in-one allocate+fill+pad+export call — the high-level peer of
    // the raw AllocateFrom*Pool methods, and the C++ mirror of Lua
    // kcdx.code{...}. Allocates from opts->pool, copies opts->bytes (length
    // opts->bytesSize) to the region head, NOP-pads the tail out to
    // opts->size, and registers opts->exportName if set. Returns the region
    // pointer (PAGE_EXECUTE_READWRITE) or null on failure (bad opts —
    // neither bytes nor size set, size < bytesSize, dotted exportName, etc.;
    // pool exhausted; alloc failure; export collision). See kcdxCodeOptions
    // for the field contract. (Version >= 2.)
    void* (*Allocate)(const kcdxCodeOptions* opts);

    // Standalone symbol-table publish — registers `addr` under
    // <owner-author>.<owner-plugin>.<bareName> via the cross-plugin symbol
    // table (the C++ mirror of kcdx.code's export=, for an address the plugin
    // already holds without allocating). `bareName` is a BARE name; the engine
    // derives the <author>.<plugin> prefix from `owner`
    // — a dotted `bareName` is an author error and is rejected. Returns true
    // on success; false on bad args (null/empty bareName, dotted bareName,
    // invalid addr) or a collision — the SAME plugin re-exporting the SAME
    // bare name with a DIFFERENT address (names are per-namespace, so
    // cross-plugin clashes cannot happen). (Version >= 2.)
    bool (*Export)(kcdxPluginHandle owner, const char* bareName, uintptr_t addr);
} kcdxTrampolineInterface;

// -----------------------------------------------------------------------------
// kcdxScriptingInterface — register native C functions callable from pak Lua
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Scripting,
// kcdxScriptingInterface_Version). Companion to the kcdx.memory.dynamic_call
// Lua-side API: dynamic_call invokes EXISTING native functions; this
// interface lets plugins author NEW Lua-callable C functions.
//
// Why this interface bundles the Lua C API as function pointers (and
// not just a single RegisterFunction call): KCD2's Lua VM lives
// statically linked inside kcdx.asi. Plugin DLLs can't see those
// symbols via the linker — there's no analogue to SKSE's "VM is a
// vtable on a game-side class" trick.
//
// So this interface ships the full Lua 5.1 C API — all 117 LUA_API
// + LUALIB_API functions — as function pointers. Plugins:
//
//   #include "kcdx/Interfaces.h"
//   ...
//   static int Lua_Greet(struct lua_State* L, void* ud) {
//       auto* lua = static_cast<const kcdxLuaApi*>(ud);
//       const char* name = lua->ToString(L, 1);
//       lua->PushString(L, "hello!");
//       return 1;
//   }
//
//   scripting->RegisterFunction(handle, "hello", "greet",
//                               Lua_Greet, (void*)scripting->lua);
//
// Alternatively plugins can stash the kcdxLuaApi pointer in a static
// global at Load time and call lua->Push... directly. Plugin authors'
// choice.
//
// Lifecycle:
//   - Plugin calls RegisterFunction during kcdxPlugin_Load OR after,
//     from a kcdxMessaging callback. Both are supported.
//   - kcdx queues registrations called before its first-update-tick;
//     once the kcdx global table is created, queued + new registrations
//     are applied to the live Lua state.
//   - Functions registered persist for the session. No Unregister.

#define kcdxScriptingInterface_Version 1u

// Forward-declare lua_State to avoid pulling lua.h into the public
// plugin API. The Lua type is opaque to plugin authors — they use
// it as an out-of-band token.
struct lua_State;

// Plugin's Lua-callable function. The user_data slot is what the
// plugin passed to RegisterFunction (commonly: the kcdxLuaApi* so
// the plugin can call lua->Push... without a global). Returns the
// number of values pushed onto the Lua stack.
typedef int (*kcdxLuaCFunction)(struct lua_State* L, void* user_data);

// Raw Lua 5.1 lua_CFunction shape (no user_data). Needed by PushCClosure
// and the luaL_Reg table — kept distinct from kcdxLuaCFunction so the
// upvalue-shim ergonomics of RegisterFunction stay opt-in.
typedef int (*kcdxLuaRawCFunction)(struct lua_State* L);

// Reader/Writer callbacks for lua_load / lua_dump.
typedef const char* (*kcdxLuaReader)(struct lua_State* L, void* ud, size_t* sz);
typedef int         (*kcdxLuaWriter)(struct lua_State* L, const void* p,
                                     size_t sz, void* ud);

// Allocator callback for lua_getallocf / lua_setallocf. Same shape as
// Lua's lua_Alloc.
typedef void* (*kcdxLuaAlloc)(void* ud, void* ptr, size_t osize, size_t nsize);

// Debug-hook callback for lua_sethook / lua_gethook.
struct kcdxLuaDebug;
typedef void (*kcdxLuaHook)(struct lua_State* L, struct kcdxLuaDebug* ar);

// Activation record for the Debug API. Layout mirrors Lua 5.1's
// lua_Debug exactly — kcdx's Lua build defines LUA_IDSIZE=60. Plugins
// pass kcdxLuaDebug* through GetStack / GetInfo / GetLocal / SetLocal
// / GetUpvalue / SetUpvalue.
typedef struct kcdxLuaDebug {
    int         event;
    const char* name;             // (n)
    const char* namewhat;         // (n) 'global', 'local', 'field', 'method'
    const char* what;             // (S) 'Lua', 'C', 'main', 'tail'
    const char* source;           // (S)
    int         currentline;      // (l)
    int         nups;             // (u) number of upvalues
    int         linedefined;      // (S)
    int         lastlinedefined;  // (S)
    char        short_src[60];    // (S) LUA_IDSIZE=60 in kcdx's Lua build
    int         i_ci;             // private — active function index
} kcdxLuaDebug;

// luaL_Reg entry for batch registration via LRegister. Mirrors Lua's
// luaL_Reg one-to-one. A NULL `name` terminates the array.
typedef struct kcdxLuaLReg {
    const char*         name;
    kcdxLuaRawCFunction func;
} kcdxLuaLReg;

// Buffer for the LBuffInit / LPrepBuffer / LAddLString / LAddString /
// LAddValue / LPushResult API. Layout matches luaL_Buffer in kcdx's
// Lua build: BUFSIZ=512 on MSVC's CRT, so LUAL_BUFFERSIZE=512. Plugins
// allocate this on the stack and pass &buf into LBuffInit. The buffer
// fields are documented in lauxlib.h but plugins generally only need
// to call the functions, not poke the fields.
typedef struct kcdxLuaLBuffer {
    char*             p;             // current position
    int               lvl;           // strings on the Lua stack
    struct lua_State* L;
    char              buffer[512];   // LUAL_BUFFERSIZE
} kcdxLuaLBuffer;

// Pseudo-indices and constants — mirror lua.h verbatim so plugin code
// reads like the canonical API. Kept as macros (not enum) to match
// upstream Lua's exact spellings.
#define kcdxLua_MultRet         (-1)
#define kcdxLua_RegistryIndex   (-10000)
#define kcdxLua_EnvironIndex    (-10001)
#define kcdxLua_GlobalsIndex    (-10002)
#define kcdxLua_UpvalueIndex(i) (kcdxLua_GlobalsIndex - (i))

// Thread status codes returned by lua_pcall / lua_resume / lua_load.
#define kcdxLua_OK         0
#define kcdxLua_Yield      1
#define kcdxLua_ErrRun     2
#define kcdxLua_ErrSyntax  3
#define kcdxLua_ErrMem     4
#define kcdxLua_ErrErr     5
#define kcdxLua_ErrFile    (kcdxLua_ErrErr + 1)  // luaL_load* file-open error

// Basic types — returned by Type().
#define kcdxLua_TNone           (-1)
#define kcdxLua_TNil            0
#define kcdxLua_TBoolean        1
#define kcdxLua_TLightUserdata  2
#define kcdxLua_TNumber         3
#define kcdxLua_TString         4
#define kcdxLua_TTable          5
#define kcdxLua_TFunction       6
#define kcdxLua_TUserdata       7
#define kcdxLua_TThread         8

// GC opcodes for GC().
#define kcdxLua_GCStop         0
#define kcdxLua_GCRestart      1
#define kcdxLua_GCCollect      2
#define kcdxLua_GCCount        3
#define kcdxLua_GCCountB       4
#define kcdxLua_GCStep         5
#define kcdxLua_GCSetPause     6
#define kcdxLua_GCSetStepMul   7

// Debug hook event codes + masks.
#define kcdxLua_HookCall      0
#define kcdxLua_HookRet       1
#define kcdxLua_HookLine      2
#define kcdxLua_HookCount     3
#define kcdxLua_HookTailRet   4

#define kcdxLua_MaskCall  (1 << kcdxLua_HookCall)
#define kcdxLua_MaskRet   (1 << kcdxLua_HookRet)
#define kcdxLua_MaskLine  (1 << kcdxLua_HookLine)
#define kcdxLua_MaskCount (1 << kcdxLua_HookCount)

// Predefined ref values returned by LRef.
#define kcdxLua_NoRef  (-2)
#define kcdxLua_RefNil (-1)

// Lua C API surface available to plugin functions. Pointer signatures
// match Lua 5.1's API verbatim, so plugin code reads like raw lua.h.
// Members are function pointers because KCD2 ships Lua 5.1 inside
// kcdx.asi (no exported symbols, no game-side vtable).
//
// Naming: PascalCase (kcdx convention) — `PushString` not `pushstring` —
// so plugin code visually differs from any in-process lua.h
// includes, and to match the rest of kcdxInterface methods.
//
// Each function name's underlying lua_* / luaL_* is in a comment for
// grep'ability.
//
// Naming convention: lua_X → X (PascalCase). luaL_X → LX (PascalCase
// with `L` prefix). The `L` prefix keeps the two namespaces visually
// distinct in plugin code and matches Lua's own `L`-prefix tradition
// for the auxiliary library.
//
// All 117 LUA_API + LUALIB_API functions from Lua 5.1 are exposed —
// no exclusions. The Lua C API is sandbox-internal (every function
// operates on lua_State*); functions that could harm a user's system
// (os.execute, io.popen, package.loadlib) live in Lua's stdlib, not
// in the C API surface this struct wraps.
typedef struct kcdxLuaApi {
    // -------------------------------------------------------------------
    // state manipulation
    // -------------------------------------------------------------------
    struct lua_State* (*NewState)    (kcdxLuaAlloc f, void* ud);                          // lua_newstate
    void              (*Close)       (struct lua_State* L);                               // lua_close
    struct lua_State* (*NewThread)   (struct lua_State* L);                               // lua_newthread
    kcdxLuaRawCFunction (*AtPanic)   (struct lua_State* L, kcdxLuaRawCFunction panicf);   // lua_atpanic
    void              (*StoreDebugInfo)   (struct lua_State* L, int enable);              // lua_storedebuginfo (Cryengine extension)
    int               (*IsStoreDebugInfo) (struct lua_State* L);                          // lua_isstoredebuginfo (Cryengine extension)

    // -------------------------------------------------------------------
    // basic stack manipulation
    // -------------------------------------------------------------------
    int   (*GetTop)        (struct lua_State* L);                                         // lua_gettop
    void  (*SetTop)        (struct lua_State* L, int idx);                                // lua_settop
    void  (*PushValue)     (struct lua_State* L, int idx);                                // lua_pushvalue
    void  (*Remove)        (struct lua_State* L, int idx);                                // lua_remove
    void  (*Insert)        (struct lua_State* L, int idx);                                // lua_insert
    void  (*Replace)       (struct lua_State* L, int idx);                                // lua_replace
    int   (*CheckStack)    (struct lua_State* L, int n);                                  // lua_checkstack
    void  (*XMove)         (struct lua_State* from, struct lua_State* to, int n);         // lua_xmove

    // -------------------------------------------------------------------
    // access functions (stack -> C)
    // -------------------------------------------------------------------
    int                 (*IsNumber)     (struct lua_State* L, int idx);                   // lua_isnumber
    int                 (*IsString)     (struct lua_State* L, int idx);                   // lua_isstring
    int                 (*IsCFunction)  (struct lua_State* L, int idx);                   // lua_iscfunction
    int                 (*IsUserdata)   (struct lua_State* L, int idx);                   // lua_isuserdata
    int                 (*IsBoolean)    (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TBOOLEAN  (macro in lua.h, exposed as fn)
    int                 (*IsNil)        (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TNIL
    int                 (*IsTable)      (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TTABLE
    int                 (*IsFunction)   (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TFUNCTION
    int                 (*IsLightUserdata)(struct lua_State* L, int idx);                 // lua_type(L,n)==LUA_TLIGHTUSERDATA
    int                 (*IsThread)     (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TTHREAD
    int                 (*IsNone)       (struct lua_State* L, int idx);                   // lua_type(L,n)==LUA_TNONE
    int                 (*IsNoneOrNil)  (struct lua_State* L, int idx);                   // lua_type(L,n)<=0
    int                 (*Type)         (struct lua_State* L, int idx);                   // lua_type
    const char*         (*TypeName)     (struct lua_State* L, int tp);                    // lua_typename
    int                 (*Equal)        (struct lua_State* L, int idx1, int idx2);        // lua_equal
    int                 (*RawEqual)     (struct lua_State* L, int idx1, int idx2);        // lua_rawequal
    int                 (*LessThan)     (struct lua_State* L, int idx1, int idx2);        // lua_lessthan
    double              (*ToNumber)     (struct lua_State* L, int idx);                   // lua_tonumber
    long long           (*ToInteger)    (struct lua_State* L, int idx);                   // lua_tointeger
    int                 (*ToBoolean)    (struct lua_State* L, int idx);                   // lua_toboolean
    const char*         (*ToString)     (struct lua_State* L, int idx);                   // lua_tostring (macro: lua_tolstring with NULL)
    const char*         (*ToLString)    (struct lua_State* L, int idx, size_t* len);      // lua_tolstring
    size_t              (*ObjLen)       (struct lua_State* L, int idx);                   // lua_objlen
    kcdxLuaRawCFunction (*ToCFunction)  (struct lua_State* L, int idx);                   // lua_tocfunction
    void*               (*ToUserdata)   (struct lua_State* L, int idx);                   // lua_touserdata
    struct lua_State*   (*ToThread)     (struct lua_State* L, int idx);                   // lua_tothread
    const void*         (*ToPointer)    (struct lua_State* L, int idx);                   // lua_topointer

    // -------------------------------------------------------------------
    // push functions (C -> stack)
    //
    // PRECISION CAVEAT on KCD2: CryEngine's bundled Lua 5.1 is compiled
    // with LUA_NUMBER=float (single-precision, 24-bit mantissa). Any
    // integer pushed via PushInteger/PushNumber whose magnitude exceeds
    // 2^24 (16,777,216) loses low bits when read back. At pointer
    // magnitudes (~2^47) the rounding step is 16 MB, so 64-bit pointers
    // round to garbage 16 MB-aligned addresses.
    //
    // Plugin guidance: do NOT use PushInteger/PushNumber to hand a
    // pointer (or any value > 2^24) to pak Lua. Use PushLightUserdata
    // (raw void* round-trip is exact, but no metatable methods) or
    // the kcdx.memory.pointer userdata channel — both stay clean.
    //
    // This caveat is intrinsic to CryEngine's Lua build; we cannot fix
    // it inside kcdx. See kcdx/docs/lua-number-precision.md.
    // -------------------------------------------------------------------
    void        (*PushNil)            (struct lua_State* L);                              // lua_pushnil
    void        (*PushNumber)         (struct lua_State* L, double n);                    // lua_pushnumber (precision-lossy; see caveat above)
    void        (*PushInteger)        (struct lua_State* L, long long n);                 // lua_pushinteger (precision-lossy; see caveat above)
    void        (*PushLString)        (struct lua_State* L, const char* s, size_t len);   // lua_pushlstring
    void        (*PushString)         (struct lua_State* L, const char* s);               // lua_pushstring
    const char* (*PushVFString)       (struct lua_State* L, const char* fmt, va_list ap); // lua_pushvfstring
    const char* (*PushFString)        (struct lua_State* L, const char* fmt, ...);        // lua_pushfstring
    void        (*PushBoolean)        (struct lua_State* L, int b);                       // lua_pushboolean
    void        (*PushLightUserdata)  (struct lua_State* L, void* p);                     // lua_pushlightuserdata (exact for pointers — preferred over PushInteger for VAs)
    int         (*PushThread)         (struct lua_State* L);                              // lua_pushthread

    // PushCFunction: pushes a kcdxLuaCFunction (the 2-arg shape with
    // user_data) as a Lua closure. The user_data slot lets plugins
    // capture a kcdxLuaApi* or other context without a global. kcdx
    // wraps it in an internal shim so Lua sees a plain lua_CFunction.
    void        (*PushCFunction)      (struct lua_State* L, kcdxLuaCFunction fn, void* ud);

    // PushCClosure: raw lua_pushcclosure — push a kcdxLuaRawCFunction
    // with `n` upvalues already on the stack. Use this when you need
    // Lua's native closure semantics (multiple upvalues) rather than
    // the single-user_data shape of PushCFunction.
    void        (*PushCClosure)       (struct lua_State* L, kcdxLuaRawCFunction fn, int n); // lua_pushcclosure

    // -------------------------------------------------------------------
    // get functions (Lua -> stack)
    // -------------------------------------------------------------------
    void  (*GetTable)        (struct lua_State* L, int idx);                              // lua_gettable
    void  (*GetField)        (struct lua_State* L, int idx, const char* k);               // lua_getfield
    void  (*RawGet)          (struct lua_State* L, int idx);                              // lua_rawget
    void  (*RawGetI)         (struct lua_State* L, int idx, int n);                       // lua_rawgeti
    void  (*CreateTable)     (struct lua_State* L, int narr, int nrec);                   // lua_createtable
    void  (*NewTable)        (struct lua_State* L);                                       // lua_newtable (macro: createtable 0,0)
    void* (*NewUserdata)     (struct lua_State* L, size_t sz);                            // lua_newuserdata
    int   (*GetMetatable)    (struct lua_State* L, int objindex);                         // lua_getmetatable
    void  (*GetFEnv)         (struct lua_State* L, int idx);                              // lua_getfenv
    void  (*GetGlobal)       (struct lua_State* L, const char* name);                     // lua_getglobal (macro: getfield+globalsindex)

    // -------------------------------------------------------------------
    // set functions (stack -> Lua)
    // -------------------------------------------------------------------
    void  (*SetTable)        (struct lua_State* L, int idx);                              // lua_settable
    void  (*SetField)        (struct lua_State* L, int idx, const char* k);               // lua_setfield
    void  (*RawSet)          (struct lua_State* L, int idx);                              // lua_rawset
    void  (*RawSetI)         (struct lua_State* L, int idx, int n);                       // lua_rawseti
    int   (*SetMetatable)    (struct lua_State* L, int objindex);                         // lua_setmetatable
    int   (*SetFEnv)         (struct lua_State* L, int idx);                              // lua_setfenv
    void  (*SetGlobal)       (struct lua_State* L, const char* name);                     // lua_setglobal (macro: setfield+globalsindex)

    // -------------------------------------------------------------------
    // load and run Lua code
    // -------------------------------------------------------------------
    void  (*Call)            (struct lua_State* L, int nargs, int nresults);              // lua_call
    int   (*PCall)           (struct lua_State* L, int nargs, int nresults, int errfunc); // lua_pcall
    int   (*CPCall)          (struct lua_State* L, kcdxLuaRawCFunction func, void* ud);   // lua_cpcall
    int   (*Load)            (struct lua_State* L, kcdxLuaReader reader, void* dt,
                              const char* chunkname);                                     // lua_load
    int   (*Dump)            (struct lua_State* L, kcdxLuaWriter writer, void* data);     // lua_dump

    // -------------------------------------------------------------------
    // coroutine functions
    // -------------------------------------------------------------------
    int   (*Yield)           (struct lua_State* L, int nresults);                         // lua_yield
    int   (*Resume)          (struct lua_State* L, int narg);                             // lua_resume
    int   (*Status)          (struct lua_State* L);                                       // lua_status

    // -------------------------------------------------------------------
    // garbage collection
    // -------------------------------------------------------------------
    int   (*GC)              (struct lua_State* L, int what, int data);                   // lua_gc

    // -------------------------------------------------------------------
    // miscellaneous
    // -------------------------------------------------------------------
    int          (*Error)        (struct lua_State* L);                                   // lua_error (call only with a value already on the stack)
    int          (*Next)         (struct lua_State* L, int idx);                          // lua_next
    void         (*Concat)       (struct lua_State* L, int n);                            // lua_concat
    kcdxLuaAlloc (*GetAllocF)    (struct lua_State* L, void** ud);                        // lua_getallocf
    void         (*SetAllocF)    (struct lua_State* L, kcdxLuaAlloc f, void* ud);         // lua_setallocf

    // -------------------------------------------------------------------
    // debug API
    // -------------------------------------------------------------------
    int          (*GetStack)        (struct lua_State* L, int level, kcdxLuaDebug* ar);   // lua_getstack
    int          (*GetInfo)         (struct lua_State* L, const char* what, kcdxLuaDebug* ar); // lua_getinfo
    const char*  (*GetLocal)        (struct lua_State* L, const kcdxLuaDebug* ar, int n); // lua_getlocal
    const char*  (*SetLocal)        (struct lua_State* L, const kcdxLuaDebug* ar, int n); // lua_setlocal
    const char*  (*GetUpvalue)      (struct lua_State* L, int funcindex, int n);          // lua_getupvalue
    const char*  (*SetUpvalue)      (struct lua_State* L, int funcindex, int n);          // lua_setupvalue
    int          (*SetHook)         (struct lua_State* L, kcdxLuaHook func, int mask, int count); // lua_sethook
    kcdxLuaHook  (*GetHook)         (struct lua_State* L);                                // lua_gethook
    int          (*GetHookMask)     (struct lua_State* L);                                // lua_gethookmask
    int          (*GetHookCount)    (struct lua_State* L);                                // lua_gethookcount

    // -------------------------------------------------------------------
    // auxiliary library (luaL_*) — `L` prefix, otherwise same conventions
    // -------------------------------------------------------------------
    void         (*LOpenLib)        (struct lua_State* L, const char* libname,
                                     const kcdxLuaLReg* l, int nup);                      // luaI_openlib
    void         (*LRegister)       (struct lua_State* L, const char* libname,
                                     const kcdxLuaLReg* l);                               // luaL_register
    int          (*LGetMetafield)   (struct lua_State* L, int obj, const char* e);        // luaL_getmetafield
    int          (*LCallMeta)       (struct lua_State* L, int obj, const char* e);        // luaL_callmeta
    int          (*LTypeError)      (struct lua_State* L, int narg, const char* tname);   // luaL_typerror
    int          (*LArgError)       (struct lua_State* L, int numarg, const char* extramsg); // luaL_argerror
    const char*  (*LCheckLString)   (struct lua_State* L, int numArg, size_t* l);         // luaL_checklstring
    const char*  (*LOptLString)     (struct lua_State* L, int numArg, const char* def, size_t* l); // luaL_optlstring
    double       (*LCheckNumber)    (struct lua_State* L, int numArg);                    // luaL_checknumber (precision-lossy; see PushNumber caveat)
    double       (*LOptNumber)      (struct lua_State* L, int nArg, double def);          // luaL_optnumber
    long long    (*LCheckInteger)   (struct lua_State* L, int numArg);                    // luaL_checkinteger (precision-lossy; see PushInteger caveat)
    long long    (*LOptInteger)     (struct lua_State* L, int nArg, long long def);       // luaL_optinteger
    void         (*LCheckStack)     (struct lua_State* L, int sz, const char* msg);       // luaL_checkstack
    void         (*LCheckType)      (struct lua_State* L, int narg, int t);               // luaL_checktype
    void         (*LCheckAny)       (struct lua_State* L, int narg);                      // luaL_checkany
    int          (*LNewMetatable)   (struct lua_State* L, const char* tname);             // luaL_newmetatable
    void*        (*LCheckUdata)     (struct lua_State* L, int ud, const char* tname);     // luaL_checkudata
    void         (*LWhere)          (struct lua_State* L, int lvl);                       // luaL_where
    int          (*LError)          (struct lua_State* L, const char* fmt, ...);          // luaL_error (varargs)
    int          (*LCheckOption)    (struct lua_State* L, int narg, const char* def, const char* const lst[]); // luaL_checkoption
    int          (*LRef)            (struct lua_State* L, int t);                         // luaL_ref
    void         (*LUnref)          (struct lua_State* L, int t, int ref);                // luaL_unref
    int          (*LLoadFile)       (struct lua_State* L, const char* filename);          // luaL_loadfile
    int          (*LLoadBuffer)     (struct lua_State* L, const char* buff, size_t sz, const char* name); // luaL_loadbuffer
    int          (*LLoadString)     (struct lua_State* L, const char* s);                 // luaL_loadstring
    struct lua_State* (*LNewState)  (void);                                               // luaL_newstate
    const char*  (*LGSub)           (struct lua_State* L, const char* s, const char* p, const char* r); // luaL_gsub
    const char*  (*LFindTable)      (struct lua_State* L, int idx, const char* fname, int szhint); // luaL_findtable

    // luaL_Buffer surface — see kcdxLuaLBuffer struct above. Plugins
    // allocate the buffer on the stack, BuffInit it, then use the
    // Add* functions to accumulate, then PushResult to push the final
    // string. (The luaL_addchar/luaL_addsize macros in upstream lauxlib
    // are not exposed — use LAddLString instead.)
    void         (*LBuffInit)       (struct lua_State* L, kcdxLuaLBuffer* B);             // luaL_buffinit
    char*        (*LPrepBuffer)     (kcdxLuaLBuffer* B);                                  // luaL_prepbuffer
    void         (*LAddLString)     (kcdxLuaLBuffer* B, const char* s, size_t l);         // luaL_addlstring
    void         (*LAddString)      (kcdxLuaLBuffer* B, const char* s);                   // luaL_addstring
    void         (*LAddValue)       (kcdxLuaLBuffer* B);                                  // luaL_addvalue
    void         (*LPushResult)     (kcdxLuaLBuffer* B);                                  // luaL_pushresult
} kcdxLuaApi;

typedef struct kcdxScriptingInterface {
    // Register a native function callable from pak Lua at:
    //   kcdx.<table_name>.<fn_name>
    //
    // table_name must be a valid Lua identifier (alphanumeric +
    // underscore, must not start with a digit). fn_name same. Both
    // are copied; the strings need not outlive the call.
    //
    // user_data is passed through to every invocation. The plugin owns
    // its lifetime; kcdx never reads or frees it. A common pattern:
    // pass `scripting->lua` so the registered function has direct
    // access to the Lua C API without a global.
    //
    // Returns 1 on success, 0 on failure (invalid args, name collision,
    // OOM). Failures are also logged to kcdx.log.
    //
    // Thread safety: must be called from the main thread (i.e., from
    // kcdxPlugin_Load or from a Task::Run callback). Calling from a
    // worker thread results in undefined behavior.
    int (*RegisterFunction)(kcdxPluginHandle owner,
                            const char*      table_name,
                            const char*      fn_name,
                            kcdxLuaCFunction fn,
                            void*            user_data);

    // The Lua C API surface, owned by kcdx. Lifetime: process. Plugin
    // may store this pointer at any time after QueryInterface returns it.
    const kcdxLuaApi* lua;
} kcdxScriptingInterface;

// -----------------------------------------------------------------------------
// kcdxMemoryInterface — AOB scan + byte read/write for C++ plugins
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Memory,
// kcdxMemoryInterface_Version). Mirrors (a subset of) the pak Lua
// kcdx.memory.* surface for C++ plugins that want to do AOB resolution
// + byte rewrites without re-implementing the locator pipeline.
//
// The functions here go through the same patch_engine code paths
// kcdx uses internally — pattern syntax (hex pairs + `?` wildcards)
// matches the kcdx.bytes / kcdx.hook Lua schema exactly.
//
// Lifecycle:
//   - Safe to call from kcdxPlugin_Load.
//   - Safe to call from any kcdxMessaging callback.
//   - Plugin is responsible for the BYTE-LEVEL safety contract:
//     same-length writes, no clobbering unrelated state.

#define kcdxMemoryInterface_Version 1u

typedef struct kcdxMemoryInterface {
    // Scan moduleName's executable sections for the given AOB pattern.
    // Pattern syntax: space-separated hex pairs ("48 8B 41"), with `?`
    // as a wildcard byte (also accepts `??`).
    //
    // Returns the absolute VA of the FIRST match, or 0 if no match (or
    // if the pattern is ambiguous — multiple matches log a warn and
    // return 0; force a unique pattern with more context bytes).
    //
    // moduleName: e.g. "WHGame.dll". Pass NULL for the main module.
    uintptr_t (*ScanPattern)(const char* moduleName, const char* pattern);

    // Resolve a module's base address. Returns 0 if not loaded.
    uintptr_t (*GetModuleBase)(const char* moduleName);

    // Write `size` bytes at `addr`, handling VirtualProtect. Returns 1
    // on success, 0 on failure (writes nothing on failure — page-
    // protect roll back happens automatically).
    //
    // SAFETY: the write must be code that's safe to execute mid-function.
    // For patches, same-length rewrites of single instructions are the
    // documented contract (matches the same-length rule for byte patches).
    int (*WriteBytes)(uintptr_t addr, const void* bytes, size_t size);

    // Read `size` bytes at `addr` into `out`. No page-protect dance
    // needed (we use VirtualQuery to confirm readable). Returns 1 on
    // success.
    int (*ReadBytes)(uintptr_t addr, void* out, size_t size);
} kcdxMemoryInterface;

// -----------------------------------------------------------------------------
// kcdxConsoleInterface — register CryEngine console commands
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Console,
// kcdxConsoleInterface_Version). Thin wrapper around CryEngine's
// `IConsole::AddCommand(const char*, ConsoleCommandFunc, int, const char*)`.
// Lets plugins expose strings runnable from the -console window without
// touching the IConsole vtable directly.
//
// Lifecycle:
//   - Safe to call from kcdxPlugin_Load (engine resolves IConsole at boot
//     before plugins are loaded).
//   - The callback fires on the main thread (CryEngine dispatches console
//     commands during the game loop).
//
// Command-arg access uses the `kcdxConsoleCmdArgs` opaque pointer plus the
// `GetArgCount` / `GetArg` accessors — kcdx wraps CryEngine's
// `IConsoleCmdArgs` vtable so plugins don't need to know its layout.

// Opaque alias for CryEngine's IConsoleCmdArgs* — passed to your
// callback. Use the accessors below to read args.
typedef struct kcdxConsoleCmdArgs kcdxConsoleCmdArgs;

// Console-command callback. Plugins write a function with this
// signature and pass it to RegisterCommand. Called on the main thread.
typedef void (*kcdxConsoleCommandCallback)(const kcdxConsoleCmdArgs* args);

#define kcdxConsoleInterface_Version 3u

typedef struct kcdxConsoleInterface {
    // Register a console command. `name` must be unique across the
    // process (CryEngine's AddCommand allows redefinition, but kcdx
    // refuses to register a name twice from different plugins). `help`
    // is displayed when the user types `help <name>`. Returns true
    // on success.
    //
    // kcdx registers commands with CryEngine's VF_RESTRICTEDMODE flag
    // automatically, which is required for the in-game `~` console
    // to dispatch the command (without it, the Scaleform UI silently
    // ignores non-devmode commands). A future API revision may
    // expose a `flags` parameter for authors who want devmode-only
    // commands; v0.1 hardcodes the "callable from the UI console"
    // shape because that's the common case.
    //
    // Lifetime: the engine retains `name`, `help`, and `cb` for the
    // lifetime of the process. Plugin authors should pass string
    // literals (static-storage) or maintain the storage themselves.
    bool (*RegisterCommand)(kcdxPluginHandle owner,
                            const char*       name,
                            const char*       help,
                            kcdxConsoleCommandCallback cb);

    // Read args from inside your callback.
    // GetArgCount returns 1 + N (the first arg is the command name
    // itself, matching CryEngine's IConsoleCmdArgs convention).
    int          (*GetArgCount)(const kcdxConsoleCmdArgs* args);

    // Get arg N as a null-terminated string. nIndex=0 is the command
    // name itself; nIndex=1..GetArgCount()-1 are the user-supplied
    // arguments. Returns null on out-of-bounds. Pointer is valid for
    // the duration of the callback only.
    const char*  (*GetArg)(const kcdxConsoleCmdArgs* args, int nIndex);

    // Get the raw command line (everything the user typed, including
    // the command name and any quoted arguments). Pointer is valid
    // for the duration of the callback only.
    const char*  (*GetCommandLine)(const kcdxConsoleCmdArgs* args);

    // Programmatically execute a console command as if the user typed
    // it into the in-game `~` console. Goes through the same
    // IConsole::ExecuteString dispatch path that user input uses, so
    // your registered command fires synchronously (same thread, same
    // semantics). Useful for self-test harnesses and for triggering
    // commands from non-console paths (hotkeys, save/load handlers).
    //
    // Returns true on success, false if the IConsole surface isn't
    // ready (i.e. before kcdxMessage_InputLoaded fires).
    bool         (*ExecuteString)(const char* commandLine);

    // --- APPEND-ONLY BELOW ---

    // Print one plain line to the in-game `~` console overlay. The line
    // appears verbatim (no command-syntax wrapper, no prefix) — the engine
    // owns the trailing newline, so pass your text without one.
    //
    // Returns true on success. Returns false if the console surface isn't
    // ready yet (before kcdxMessage_InputLoaded), if the underlying print
    // path could not be resolved on this game build, or for a null/empty
    // string. A refusal is logged — it is never a silent no-op.
    bool         (*Print)(const char* text);

    // Read a game CVar's 32-bit int value by name. The name supplies
    // both the lookup and the verified accessor — you write the CVar
    // string you already hold (a modding wiki, the in-game `~` console,
    // a config); the engine resolves the console + the ICVar accessor.
    //
    // Returns true and writes `*out` on success. Returns false and
    // leaves `*out` UNTOUCHED if the CVar does not exist, or if the
    // console surface isn't ready (before kcdxMessage_InputLoaded), or
    // for a null/empty name. The out-param + bool-return shape makes a
    // "CVar missing" distinguishable from a real value of 0 — a failed
    // read never writes garbage. A refusal is logged.
    bool         (*GetCVarInt)(const char* name, int* out);

    // Read a game CVar as a bool: `*out = (the CVar's int value != 0)`.
    // No separate engine entity — the bool is derived from the int
    // accessor. Same true-writes-/false-leaves-untouched contract as
    // GetCVarInt; `*out` is untouched on any miss.
    bool         (*GetCVarBool)(const char* name, bool* out);

    // Read a game CVar's float value by name. Same contract as
    // GetCVarInt via the ICVar's float accessor — true writes `*out`,
    // false leaves it untouched on any miss.
    bool         (*GetCVarFloat)(const char* name, float* out);
} kcdxConsoleInterface;

// -----------------------------------------------------------------------------
// kcdxSerializationInterface — per-save plugin data (a `.kcdx` co-save)
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Serialization,
// kcdxSerializationInterface_Version). Modeled on SKSE's
// SKSESerializationInterface — plugins register Save/Load/Revert
// callbacks, get a unique 32-bit ID, and read/write arbitrary blobs
// via Open/Write/Read calls. The engine bundles all plugins' data
// into a single `<savename>.kcdx` co-save file that lives next to
// KCD2's `<savename>.whs`.
//
// Lifecycle:
//   - Register callbacks from kcdxPlugin_Load (or any kcdxMessaging
//     callback that fires before the first save/load).
//   - SetUniqueID is REQUIRED before any data your plugin writes is
//     persisted. The ID identifies your plugin's section in the
//     co-save and gates which chunks GetNextRecordInfo surfaces in
//     your LoadCallback. Two plugins using the same UID collide;
//     pick something distinct (a 32-bit FourCC from your plugin's
//     stable name works well).
//   - SaveCallback fires AFTER the engine has written the .whs.
//     Inside it, call OpenRecord(tag, version) to start a chunk,
//     then WriteRecordData(buf, len) to push bytes. Multiple
//     records per save are fine; reopen with a new tag.
//   - LoadCallback fires AFTER the engine has hydrated the world
//     from the .whs (i.e. at kcdxMessage_PostLoadGame timing).
//     Inside it, GetNextRecordInfo walks YOUR plugin's chunks one
//     by one; for each, ReadRecordData(buf, len) pulls the bytes.
//   - RevertCallback fires when starting a new game, OR when
//     loading a save that has no chunks for your plugin's UID
//     (because the save predates your plugin's installation, or
//     was written by a kcdx that didn't have your plugin loaded).
//     Use this to reset in-memory state to its "fresh game" values.
//
// Co-save format (informational, not part of the API contract):
//   Header: magic "KCDX" (4B), format version (4B u32), plugin count (4B u32)
//   Per plugin:
//     - UID (4B u32)
//     - chunk count (4B u32)
//     - section length in bytes (4B u32)
//     - chunks back-to-back
//   Per chunk:
//     - tag (4B u32, plugin-defined)
//     - version (4B u32, plugin-defined per-tag)
//     - tag-name length (4B u32) + tag-name bytes (the human-readable
//       string for an OpenRecordNamed chunk; length 0 for a chunk
//       opened via the numeric OpenRecord)
//     - length in bytes (4B u32)
//     - data
//
// Threading: SaveCallback and LoadCallback fire on the main thread
// (same thread as kcdxMessage_SaveGame / kcdxMessage_PostLoadGame).
// OpenRecord / WriteRecordData / GetNextRecordInfo / ReadRecordData
// MUST be called only from inside your callback — they consult
// thread-local engine state that's only valid during the callback.

// Version 2 adds the string-tag surface (OpenRecordNamed +
// GetRecordTagName) at the END of the struct, append-only — see the
// APPEND-ONLY block. A v1 plugin reading the v2 struct sees the
// unchanged prefix members at their original offsets (no ABI break).
#define kcdxSerializationInterface_Version 2u

typedef void (*kcdxSerializationSaveCallback)  (kcdxPluginHandle plugin);
typedef void (*kcdxSerializationLoadCallback)  (kcdxPluginHandle plugin);
typedef void (*kcdxSerializationRevertCallback)(kcdxPluginHandle plugin);

typedef struct kcdxSerializationInterface {
    // Set a unique 32-bit ID for this plugin's chunks. Required
    // before any saved data is persisted. A common convention is a
    // FourCC of your plugin's short name.
    void (*SetUniqueID)(kcdxPluginHandle plugin, uint32_t uid);

    // Register callbacks. Pass null to clear a previously-registered
    // callback. Re-registering replaces the previous callback.
    void (*SetSaveCallback)  (kcdxPluginHandle plugin, kcdxSerializationSaveCallback   cb);
    void (*SetLoadCallback)  (kcdxPluginHandle plugin, kcdxSerializationLoadCallback   cb);
    void (*SetRevertCallback)(kcdxPluginHandle plugin, kcdxSerializationRevertCallback cb);

    // Write side — call from your SaveCallback. OpenRecord starts a
    // new chunk in your plugin's section; WriteRecordData appends
    // bytes to the chunk that was last opened. Multiple records per
    // SaveCallback are fine; just OpenRecord again with a new tag.
    // Returns false if SetUniqueID wasn't called or the engine isn't
    // currently in a save phase.
    bool (*OpenRecord)     (uint32_t tag, uint32_t version);
    bool (*WriteRecordData)(const void* buf, uint32_t len);

    // Read side — call from your LoadCallback. GetNextRecordInfo
    // advances the cursor to the next chunk belonging to YOUR
    // plugin's UID, populating *outTag / *outVersion / *outLen.
    // Returns false when there are no more chunks (loop until then).
    // After a successful GetNextRecordInfo, call ReadRecordData with
    // a buffer of at least *outLen bytes to extract the chunk data.
    // (If you don't want a chunk, just call GetNextRecordInfo again
    // — the engine skips the unread chunk automatically.)
    bool (*GetNextRecordInfo)(uint32_t* outTag, uint32_t* outVersion, uint32_t* outLen);
    bool (*ReadRecordData)   (void* buf, uint32_t len);

    // --- APPEND-ONLY BELOW (kcdxSerializationInterface_Version >= 2) ---
    // New members go HERE, at the END, never mid-struct: a plugin DLL
    // built against an older version reads the prefix members at their
    // original offsets, so appending cannot shift them (append-only ABI).

    // Named-tag write side — the common path; the disassembler test's
    // fix for the hand-packed FourCC. Pass a human-readable string tag
    // ("counter", "spawned_npcs", …); the engine hashes it to the u32
    // the chunk stores AND records the original string so the read side
    // can hand it back via GetRecordTagName. Call from your SaveCallback
    // exactly like OpenRecord; follow with WriteRecordData.
    //
    // Returns false (and logs, naming both tags + your plugin) if two
    // DIFFERENT string tags collide to the same hash within this save —
    // a silent data-merge hazard the engine refuses rather than merges.
    // Reopening the SAME string tag again in one save is fine (multiple
    // records per save). Returns false too if SetUniqueID wasn't called
    // or you're not currently in a save phase.
    //
    // The numeric OpenRecord above stays for expert/interop use; a
    // chunk opened that way stores no name (GetRecordTagName returns "").
    bool (*OpenRecordNamed)(const char* tag, uint32_t version);

    // Named-tag read side — call AFTER a successful GetNextRecordInfo
    // to retrieve the current chunk's stored string tag. Returns the
    // human-readable name for a chunk written via OpenRecordNamed, or
    // "" for a numeric-tagged chunk (OpenRecord) or an old cosave that
    // predates the named-tag format. The returned pointer is valid
    // until the next GetNextRecordInfo / ReadRecordData / end of the
    // LoadCallback; copy it if you need to keep it. Never null.
    const char* (*GetRecordTagName)();
} kcdxSerializationInterface;

// -----------------------------------------------------------------------------
// kcdxHookInterface — C++ mirror of the Lua kcdx.hook.* surface
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Hook,
// kcdxHookInterface_Version). C++ DLLs install function hooks through this
// interface; the surface is feature-parity with the Lua kcdx.hook.* sub-verbs
// (ONE model, two languages, full parity at all times).
//
// SHAPE — sub-verb-per-variant, NOT mode-as-key (discrete behavioral
// variants are sub-verbs, not table keys).
// The Lua surface exposes a sub-verb per hook variant
// (kcdx.hook.before / .after / .around / .replace / .mid / .callsite); this
// interface mirrors that one-to-one as six method pointers (Before, After,
// Around, Replace, Mid, Callsite). The variant IS the method name — there is
// no `mode` field on the options struct, and there is no shared Install entry
// point. Each method takes the variant's required args positionally
// (target + callback) and an optional knobs container (`opts`) that may be
// null for the simple case.
//
// THIS HEADER ships the raw method pointers — the unchecked floor of the
// `Kcdx.h` model (its "raw" floor; see docs/cpp/wrapper.md "The 3-floor
// model"). The shipped `include/kcdx/Kcdx.h` wrapper layers typed templated
// helpers on top: `kcdx::hook::Before<Sig, &fn>(...)` (the empowered floor)
// and `kcdx::hook::TryBefore<Sig, &fn>(...)` (the handle-returning floor).
// There is NO separate `InstallRawUnchecked` form — `K.hook->Before(...)`
// (this struct, via the wrapper's `K.hook` pointer) IS the unchecked floor
// by construction (a `void*` callback is unchecked already). The raw
// interface (this struct) is the always-available
// floor — every author capability is reachable through it via
// `api->QueryInterface(kcdxInterface_Hook, kcdxHookInterface_Version)`
// without ever including the wrapper. The wrapper, when it lands, will
// expose the raw interface pointer directly (`K.hook == kcdxHookInterface*`)
// so nothing is hidden from raw-interface callers.
//
// The disassembler test — the engine does the heavy lifting; the author
// declares intent. The COMMON path is the
// positional `target` argument carrying a NAME — the Address Library entry
// (`"IsInCombat"`), the explicit prefixed cross-plugin form
// (`"redmoon.outfit.open_inventory"`), or the 1-dot engine-seed form
// (`"kcdx.luaL_loadfile"`). A name resolves to BOTH the address AND the
// verified signature; the author types one line and never hand-writes hex
// or ABI. Every other locator field is an EXPERT/ADVANCED escape hatch for
// targets the library cannot yet name, lives in `opts`, and is labeled
// `[advanced]` in-place below. The author identifies an un-named target
// ONCE via an advanced locator, names it, and refers to it by name
// thereafter (declare once / share / coexist).
//
// Threading: the engine auto-marshals an off-thread hook hit to the main
// thread before firing the callback. C++ callbacks behave identically
// to Lua callbacks on off-thread fires; the `offThread` field on opts
// selects marshal (default) / skip / error per the same model. The engine
// queues off-thread fires onto the main thread; the original function
// returns synchronously with its pre-hook default behavior.

// Version 2: the Mid callback ABI gained an `int` return (kcdxMidResult) so a
// C++ mid hook can skip the captured instruction — the parity mirror of the
// Lua mid callback's `return "skip"`. v1 mid callbacks were `void` (no skip
// channel). The return slot was previously unused (a mid hook never returns a
// value to the hooked function — that is `Replace`/`Around`), so repurposing
// it costs no prior capability. See kcdxMidResult + the Mid ABI note below.
#define kcdxHookInterface_Version 2u

// Mid-callback result — what a C++ Mid callback returns to tell the engine
// whether to RUN or SKIP the captured instruction. Mirrors the Lua mid
// callback's run/skip contract (Lua: return nothing/false = run, return
// "skip"/true = skip). The engine reads this return into the skip-original
// flag the JIT consumes; returning Skip resumes execution PAST the captured
// instruction (its effect does not happen), Run lets it execute normally.
// Mutating captures via the values[] array (writing values[i].value_*) is
// independent of this and applies in BOTH cases.
typedef enum kcdxMidResult {
    kcdxMidResult_Run  = 0,   // (default) the captured instruction executes
    kcdxMidResult_Skip = 1,   // the captured instruction is skipped
} kcdxMidResult;

// Opaque handle returned by every sub-verb install method. 0 = registration
// failed at Install time (the engine logs the teaching error to both the
// engine log and the calling plugin's log). A NON-ZERO handle does NOT yet
// mean applied: kcdx defers apply to a later pass (deferred-apply model);
// query IsApplied(h) after the apply pass to confirm. Stable for the process
// lifetime; never reused; safe to copy by value. Matches the registry handle
// id width (lua_registry::Append returns uint64_t).
typedef uint64_t kcdxHookHandle;

// Off-thread routing — the engine auto-marshals off-thread hits. Engine compares
// the dispatch thread to the recorded main-thread ID; off-thread fires
// route per the selected policy. Default (Marshal) is the right answer
// for almost every site; the alternatives exist for the rare cases
// where queue pressure or strict main-thread asserts are wanted.
typedef uint8_t kcdxHookOffThread;
#define kcdxHookOffThread_Marshal  0u  // default — engine queues to main thread
#define kcdxHookOffThread_Skip     1u  // silently drop off-thread fires; warn-once-per-hook
#define kcdxHookOffThread_Error    2u  // log error and drop; author asserts main-thread-only

// Callsite sub-verb behavior selector (Callsite is one method pointer
// on the interface; the behavior is selected by this field on opts).
// Mirrors kcdxHookOffThread's shape. Default 0 = Before, the most
// common callsite pattern. Author selects After/Around/Replace by
// setting opts.callsiteBehavior accordingly.
typedef uint8_t kcdxHookCallsiteBehavior;
#define kcdxHookCallsiteBehavior_Before  0u
#define kcdxHookCallsiteBehavior_After   1u
#define kcdxHookCallsiteBehavior_Around  2u
#define kcdxHookCallsiteBehavior_Replace 3u

// One mid-mode capture descriptor (used by the Mid sub-verb). Author owns the
// storage (typically a static-const array literal in the calling DLL); the
// engine reads-only at Install time and copies what it needs. Captures are
// positional when `name` is null; named when `name` is set — drives whether
// the callback's handle table is keyed by 1..N or by name (parity with the
// Lua positional-vs-map surface).
typedef struct kcdxHookCapture {
    const char* expr;   // register / memory expression ("rcx", "[rcx+0x10]")
    const char* type;   // type string ("i32", "i64", "ptr", etc.)
    const char* name;   // optional — author's name for this capture; null = positional
} kcdxHookCapture;

// Runtime mirror of kcdxHookCapture for the C Mid callback. The
// install-time kcdxHookCapture array carries metadata (expr/type/
// name); this struct carries the runtime VALUE for each capture at
// dispatch time. Engine fills the typed value field per `type` pre-
// call from the JIT slot payload (16-byte stride); reads back the
// SAME field post-call and writes the bytes back. Author touches the
// value field matching the capture's type — touching the wrong field
// is silently dropped (e.g. setting value_double on an i32 capture).
//
// The Mid callback ABI is `int cFn(kcdxHookCaptureValue* values,
// int count)` (kcdxHookInterface_Version >= 2; v1 was `void`). The
// return is a kcdxMidResult: return kcdxMidResult_Skip to skip the
// captured instruction, kcdxMidResult_Run (or just 0) to run it — the
// parity mirror of the Lua mid callback's `return "skip"`. Author
// reads/writes values[i].value_<type> per the capture's declared type
// (independent of the run/skip return — capture writes apply either way):
//
//   int32_t hp = (int32_t)values[0].value_int64;  // read
//   values[0].value_int64 = new_hp;               // write
//   return (hp <= 0) ? kcdxMidResult_Skip : kcdxMidResult_Run;
//
// Engine type → field mapping:
//   i8/i16/i32/i64/u8/u16/u32/u64/bool  → value_int64
//   f32/f64/float/double                → value_double
//   ptr                                  → value_ptr
typedef struct kcdxHookCaptureValue {
    const char* name;          // capture name (from kcdxHookCapture.name) or null for positional
    const char* type;          // capture type string (e.g. "i32", "ptr")
    int64_t     value_int64;   // populated for integer/bool types
    double      value_double;  // populated for f32/f64 types
    void*       value_ptr;     // populated for ptr type
    // --- APPEND-ONLY BELOW (new fields go HERE, never mid-struct) ---
} kcdxHookCaptureValue;

// Optional knobs container shared by every sub-verb. Pass null for the
// simple case; pass a pointer to one of these for any of: a non-default
// locator (the COMMON path is the positional `target` arg — see each
// method below), `signature` override, alternate `name` / `description`,
// off-thread policy, module override, AOB context / anchor, captures (for
// Mid), or callsite sub-locator (for Callsite). POD, C-ABI shape (no
// std::string / std::vector / std::optional — sentinel values for unset:
// null for strings, 0 for numerics, captureCount=0 for no captures).
//
// Locator policy. The positional `target` argument is the COMMON path; the
// engine resolves it via address_library::ResolveByName(target, owningPlugin)
// (self > engine > other precedence). The fields in
// this struct's "Function-entry locator" block are EXPERT/ADVANCED escape
// hatches: pass an empty `target` (null or "") on the install method AND set
// one of these instead when the target cannot yet be named. Each is labeled
// `[advanced]` (a labeled expert-only escape hatch).
//
// Owning identity (`owningPlugin`). Drives the self > engine > other-plugin
// precedence walk in any bare-name locator. The
// wrapper helpers thread this for you; raw-interface callers pass their own
// handle (from kcdxInterface::GetPluginHandle). Pass kcdxInvalidPluginHandle
// to disable self-tier resolution (engine-seed + other-plugin only,
// anonymous path).
typedef struct kcdxHookOptions {
    // --- Identity ---------------------------------------------------------
    // Optional override of the engine-synthesized identity. The engine
    // composes a default of "<handleId>:<target>" when this is null; supply
    // a stable, human-readable string when you want log lines tagged with
    // your own name.
    const char* name;          // optional — null = engine synthesizes
    const char* description;   // optional — freeform; may be null

    // --- Function-entry locator (EXPERT/ADVANCED — `target` is the common
    //     path; these are for targets the library cannot yet name) --------
    // The author identifies the target ONCE via one of these forms, names it
    // (publishes via kcdx.address or via a cross-plugin export),
    // and refers to it by name thereafter (declare once /
    // share / coexist). All sentinel-null/zero when unset.
    const char* pattern;              // [advanced] AOB hex at function entry; null = unset
    uint64_t    addressId;            // [advanced] Address-Library numeric ID; 0 = unset
    const char* targetSymbol;         // [advanced] cross-plugin symbol-table lookup; null = unset
    const char* targetLuaCfunction;   // [advanced] e.g. "System.LogAlways"; null = unset
    uintptr_t   address;              // [advanced] raw absolute VA; 0 = unset
    int32_t     offset;               // applied after resolution (Mid uses this too)
    const char* context;              // [advanced] AOB disambiguation; null = none
    const char* anchorString;         // [advanced] string anchor; null = none
    uint32_t    maxAnchorDistance;    // default 4096 (engine substitutes if 0)
    const char* module;               // default "WHGame.dll" (engine substitutes if null)

    // --- Callsite sub-locator (Callsite sub-verb only) -------------------
    // The Callsite sub-verb redirects ONE call instruction. The positional
    // `target` arg on Callsite(...) is the FUNCTION whose body contains the
    // call; these fields locate the CALL instruction within it whose rel32
    // displacement gets rewritten. Exactly one of callsitePattern /
    // callsiteAddressId / callsiteRva resolves the callsite address.
    const char* callsitePattern;      // [advanced] AOB at the CALL instr; null = unset
    int32_t     callsiteOffset;       // offset to the CALL opcode in the pattern match
    uint64_t    callsiteAddressId;    // [advanced] Address-Library ID of the callsite; 0 = unset
    const char* callsiteRva;          // [advanced] "WHGame.dll @ rva 0x12345a" form; null = unset

    // --- Signature --------------------------------------------------------
    // Unparsed signature DSL ("i32 (i32 seed)" / "void ()"). Engine parses
    // + caches at Install time. For the COMMON path (a named `target`
    // carrying a library-verified signature) this may be null and the engine
    // substitutes the verified one — the author never re-types what the
    // engine already knows. For the Mid sub-verb this may also be null (raw
    // register captures with no function signature). For non-Mid sub-verbs
    // on a locator that does NOT carry a signature (e.g. raw `address` with
    // no published library entry), the install method fails when signature
    // is null. See hook_signature.h for the DSL grammar.
    //
    // CONFLICT CONTRACT — named target + explicit signature both set: the
    // EXPLICIT signature WINS (the deliberate-override case). The engine
    // consults the verified ABI only to DETECT a mismatch, not to override:
    // if the explicit signature is incompatible with the named target's
    // verified ABI (different arg count or per-slot/return type), the install
    // emits a teaching WARN (HOOK_SIG_GATE / explicit_overrides_verified) and
    // PROCEEDS with the explicit signature — the install still succeeds.
    const char* signature;

    // --- Mid sub-verb captures -------------------------------------------
    // Pointer + count C-ABI form. Author owns the storage (typically a
    // static-const array literal in the calling DLL); the engine reads-only
    // at Install time and copies what it needs. Both null/0 unless calling
    // the Mid sub-verb.
    const kcdxHookCapture* captures;
    uint32_t               captureCount;

    // --- Off-thread routing (engine auto-marshals off-thread hits) -------
    // Default (Marshal) is the right answer for almost every site. Pass
    // 0 (kcdxHookOffThread_Marshal) to take the default; Skip / Error
    // are for the rare cases the author asserts a main-thread invariant.
    kcdxHookOffThread offThread;

    // --- Owning plugin identity ------------------------------------------
    // Drives self > engine > other-plugin precedence for the bare-name
    // form of the positional `target` arg. The author's own plugin handle
    // (from kcdxInterface::GetPluginHandle). Pass kcdxInvalidPluginHandle
    // (or 0) to disable self-tier resolution (engine-seed + other-plugin
    // only, anonymous path). The wrapper helpers stash the author's handle
    // at Kcdx.Init and pass it automatically; raw-interface users pass
    // their own handle directly.
    kcdxPluginHandle owningPlugin;

    // --- APPEND-ONLY BELOW ---------------------------------------------
    // New options fields go HERE, never mid-struct. Same append-only discipline
    // as the kcdxHookInterface vtable: mid-struct insert shifts every
    // subsequent field's offset; a plugin DLL compiled against the older
    // header would read through the wrong offset → ACCESS_VIOLATION.

    // Callsite sub-verb behavior selector. Only meaningful when calling
    // the Callsite() sub-verb on kcdxHookInterface; ignored by every
    // other sub-verb. Default 0 = kcdxHookCallsiteBehavior_Before (the
    // most common callsite pattern). See kcdxHookCallsiteBehavior_*
    // above. The Lua surface uses `mode = "callsite"` + an inline
    // before=/after=/around=/replace= function; this field is the C++
    // mirror.
    kcdxHookCallsiteBehavior callsiteBehavior;
} kcdxHookOptions;

typedef struct kcdxHookInterface {
    // ------------------------------------------------------------------
    // Install methods — one per hook variant (sub-verb-per-variant).
    // Mirrors kcdx.hook.before / .after / .around / .replace / .mid /
    // .callsite one-to-one. The variant IS the method name — there is no
    // shared Install with a `mode` enum (variants are sub-verbs, not keys).
    //
    // Each method takes the COMMON path positionally:
    //   `target`   — Address-Library name OR explicit cross-plugin form
    //                ("<author>.<plugin>.<bare>") OR engine-seed form
    //                ("kcdx.<seedname>"). The engine resolves via
    //                address_library::ResolveByName(target, owningPlugin)
    //                with self > engine > other precedence. Pass null or
    //                "" only when using an [advanced] locator in opts
    //                (pattern / addressId / targetSymbol /
    //                targetLuaCfunction / address). For Callsite, this is
    //                the FUNCTION containing the call; the CALL instruction
    //                is selected by opts->callsitePattern / etc.
    //   `callback` — function pointer cast to void* for C-ABI portability.
    //                The engine's JIT thunk casts this to the signature
    //                derived from the resolved `target` (or opts->signature
    //                if you override). ABI: free function or static member
    //                (cdecl/stdcall per the signature DSL); capturing
    //                lambdas are NOT directly callable — pass a free
    //                function that calls into your capturing state.
    //   `opts`     — optional knobs container; pass nullptr for the simple
    //                case. See kcdxHookOptions above.
    //
    // Returns a handle to keep for IsApplied / GetReason / Uninstall. A
    // zero handle = registration FAILED at Install time (mode/locator
    // mismatch, signature parse failure, owning-plugin handle unknown,
    // etc.). The teaching reason is auto-logged at Error level to the
    // engine log AND the calling plugin's log — raw-interface callers get
    // the same loud-on-failure behavior as the wrapper users (the
    // empowerment-frame floor-1 contract). A NON-ZERO handle does NOT yet
    // mean APPLIED — use IsApplied(h) after the apply pass; use
    // GetReason(h) for the failure reason when a registered hook later
    // failed to apply.

    // Run callback BEFORE the original; may mutate args.
    kcdxHookHandle (*Before)  (const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // Run callback AFTER the original; may mutate the return value.
    kcdxHookHandle (*After)   (const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // Callback decides whether/when to call original (receives a
    // call_original primitive); its return is the result.
    kcdxHookHandle (*Around)  (const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // Original never runs; callback's return is the result.
    kcdxHookHandle (*Replace) (const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // Mid-function capture at opts->offset; callback receives a handle table
    // keyed by opts->captures (positional or named).
    kcdxHookHandle (*Mid)     (const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // Redirect ONE call instruction inside `target` (the containing
    // function). The CALL instruction is selected by opts->callsitePattern
    // / opts->callsiteAddressId / opts->callsiteRva.
    kcdxHookHandle (*Callsite)(const char* target, void* callback,
                               const kcdxHookOptions* opts /* nullable */);

    // ------------------------------------------------------------------
    // Query / control methods on a handle.
    // ------------------------------------------------------------------

    // Query whether the apply pass has installed the hook described by `h`.
    // Returns false for an unknown handle, a still-pending handle (apply
    // pass hasn't run yet), an uninstalled handle, or a handle that failed
    // to apply. Mirrors the Lua `h:applied()` query.
    bool (*IsApplied)(kcdxHookHandle h);

    // Failure reason for a hook that did not apply (or that was
    // uninstalled). Returns null when `h` is valid AND applied; otherwise
    // returns a teaching-error string explaining what went wrong + the fix
    // (e.g. "target 'IsInCombat' did not resolve in WHGame.dll @ runtime
    // build 1.5.1164953 — confirm the name in your Address Library, or use
    // the expert `pattern` locator"). String is owned by the engine and
    // valid for the process lifetime. Mirrors the Lua `h:reason()` query.
    const char* (*GetReason)(kcdxHookHandle h);

    // Author-supplied (or engine-synthesized) hook name. Returns null for
    // an unknown handle. String is owned by the engine and valid for the
    // lifetime of the handle. Mirrors the Lua `h:name()` query.
    const char* (*GetName)(kcdxHookHandle h);

    // Uninstall the hook described by `h`. Returns true on success.
    // Idempotent: uninstalling an already-uninstalled (or never-applied)
    // handle returns true and is a no-op. Safe to call from
    // kcdxPlugin_Load or any kcdxMessaging callback. After return,
    // IsApplied(h) is false and the callback no longer fires. The
    // underlying MinHook detour stays installed for the session — engine
    // reuses it if another install lands on the same target later. Mirrors
    // the Lua `h:uninstall()` method.
    bool (*Uninstall)(kcdxHookHandle h);

    // --- APPEND-ONLY BELOW (kcdxHookInterface_Version >= 2) ---------------
    // New members go HERE, at the END, never mid-struct: a plugin DLL
    // built against an older version reads the prefix members at their
    // original offsets, so appending cannot shift them (append-only ABI).
} kcdxHookInterface;

// -----------------------------------------------------------------------------
// kcdxBytesInterface — C++ DLL mirror of the Lua kcdx.bytes surface
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Bytes,
// kcdxBytesInterface_Version). C++ DLLs register byte rewrites through this
// interface; the surface is feature-parity with the Lua kcdx.bytes
// (ONE model, two languages, full parity at all times).
//
// SHAPE — ONE operation, not sub-verbs. Unlike kcdxHookInterface (six install
// methods, one per hook variant), a byte rewrite has a SINGLE operation: write
// `replacement` at a located site. There is therefore ONE Register method
// taking an options struct, plus the same query quartet kcdxHookInterface
// exposes (IsApplied / GetReason / GetName / Uninstall). The Lua surface is the
// single `kcdx.bytes{...}` call returning a handle with :applied()/:reason()/
// :name(); this interface mirrors it one-to-one.
//
// DEFERRED-APPLY CONTRACT — same model as kcdxHookInterface. Register validates
// IMMEDIATELY (locator format, exactly-one-locator, replacement present,
// original==replacement length) and returns a handle; a ZERO handle means
// registration FAILED at Register time (the engine logs the teaching reason to
// both the engine log and the calling plugin's log). A NON-ZERO handle does
// NOT yet mean APPLIED: the actual VirtualProtect + memcpy is DEFERRED to the
// end-of-zone apply pass so the conflict engine sees every plugin's intent
// before any byte is written. Query IsApplied(h) after the apply pass to
// confirm; GetReason(h) gives the failure reason for a registered rewrite that
// later failed to apply (locator miss, byte-mismatch, etc.).
//
// COEXIST WITH kcdxMemoryInterface::WriteBytes. This interface is the DEFERRED,
// locator-based, conflict-resolved registration path. For an IMMEDIATE raw
// write at an address you already hold (no locator, no conflict arbitration,
// no deferral), use kcdxMemoryInterface::WriteBytes / ReadBytes instead — that
// surface is unchanged and unaffected by this interface.
//
// The disassembler test — the engine does the heavy lifting; the author
// declares intent. The COMMON path is
// opts->target carrying a NAME the engine resolves to an address (an Address
// Library seed entry, an author-declared target, or the explicit prefixed
// cross-plugin form). The author types a name and never hand-writes hex.
// The pattern / addressId / targetSymbol locator fields are EXPERT/ADVANCED
// escape hatches for sites the name table cannot yet name; each is labeled
// `[advanced]` in-place below.

#define kcdxBytesInterface_Version 1u

// Opaque handle returned by Register. 0 = registration failed at Register time
// (the engine logs the teaching error to both the engine log and the calling
// plugin's log). A NON-ZERO handle does NOT yet mean applied: kcdx defers apply
// to the end-of-zone pass (deferred-apply model); query IsApplied(h) after the
// apply pass to confirm. Stable for the process lifetime; never reused; safe to
// copy by value. Shares the registry handle id space with kcdxHookHandle
// (lua_registry::Append returns uint64_t).
typedef uint64_t kcdxBytesHandle;

// Options for a byte-rewrite registration. Mirrors the Lua kcdx.bytes named
// table EXACTLY (src/lua_bind_bytes.cpp Lua_Bytes). POD, C-ABI shape (no
// std::string / std::vector — sentinel values for unset: null for strings,
// 0 for numerics).
//
// LOCATOR CONTRACT — EXACTLY ONE locator must be set (exactly one of target /
// pattern / addressId / targetSymbol non-null/non-zero). `target` is the COMMON
// PATH — a NAME the engine resolves to an address (the disassembler test —
// the name carries address AND ABI). The other three are the labeled EXPERT/ADVANCED
// escape hatch for sites the name table cannot yet name. Setting zero locators,
// or more than one, is a Register-time rejection with a teaching error.
typedef struct kcdxBytesOptions {
    // --- Identity ---------------------------------------------------------
    // Optional. `name` defaults to "cpp_bytes" when null (the engine
    // substitutes it); supply a stable, human-readable string when you want
    // log lines + cross-plugin references tagged with your own name.
    const char* name;          // optional — null = engine default "cpp_bytes"
    const char* description;   // optional — freeform; may be null

    // --- Locator (EXACTLY ONE — `target` is the common path; the rest are
    //     the labeled EXPERT/ADVANCED escape hatch) -----------------------
    // The COMMON PATH. A name the engine resolves to an address via
    // address_library::ResolveByName(target, owningPlugin) with self > engine
    // > other precedence. An Address Library seed
    // entry ("kcdx.<seedname>"), an author-declared target, the bare own-plugin
    // name, or the explicit cross-plugin form ("<author>.<plugin>.<bare>").
    // The author types one line and never hand-writes hex. null = unset.
    const char* target;
    // The three EXPERT/ADVANCED locators — use ONLY when the target cannot yet
    // be named. The author identifies the site ONCE via one of these, names it,
    // and refers to it by name thereafter (declare once /
    // share / coexist).
    const char* pattern;       // [advanced] AOB hex at the rewrite site; null = unset
    uint64_t    addressId;     // [advanced] Address-Library numeric ID; 0 = unset
    const char* targetSymbol;  // [advanced] cross-plugin published-symbol lookup; null = unset

    // --- The rewrite ------------------------------------------------------
    // `replacement` is REQUIRED — the bytes to write (hex string,
    // e.g. "90 90 90"). `original` is optional verify bytes; when set it must
    // equal the replacement byte length (Register rejects a mismatch) and the
    // apply pass refuses to write if the site doesn't currently match.
    const char* replacement;   // REQUIRED — bytes to write; null/empty = rejected
    const char* original;      // optional — verify bytes; null = no verify

    // --- Refinements ------------------------------------------------------
    const char* module;        // default "WHGame.dll" (engine substitutes if null)
    int         offset;        // default 0 — applied after locator resolution
    bool        idempotent;    // default true — skip re-apply if bytes already match
    const char* context;       // [advanced] AOB disambiguation for `pattern`; null = none
    const char* anchorString;  // [advanced] string anchor for `pattern`; null = none

    // --- Owning plugin identity ------------------------------------------
    // REQUIRED. Drives self > engine > other-plugin precedence for the
    // bare-name form of `target`. The author's own plugin handle (from
    // kcdxInterface::GetPluginHandle). Pass kcdxInvalidPluginHandle (or 0) to
    // disable self-tier resolution (engine-seed + other-plugin only, anonymous
    // path). The planned wrapper threads this for you; raw-interface callers
    // pass their own handle directly.
    kcdxPluginHandle owningPlugin;

    // --- APPEND-ONLY BELOW ---------------------------------------------
    // New options fields go HERE, never mid-struct. Same append-only discipline as
    // kcdxHookOptions / the kcdxBytesInterface vtable: a mid-struct insert
    // shifts every subsequent field's offset; a plugin DLL compiled against
    // the older header would read through the wrong offset → ACCESS_VIOLATION.
} kcdxBytesOptions;

typedef struct kcdxBytesInterface {
    // ------------------------------------------------------------------
    // Register a byte rewrite. Mirrors the Lua kcdx.bytes{...} call.
    //
    // Validates the options IMMEDIATELY (exactly-one-locator, replacement
    // present, original==replacement length) and returns a handle to keep for
    // IsApplied / GetReason. A ZERO handle = registration FAILED at Register
    // time; the teaching reason is auto-logged at Error level to the engine
    // log AND the calling plugin's log (raw-interface callers get the same
    // loud-on-failure behavior as the planned wrapper users). A NON-ZERO
    // handle does NOT yet mean APPLIED — the VirtualProtect + memcpy is
    // deferred to the end-of-zone apply pass; use IsApplied(h) after it to
    // confirm, GetReason(h) for the failure reason of a rewrite that
    // registered but later failed to apply.
    kcdxBytesHandle (*Register)(const kcdxBytesOptions* opts);

    // ------------------------------------------------------------------
    // Query / control methods on a handle.
    // ------------------------------------------------------------------

    // Query whether the apply pass has written the rewrite described by `h`.
    // Returns false for an unknown handle, a still-pending handle (apply pass
    // hasn't run yet), or a handle that failed to apply. Mirrors the Lua
    // `h:applied()` query.
    bool (*IsApplied)(kcdxBytesHandle h);

    // Failure reason for a rewrite that did not apply. Returns null when `h`
    // is valid AND applied; otherwise returns a teaching-error string
    // explaining what went wrong + the fix (locator miss, byte-mismatch,
    // wrong game version, etc.). String is owned by the engine and valid for
    // the process lifetime. Mirrors the Lua `h:reason()` query.
    const char* (*GetReason)(kcdxBytesHandle h);

    // Author-supplied (or engine-default "cpp_bytes") rewrite name. Returns
    // null for an unknown handle. String is owned by the engine and valid for
    // the lifetime of the handle. Mirrors the Lua `h:name()` query.
    const char* (*GetName)(kcdxBytesHandle h);

    // Uninstall the rewrite described by `h`. A byte rewrite has NO revert
    // (the original bytes are not retained for restore), so this is DECLARED
    // for kcdxHookInterface signature parity but returns false + logs a
    // teaching line explaining bytes cannot be reverted (use a hook for
    // reversible interception). Mirrors the Lua `h:uninstall()` method.
    bool (*Uninstall)(kcdxBytesHandle h);

    // --- APPEND-ONLY BELOW (kcdxBytesInterface_Version >= 2) --------------
    // New members go HERE, at the END, never mid-struct: a plugin DLL built
    // against an older version reads the prefix members at their original
    // offsets, so appending cannot shift them (append-only ABI).
} kcdxBytesInterface;

// -----------------------------------------------------------------------------
// kcdxDeclareInterface — C++ mirror of the Lua kcdx.declare / kcdx.declared surface
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Declare,
// kcdxDeclareInterface_Version). C++ DLL plugins populate the author-declared
// track of the unified named-target table through this interface; the surface
// is feature-parity with the Lua kcdx.declare(...) / kcdx.declared(name) calls
// (ONE model, two languages, full parity at all times).
//
// SHAPE — two methods. Declare(...) is the write surface (a plugin declares a
// per-version named target under its own <author>.<plugin>.<bare> triple). Get
// (name) is the read surface for VALUE entries (the bitmask / constant form;
// PATTERN declarations are consumed by name through the hook / bytes verbs,
// not through this accessor).
//
// SAME validation, SAME store-layer dispatch as the Lua surface — Declare
// routes to the same declared-targets store the Lua binder writes to, so the
// resulting registry entries are indistinguishable by source. A C++ plugin's
// declared name resolves identically to a Lua plugin's declared name; a Lua
// plugin can hook a name a C++ plugin declared and vice versa.
//
// The author-declared store is owned by the calling plugin's
// <author>.<plugin>  namespace prefix — Declare reads the (author, plugin)
// pair off the supplied owningPlugin handle (the same self > engine > other
// precedence mechanism the hook / bytes interfaces use), so a bare name
// resolves to the calling plugin's own declaration first.
//
// Phase gating: Declare and Get both reach the declared-targets store, which
// requires the engine to be at or past the refdb-ready phase (the running
// game version string + the scan engine are reachable from there). In
// practice this means Declare / Get are safe from kcdxPlugin_Load,
// kcdxPlugin_PostGameLoad, and any kcdxMessage_* callback — they are NOT
// safe from a DllMain-time hook or a pre-engine-init context.

#define kcdxDeclareInterface_Version 1u

// One per-version entry the author passes to Declare. The kindTag string
// discriminates the payload shape AND drives the hook-mode validity gate at
// install time (kindTag == "function" — the default for a pattern entry —
// triggers the pattern-without-signature rejection; set kindTag to a non-
// function tag to declare a data slot / value the author does not intend to
// hook).
//
// Mirrors src/declared_targets.h struct VersionEntry one-to-one: the binder
// translates this POD form into the store's VersionEntry shape with no
// semantic re-interpretation.
//
//   - versionKey: the version string the author is declaring for. Exact
//     ("1.5.1164953") or wildcard ("1.5.*" / "1.*.*" — any suffix component
//     may be a bare '*'). Malformed keys are rejected by Declare with a
//     teaching error.
//   - patternStr: the AOB byte pattern at the rewrite/hook site (e.g.
//     "48 8B 05 ?? ?? ?? ?? 8B"). null or empty = no pattern (this entry
//     is a value entry, see valueInt / valueStr below).
//   - signatureStr: the ABI signature DSL ("i32 (ptr)" etc.) for a pattern
//     entry. Required when patternStr is set AND the entry is intended for
//     hook use (kindTag empty or "function"); the engine cannot infer an
//     ABI from a pattern. null = unset.
//   - kindTag: the entry-kind tag ("function" default for a pattern entry;
//     "data_slot" / "value" / etc. opt out of hook-mode usage). null =
//     engine substitutes the default per the entry's shape.
//   - valueInt / valueStr / valueIsString: the literal value the entry
//     carries (for a non-pattern value entry). valueIsString discriminates
//     the two value slots; ignored when patternStr is set. valueStr is
//     copied into the store at Declare time — caller need not retain it.
typedef struct kcdxDeclareEntry {
    const char* versionKey;        // REQUIRED — exact or wildcard
    const char* patternStr;        // null = value entry
    const char* signatureStr;      // null = no signature (rejected for hook-use patterns)
    const char* kindTag;           // null = engine substitutes default
    int64_t     valueInt;          // populated for value entries when !valueIsString
    const char* valueStr;          // populated for value entries when valueIsString; null otherwise
    bool        valueIsString;     // discriminator for valueInt vs valueStr

    // --- APPEND-ONLY BELOW ---------------------------------------------
    // New fields go HERE, never mid-struct. Same append-only discipline as
    // kcdxHookOptions / kcdxBytesOptions: a mid-struct insert shifts every
    // subsequent field's offset; a plugin DLL compiled against the older
    // header would read through the wrong offset → ACCESS_VIOLATION.
} kcdxDeclareEntry;

// Result of kcdxDeclareInterface::Get(name). Returned by value (no heap, no
// out-param). `found` discriminates a Value hit from every miss case
// (Kind::NoEntry / Kind::VersionMismatch / Kind::Pattern in the store's
// terms) — PATTERN entries also return found == false from this accessor
// because they have no value payload to surface; consume them through the
// hook / bytes verbs.
//
// `isString` discriminates the two payload slots when found is true; one of
// `intValue` / `stringValue` is meaningful, the other is left at the default.
//
// `stringValue` points into the declared-targets store's owned std::string
// storage, which uses node-stable container backing (each entry sits in a
// stable container node). The pointer survives a subsequent Declare call on
// a DIFFERENT (author, plugin, name) triple from any plugin — the
// node-stable storage guarantees prior nodes never move when new triples
// append. A re-Declare of the SAME (author, plugin, name) triple from your
// own plugin currently invalidates every prior `stringValue` you cached for
// that name; re-Get after re-Declaring. The same-triple invalidation will
// be removed in a follow-up change that routes valueStr storage through a
// process-lifetime arena; at that point the pointer becomes valid for the
// process lifetime unconditionally. The pointer is NULL when isString is
// false (an integer-valued entry) or when found is false (a miss).
typedef struct kcdxDeclaredValue {
    bool        found;             // true iff Get found a VALUE entry on the running version
    bool        isString;          // discriminates intValue vs stringValue when found
    int64_t     intValue;          // populated when found && !isString
    const char* stringValue;       // populated when found && isString; lifetime per the contract above; null otherwise

    // --- APPEND-ONLY BELOW ---------------------------------------------
    // New fields go HERE, never mid-struct.
} kcdxDeclaredValue;

typedef struct kcdxDeclareInterface {
    // ------------------------------------------------------------------
    // Declare a per-version named target. Same write surface as the Lua
    // kcdx.declare(module, name, versions_kv) call.
    //
    //   module     — REQUIRED. The module the declared target lives in
    //                (e.g. "WHGame.dll"). No default — a defaulted module
    //                silently misroutes when secondary modules become a
    //                concern.
    //   bareName   — REQUIRED. The bare name the plugin is declaring. The
    //                engine stamps it as <author>.<plugin>.<bareName>
    //                from the owningPlugin's manifest, matching the Lua
    //                binder. Charset [a-z0-9_], 2..128 chars.
    //   entries    — REQUIRED. Pointer to an array of count
    //                kcdxDeclareEntry rows (the per-version table). The
    //                engine COPIES every field it needs at Declare time;
    //                the caller need not retain the array or its string
    //                contents after Declare returns. Must be non-null
    //                and count must be > 0 (an empty declaration is an
    //                author bug — Declare rejects with a teaching error).
    //   count      — the number of entries in the array.
    //   owningPlugin — REQUIRED. Drives the <author>.<plugin> prefix
    //                under which the bare name is registered, AND the
    //                self-tier of self > engine > other for any later
    //                resolution by the calling plugin. Pass the author's
    //                own handle (from kcdxInterface::GetPluginHandle on
    //                the manifest's [plugin].name). Passing
    //                kcdxInvalidPluginHandle rejects the declaration —
    //                an unattributed declaration has nowhere to live in
    //                the precedence walk (same rule as the Lua binder).
    //
    // Returns true on accept; false on reject. The engine logs a teaching
    // error under category "DECLARED_TARGET" / "DECLARED_TARGET_BIND" on
    // every reject path — the author reads the cause in the dev log.
    //
    // Idempotent per (author, plugin, bareName): a second Declare with the
    // same triple REPLACES the first cleanly (the prior memoization is
    // dropped so the new declaration resolves fresh).
    //
    // Launch-time only — Declare is intended to be called from
    // kcdxPlugin_Load. The author-declared store is built during plugin
    // load; calling Declare from a DllMain-time hook or before refdb is
    // open trips the engine's phase gate.
    bool (*Declare)(const char* module,
                    const char* bareName,
                    const kcdxDeclareEntry* entries,
                    size_t count,
                    kcdxPluginHandle owningPlugin);

    // ------------------------------------------------------------------
    // Read a declared VALUE entry's payload. Same read surface as the
    // Lua kcdx.declared(name) call.
    //
    //   name        — either a bare 1-segment name (resolves against the
    //                 calling plugin's own declarations — the SELF tier
    //                 only) OR a 3-segment "<author>.<plugin>.<bare>"
    //                 explicit form (resolves against the named plugin's
    //                 declared store directly, mirroring the Lua binder).
    //                 No other dot-count is meaningful for declared-value
    //                 reads — anything else returns a miss.
    //   owningPlugin — drives the SELF tier of the 1-segment lookup
    //                  (the (author, plugin) is read off the handle, same
    //                  as Declare). Passing kcdxInvalidPluginHandle on a
    //                  1-segment name reads an empty owner (no self tier);
    //                  the 3-segment explicit form is unaffected by the
    //                  owner.
    //
    // Returns a kcdxDeclaredValue. `found == true` iff the name resolved
    // to a VALUE entry on the running game version. PATTERN entries,
    // NoEntry, and VersionMismatch all return `found == false` — PATTERN
    // entries are consumed by name through the hook / bytes verbs, not
    // through this accessor.
    //
    // The C++ side carries no LUA_NUMBER=float precision threshold: the
    // 2^24-mantissa rounding the Lua kcdx.declared accessor faces is a
    // CryEngine Lua VM property, not a kcdx limitation. Integer values
    // are surfaced as the full int64_t.
    //
    // Read-only; idempotent; safe to call many times per launch.
    kcdxDeclaredValue (*Get)(const char* name, kcdxPluginHandle owningPlugin);

    // --- APPEND-ONLY BELOW (kcdxDeclareInterface_Version >= 2) -----------
    // New members go HERE, at the END, never mid-struct: a plugin DLL built
    // against an older version reads the prefix members at their original
    // offsets, so appending cannot shift them (append-only ABI).
} kcdxDeclareInterface;

// -----------------------------------------------------------------------------
// kcdxAssetInterface — C++ mirror of the Lua kcdx.assets.* surface
// -----------------------------------------------------------------------------
//
// Fetched via kcdxInterface::QueryInterface(kcdxInterface_Assets,
// kcdxAssetInterface_Version). The C++ author's mirror of the programmatic
// asset surface (the no-code `replaces.toml` sidecar is language-neutral and
// works the same for a C++ plugin — see the asset-replacement docs; this
// interface is the IN-CODE replace/declare/register/resolve path). Full Lua
// <-> C++ parity (ONE model, two languages): every verb produces the SAME
// result as its Lua peer (kcdx.assets.get_by_path / get_by_name / declare /
// register / replace), routed through the SAME engine-side resolution + the
// SAME runtime stores the Lua binder writes — a C++-declared name and a Lua-
// declared name are indistinguishable; a Lua plugin resolves a name a C++
// plugin published and vice versa.
//
// RETURN SHAPE — every method returns `const char*`: the resolved LOADABLE
// PATH on success (the absolute on-disk path the asset-resolution seam opens
// to serve the file — exactly the value the Lua peer returns), or `nullptr`
// on failure. The teaching error is LOGGED to the dev log (the ASSET_GET /
// ASSET_RUNTIME structured lines the engine already emits on every failure
// path), NOT handed back in code — the path is used directly by the author;
// the error teaches via the dev log (the C++ author's native channel, the
// same convention as kcdxConsoleInterface::GetCVar* and
// kcdxDeclareInterface::Declare). A nullptr return with a dev-log line is the
// loud failure (never a silent empty string).
//
// OWN vs CROSS-PLUGIN — every method here is the author's OWN-namespace form:
// the engine resolves the CALLING plugin from `self` (the owningPlugin
// handle), so the author never types their own <author>.<plugin> prefix.
// The Lua kcdx.plugin.<a>.<p>.assets.* navigable cross-plugin READ form has no
// C++ navigable-namespace analogue; a C++ author reaches another mod's
// published asset by passing its packed "<author>.<plugin>.<bare>" name to
// Replace's `target` (the string-key cross-plugin form, design §5.3) — the
// same packed-name route the Lua kcdx.assets.replace cross-mod target uses.
//
// Phase gating: the runtime stores are reachable from kcdxPlugin_Load (the
// same init phase the Lua binder reaches synchronously); Declare / Register /
// Replace are intended to be called from kcdxPlugin_Load. GetByPath is a pure
// read with no store dependency.

#define kcdxAssetInterface_Version 1u

typedef struct kcdxAssetInterface {
    // ------------------------------------------------------------------
    // Resolve YOUR OWN asset (a path relative to your plugin's assets/
    // folder) to its loadable on-disk path. The common path — the C++
    // spelling of Lua kcdx.assets.get_by_path(path). A PURE READ: it
    // mutates no store and depends on none.
    //
    //   self  — REQUIRED. Your own plugin handle (from
    //           api->GetPluginHandle("<[plugin].name>")). The engine
    //           resolves the calling plugin from it — you never type your
    //           own <author>.<plugin> prefix.
    //   path  — REQUIRED. The path to your asset, relative to your assets/
    //           folder (e.g. "icons/my_icon.dds"). '..' traversal is
    //           rejected.
    //
    // Returns the LOADABLE PATH (the absolute disk path the seam opens) on
    // success; nullptr on failure (no such asset under your assets/, no
    // assets/ entrypoint declared, an unattributed self, or a '..' escape).
    // The teaching error naming the missing path is in the dev log (category
    // ASSET_GET) — never a silent nullptr for a typo.
    //
    //   const char* icon = K.assets->GetByPath(self, "icons/my_icon.dds");
    //   if (icon) { /* hand `icon` to a game asset API */ }
    //   else      { /* the dev log says exactly which path missed */ }
    const char* (*GetByPath)(kcdxPluginHandle self, const char* path);

    // ------------------------------------------------------------------
    // Resolve a name YOU published (with Declare, below) to its loadable
    // path. The C++ spelling of Lua kcdx.assets.get_by_name(name) — the
    // read peer of Declare.
    //
    //   self  — REQUIRED. Your own plugin handle (resolves your namespace).
    //   name  — REQUIRED. The bare name you published (e.g. "shirt"). The
    //           engine resolves it against YOUR OWN <author>.<plugin>
    //           namespace — you type only the bare name.
    //
    // Returns the published asset's loadable path on success; nullptr on a
    // name you never declared (the teaching error naming it is in the dev
    // log, category ASSET_GET) — never a silent nullptr for a typo.
    const char* (*GetByName)(kcdxPluginHandle self, const char* name);

    // ------------------------------------------------------------------
    // Publish a stable NAME for one of your assets as a shared contract.
    // The C++ spelling of Lua kcdx.assets.declare(name, file). Publishes
    // YOUR <author>.<plugin>.<name> -> the resolved disk path of `file`,
    // so another mod can reference your asset by name.
    //
    //   self  — REQUIRED. Your own plugin handle (the namespace to publish
    //           into).
    //   name  — REQUIRED. The bare published name (e.g. "shirt"). The engine
    //           stamps it as <author>.<plugin>.<name>; you type only the bare
    //           name.
    //   file  — REQUIRED. The file the name names, relative to your assets/
    //           folder (resolved like GetByPath — '..'-reject + must exist).
    //
    // Returns the declared file's LOADABLE PATH on success — the SAME value a
    // later GetByName(self, name) yields, so you can use it immediately AND
    // publish it in one call (mirrors the Lua declare's path-return). nullptr
    // on failure (unresolvable file, anonymous self, empty name/file); the
    // teaching error is in the dev log (category ASSET_GET / ASSET_RUNTIME).
    const char* (*Declare)(kcdxPluginHandle self, const char* name,
                           const char* file);

    // ------------------------------------------------------------------
    // Make a not-at-load asset available at a runtime virtual path. The C++
    // spelling of Lua kcdx.assets.register(vpath, file). Writes a runtime
    // overlay: the engine serves `file` where it opens `vpath` AFTER this
    // call (take-effect = thereafter).
    //
    //   self  — REQUIRED. Your own plugin handle.
    //   vpath — REQUIRED. The virtual path the game opens (e.g.
    //           "Libs/UI/Textures/MyGen.dds").
    //   file  — REQUIRED. The file that serves it, relative to your assets/
    //           folder (resolved like GetByPath).
    //
    // Returns the loadable path of `file` on success; nullptr on failure
    // (unresolvable file, anonymous self, empty arg). The teaching error is
    // in the dev log (category ASSET_GET / ASSET_RUNTIME).
    const char* (*Register)(kcdxPluginHandle self, const char* vpath,
                            const char* file);

    // ------------------------------------------------------------------
    // Register a runtime REPLACEMENT keyed by a target. The C++ spelling of
    // Lua kcdx.assets.replace(target, with). Two target forms (the engine
    // disambiguates, mirroring the Lua replace):
    //   * a VANILLA asset path ("Libs/UI/Textures/KCDLogo.dds") — keys the
    //     runtime overlay by that vpath directly.
    //   * a packed CROSS-MOD published name ("<author>.<plugin>.<bare>") —
    //     resolves the name to the publisher's serve-vpath (design §5.3) and
    //     keys the overlay by THAT, so your `with` wins where the other mod's
    //     published asset serves. The packed-name route is also how a C++
    //     author replaces another mod's asset (no navigable-namespace form).
    //
    //   self   — REQUIRED. Your own plugin handle.
    //   target — REQUIRED. The vanilla vpath OR the packed cross-mod name.
    //   file   — REQUIRED. The replacement file, relative to your assets/
    //            folder (resolved like GetByPath).
    //
    // Returns the loadable path of `file` on success; nullptr on failure
    // (unresolvable file, an unresolvable packed cross-mod target — the owner
    // never published that name — anonymous self, empty arg). The teaching
    // error naming the unresolved target / missing file is in the dev log
    // (category ASSET_GET / ASSET_RUNTIME) — never a silent overlay write the
    // resolver could never hit.
    const char* (*Replace)(kcdxPluginHandle self, const char* target,
                           const char* file);

    // --- APPEND-ONLY BELOW (kcdxAssetInterface_Version >= 2) -------------
    // New members go HERE, at the END, never mid-struct: a plugin DLL built
    // against an older version reads the prefix members at their original
    // offsets, so appending cannot shift them (append-only ABI).
} kcdxAssetInterface;

// -----------------------------------------------------------------------------
// Plugin entry points (you export these from your DLL)
// -----------------------------------------------------------------------------
//
// Both functions are OPTIONAL but at least one must be present in a C++
// plugin's DLL. Pure-TOML plugins (no DLL) don't export anything; they're
// pure declarative and apply via kcdx's patch/hook engines.
//
// kcdxPlugin_Preload runs in the "preload wave" before any plugin's Load.
// Use it only for registering symbols or state that another plugin's Load
// might depend on. Most plugins should not need Preload.
//
// kcdxPlugin_Load runs in the load wave, after every Preload has returned.
// At this point all other plugins are visible (GetPluginInfo works for any
// loaded plugin). Register listeners, install hooks, call into peers here.
//
// kcdxPlugin_PostGameLoad (OPTIONAL) runs LATER than Load — in the
// after_game phase at the first update tick, NOT during the load wave.
// It is the C++ mirror of the Lua `lua_after` entrypoint slot: it fires
// AFTER all before-game work has been applied (every plugin's hooks +
// byte patches are live) and BEFORE kcdxMessage_InputLoaded. All
// PostGameLoad exports run in LOAD-ORDER PRIORITY (the plugin's
// resolved load_order priority, ties broken by name), the same order
// as lua_after. Use it for work that must observe before-game state —
// e.g. reading a value a hook installed at load time now affects, or
// initialization that depends on the game's after-phase being live.
// The full C++ lifecycle is: Preload -> Load -> [before-work applied]
// -> PostGameLoad. It is resolved as an optional export on the SAME
// plugin DLL as Preload/Load (one DLL, three optional exports) — there
// is no separate "after" DLL file.
//
// Plugin self-identity: at load time, your DLL doesn't yet know its own
// kcdxPluginHandle. Call `api->GetPluginHandle("your.stable.name")` to
// resolve it, where the name string must match what you declared in your
// kcdx.toml [plugin] name field. Cache the handle for later use.

// Return true on success. A false return is logged but the DLL is not
// unloaded (Windows reclaims at process exit; kcdx, like SKSE, does not
// FreeLibrary).
typedef bool (*kcdxPlugin_Preload_t)     (const kcdxInterface* api);
typedef bool (*kcdxPlugin_Load_t)        (const kcdxInterface* api);
typedef bool (*kcdxPlugin_PostGameLoad_t)(const kcdxInterface* api);

// -----------------------------------------------------------------------------
// Helper: pack a KCD2 build number into the encoded form used by
// kcdxPluginInfo.runtimeCompatibleGameVersion and kcdxInterface::
// runtimeGameVersion.
// -----------------------------------------------------------------------------
//
// Encoding: (major << 24) | (minor << 16) | (build_lo16).
// E.g. 1.5.1164953 → (1 << 24) | (5 << 16) | (1164953 & 0xFFFF) = 0x010579D9.
//
// The build_lo16 truncation is intentional — KCD2 builds use the lower 16 bits
// of the patch revision as the disambiguator and the upper bits never matter
// for compatibility checks.
//
// kcdx.toml authors write the human-readable string ("1.5.1164953") in their
// [plugin] compatible_game_versions array and the engine parses it.
#define kcdxMakeGameVersion(major, minor, build) \
    ((uint32_t)((((major) & 0xFFu) << 24) | (((minor) & 0xFFu) << 16) | ((build) & 0xFFFFu)))

#ifdef __cplusplus
}  // extern "C"
#endif

// -----------------------------------------------------------------------------
// kcdxLogger — ergonomic C++ wrapper over kcdxInterface::Log
// -----------------------------------------------------------------------------
//
// Plugin authors: build one of these once you have an api pointer +
// your plugin handle. Then log with short member-function calls.
//
//   static kcdxLogger gLog;
//
//   bool kcdxPlugin_Load(const kcdxInterface* api) {
//       gLog = kcdxLogger(api, api->GetPluginHandle("my.plugin"));
//
//       gLog.Info ("INIT",      "loaded, engine v0x%08X", api->kcdxVersion);
//       gLog.Warn ("MESSAGING", "Messaging interface unavailable");
//       gLog.Error("INIT",      "RegisterFunction failed: %s", err);
//       return true;
//   }
//
// Routing rules (severity gates, plugin file inclusion, dev-log
// behavior) are documented in kcdx/docs/logging.md. Short version:
//
//   - INFO / WARN / ERROR always reach kcdx.log AND this plugin's file.
//   - WARN / ERROR ALWAYS reach this plugin's file, even if you set
//     log_level = "off" in your kcdx.toml.
//   - DEBUG / TRACE reach this plugin's file only when allowed by
//     your manifest log_level OR when the user enabled dev mode in
//     engine.toml.
//
// The wrapper is header-only and zero-allocation per call (formats
// into a 1 KiB stack buffer, truncates if exceeded). Each call costs
// one indirect call + vsnprintf.
//
// Safe to call from any thread. Default-constructed loggers (no api
// pointer set) are no-ops — useful if you want a `kcdxLogger gLog;`
// at file scope and assign it during Plugin_Load.
//
// Construct with a different handle if you have a reason to log on
// behalf of another plugin (rare; usually you pass your own handle).

#ifdef __cplusplus

#include <cstdarg>
#include <cstdio>

struct kcdxLogger {
    const kcdxInterface* api = nullptr;
    kcdxPluginHandle     self = kcdxInvalidPluginHandle;

    kcdxLogger() = default;
    kcdxLogger(const kcdxInterface* a, kcdxPluginHandle s)
        : api(a), self(s) {}

    // Convenience: did the user wire this logger up?
    bool ready() const { return api != nullptr; }

    void Trace(const char* category, const char* fmt, ...) const {
        if (!api) return;
        char buf[1024];
        va_list args; va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        api->Log(self, kcdxLog_Trace, category, buf);
    }
    void Debug(const char* category, const char* fmt, ...) const {
        if (!api) return;
        char buf[1024];
        va_list args; va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        api->Log(self, kcdxLog_Debug, category, buf);
    }
    void Info(const char* category, const char* fmt, ...) const {
        if (!api) return;
        char buf[1024];
        va_list args; va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        api->Log(self, kcdxLog_Info, category, buf);
    }
    void Warn(const char* category, const char* fmt, ...) const {
        if (!api) return;
        char buf[1024];
        va_list args; va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        api->Log(self, kcdxLog_Warn, category, buf);
    }
    void Error(const char* category, const char* fmt, ...) const {
        if (!api) return;
        char buf[1024];
        va_list args; va_start(args, fmt);
        vsnprintf(buf, sizeof(buf), fmt, args);
        va_end(args);
        api->Log(self, kcdxLog_Error, category, buf);
    }
};

#endif  // __cplusplus
