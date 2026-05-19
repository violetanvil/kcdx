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
//
// See ../docs/design.md in the kcdx repo for the full design spec, schema,
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
    const char* name;              // Stable plugin ID. Never null.
    const char* displayName;       // UI name. Never null; falls back to `name` if not declared.
    const char* author;
    const char* description;
    const char* url;
    const char* supportEmail;
    uint32_t    version;           // Packed semver (0xMMmmpp00)
    uint32_t    kcdxMinVersion;
    uint32_t    runtimeCompatibleGameVersion;  // The matched entry from compatible_game_versions,
                                                // or 0 if version_independent
    int         versionIndependent;            // 0/1
} kcdxPluginInfo;

// -----------------------------------------------------------------------------
// kcdxInterface — the root API a plugin receives at load
// -----------------------------------------------------------------------------

// Sub-interface identifiers passed to kcdxInterface::QueryInterface.
enum kcdxInterfaceID {
    kcdxInterface_Messaging      = 1,  // Phase 3
    kcdxInterface_Trampoline     = 2,  // Phase 4
    kcdxInterface_Task           = 3,  // Phase 3
    kcdxInterface_Scripting      = 4,  // Phase 5
    kcdxInterface_Serialization  = 5,  // Phase 6
};

// Log levels passed to kcdxInterface::Log. Match the severities the engine
// itself uses; the engine routes Log(handle, level, msg) into the plugin's
// own log file at <plugin-folder>/<folder-name>.log with the plugin's
// stable name prepended as `[name] ...`.
enum kcdxLogLevel {
    kcdxLog_Info  = 0,
    kcdxLog_Warn  = 1,
    kcdxLog_Error = 2,
    kcdxLog_Debug = 3,
};

// Root accessor. The engine passes a const pointer to one of these to your
// kcdxPlugin_Preload and kcdxPlugin_Load functions. Treat as read-only.
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
    // Library has it as `removed` for this version. Phase 7+.
    uintptr_t (*ResolveAddress)(uint64_t id);

    // Write a line to this plugin's own log file at
    //   <plugins-dir>/<plugin-folder>/<plugin-folder>.log
    //
    // The engine handles file lifecycle (lazy-open on first call, truncate on
    // session start) and a hard 20 MB size cap per file (further writes get
    // silently dropped, with one warning to kcdx.log naming the offending
    // file). The line is automatically prefixed with the plugin's stable
    // name so multiple plugins can identify themselves if they share a log
    // surface for any reason.
    //
    // `msg` should be a UTF-8 null-terminated C string. Newlines are added
    // by the engine — don't append your own.
    //
    // Safe to call from any thread.
    void (*Log)(kcdxPluginHandle self, uint32_t level, const char* msg);

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

    // Save selected, before engine reads it. `data` points to the save name
    // as a null-terminated C string. NOT YET FIRED (Phase 6).
    kcdxMessage_PreLoadGame   = 5,

    // Save finished loading, world is interactive. `data` = save name string.
    // NOT YET FIRED (Phase 6).
    kcdxMessage_PostLoadGame  = 6,

    // Game being saved (manual or quicksave). `data` = save name string.
    // NOT YET FIRED (Phase 6).
    kcdxMessage_SaveGame      = 7,

    // A save plus its .kcdx co-save being deleted. `data` = save name string.
    // NOT YET FIRED (Phase 6).
    kcdxMessage_DeleteGame    = 8,

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

#define kcdxTrampolineInterface_Version 1u

typedef struct kcdxTrampolineInterface {
    // Allocate `size` bytes of executable memory within ±2 GB of WHGame.dll's
    // .text. Returns null if `size` is zero, the pool is exhausted, or no
    // free region within range exists. Plugin owns the returned pointer for
    // the lifetime of the process — no Free function (matches SKSE's model).
    void* (*AllocateFromBranchPool)(kcdxPluginHandle owner, size_t size);

    // Allocate `size` bytes of executable memory anywhere. Returns null on
    // failure (typically: out of memory).
    void* (*AllocateFromLocalPool)(kcdxPluginHandle owner, size_t size);
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
// vtable on a game-side class" trick (subagent research, 2026-05-18).
//
// So this interface ships the ~30 lua_* / luaL_* calls a plugin
// actually needs as function pointers. Plugins:
//
//   #include "kcdx/Interfaces.h"
//   ...
//   static int Lua_Greet(struct lua_State* L, void* ud) {
//       auto* lua = static_cast<const kcdxLuaApi*>(ud);
//       const char* name = lua->ToString(L, 1, nullptr);
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

// Lua C API surface available to plugin functions. Pointer signatures
// match Lua 5.1's API verbatim, so plugin code reads like raw lua.h.
// Members are function pointers because KCD2 ships Lua 5.1 inside
// kcdx.asi (no exported symbols, no game-side vtable).
//
// Naming: PascalCase (kcdx convention) — `PushString` not `pushstring` —
// so plugin code visually differs from any in-process lua.h
// includes, and to match the rest of kcdxInterface methods.
//
// Each function name's underlying lua_* is in a comment for grep'ability.
typedef struct kcdxLuaApi {
    // --- stack inspection ---
    int         (*GetTop)        (struct lua_State* L);                                   // lua_gettop
    void        (*SetTop)        (struct lua_State* L, int idx);                          // lua_settop
    void        (*PushValue)     (struct lua_State* L, int idx);                          // lua_pushvalue
    void        (*Remove)        (struct lua_State* L, int idx);                          // lua_remove
    void        (*Insert)        (struct lua_State* L, int idx);                          // lua_insert
    void        (*Replace)       (struct lua_State* L, int idx);                          // lua_replace
    int         (*CheckStack)    (struct lua_State* L, int n);                            // lua_checkstack

    // --- type queries ---
    int         (*Type)          (struct lua_State* L, int idx);                          // lua_type
    int         (*IsNumber)      (struct lua_State* L, int idx);                          // lua_isnumber
    int         (*IsString)      (struct lua_State* L, int idx);                          // lua_isstring
    int         (*IsBoolean)     (struct lua_State* L, int idx);                          // lua_isboolean
    int         (*IsNil)         (struct lua_State* L, int idx);                          // lua_isnil
    int         (*IsCFunction)   (struct lua_State* L, int idx);                          // lua_iscfunction
    int         (*IsTable)       (struct lua_State* L, int idx);                          // lua_istable
    int         (*IsFunction)    (struct lua_State* L, int idx);                          // lua_isfunction
    int         (*IsUserdata)    (struct lua_State* L, int idx);                          // lua_isuserdata

    // --- pull values from stack ---
    const char* (*ToString)      (struct lua_State* L, int idx);                          // lua_tostring
    const char* (*ToLString)     (struct lua_State* L, int idx, size_t* len);             // lua_tolstring
    double      (*ToNumber)      (struct lua_State* L, int idx);                          // lua_tonumber
    long long   (*ToInteger)     (struct lua_State* L, int idx);                          // lua_tointeger (lua_Integer is ptrdiff_t on 5.1)
    int         (*ToBoolean)     (struct lua_State* L, int idx);                          // lua_toboolean
    const void* (*ToPointer)     (struct lua_State* L, int idx);                          // lua_topointer
    void*       (*ToUserdata)    (struct lua_State* L, int idx);                          // lua_touserdata

    // --- push values onto stack ---
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
    // it inside kcdx. See kcdx/docs/lua-number-precision.md for the
    // probe data and kcdx/CLAUDE.md hard rule #17.
    void        (*PushString)    (struct lua_State* L, const char* s);                    // lua_pushstring
    void        (*PushLString)   (struct lua_State* L, const char* s, size_t len);        // lua_pushlstring
    void        (*PushNumber)    (struct lua_State* L, double n);                         // lua_pushnumber (precision-lossy; see caveat above)
    void        (*PushInteger)   (struct lua_State* L, long long n);                      // lua_pushinteger (precision-lossy; see caveat above)
    void        (*PushBoolean)   (struct lua_State* L, int b);                            // lua_pushboolean
    void        (*PushNil)       (struct lua_State* L);                                   // lua_pushnil
    void        (*PushCFunction) (struct lua_State* L, kcdxLuaCFunction fn, void* ud);    // lua_pushcclosure with one upvalue (the ud)
    void        (*PushLightUserdata)(struct lua_State* L, void* p);                       // lua_pushlightuserdata (exact for pointers — preferred over PushInteger for VAs)

    // --- tables ---
    void        (*NewTable)      (struct lua_State* L);                                   // lua_newtable
    void        (*GetField)      (struct lua_State* L, int idx, const char* k);           // lua_getfield
    void        (*SetField)      (struct lua_State* L, int idx, const char* k);           // lua_setfield
    void        (*RawGetI)       (struct lua_State* L, int idx, int n);                   // lua_rawgeti
    void        (*RawSetI)       (struct lua_State* L, int idx, int n);                   // lua_rawseti
    void        (*GetGlobal)     (struct lua_State* L, const char* name);                 // lua_getglobal (5.1 macro: getfield+globalsindex)
    void        (*SetGlobal)     (struct lua_State* L, const char* name);                 // lua_setglobal

    // --- error reporting ---
    int         (*Error)         (struct lua_State* L);                                   // lua_error  (call only with a value already on the stack)
    int         (*ErrorF)        (struct lua_State* L, const char* fmt, ...);             // luaL_error (varargs convenience)
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
// Plugin self-identity: at load time, your DLL doesn't yet know its own
// kcdxPluginHandle. Call `api->GetPluginHandle("your.stable.name")` to
// resolve it, where the name string must match what you declared in your
// kcdx.toml [plugin] name field. Cache the handle for later use.

// Return true on success. A false return is logged but the DLL is not
// unloaded (Windows reclaims at process exit; kcdx, like SKSE, does not
// FreeLibrary).
typedef bool (*kcdxPlugin_Preload_t)(const kcdxInterface* api);
typedef bool (*kcdxPlugin_Load_t)   (const kcdxInterface* api);

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
