// kcdx/Interfaces.h — public plugin API
//
// SKSE-shaped extender interface for Kingdom Come: Deliverance II.
// Plugin authors:
//   1. #include this header
//   2. Export `kcdxPluginVersionData` as a data symbol describing your plugin
//   3. Export `kcdxPlugin_Load` as a function the engine calls at load time
//   4. Optionally export `kcdxPlugin_Preload` for early-phase setup
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
// kcdxPluginVersionData — the exported metadata block
// -----------------------------------------------------------------------------
//
// Every C++ plugin DLL must export an instance of this struct as a data
// symbol with the literal name `kcdxPluginVersionData`. The engine reads it
// at discovery time (before any plugin code runs) to validate compatibility,
// check dependencies, and decide load order.

// Bitfield flags for kcdxPluginVersionData::versionIndependence.
enum kcdxVersionIndependence {
    // Plugin uses kcdx::ResolveAddress() for runtime offset lookups; skip the
    // strict compatibleGameVersions check. Required if compatibleGameVersions
    // is empty.
    kcdxVersionIndependent_AddressLibrary = 1u << 0,

    // Reserved for future use. Set zero.
    kcdxVersionIndependent_StructsPostAE = 1u << 1,
};

// One entry in a plugin's optional dependency array.
typedef struct kcdxPluginDependency {
    const char* name;          // Other plugin's stable name (their kcdxPluginVersionData.name)
    uint32_t    minVersion;    // Their pluginVersion must be >= this
    uint32_t    flags;         // Bit 0: optional (load anyway if missing)
} kcdxPluginDependency;

#define kcdxDependencyFlag_Optional (1u << 0)

// Current data block version. Bump when adding fields to kcdxPluginVersionData.
#define kcdxPluginVersionData_CurrentVersion 1u

typedef struct kcdxPluginVersionData {
    uint32_t dataVersion;              // Must equal kcdxPluginVersionData_CurrentVersion
    uint32_t pluginVersion;            // Your plugin's own integer version

    char     name[256];                // STABLE PLUGIN ID. Must be unique across all loaded
                                       // plugins. Used as messaging sender identity,
                                       // serialization record key, dependency lookup target.
                                       // Convention: "author.mod-name". NOT the filename.

    char     author[256];
    char     supportEmail[252];

    uint32_t versionIndependenceEx;    // Reserved for forward compat. Set 0.
    uint32_t versionIndependence;      // Bitfield of kcdxVersionIndependence flags.

    uint32_t compatibleGameVersions[16];  // KCD2 build numbers this plugin tested against.
                                          // Encoded as packed BCD-ish (e.g. 0x010505BC for
                                          // 1.5.1164953 — see notes in design.md).
                                          // Zero-terminated array. Empty means
                                          // "any version" — valid only if
                                          // versionIndependence has AddressLibrary set.

    uint32_t kcdxVersionRequired;      // Minimum kcdx engine version (e.g. 0x00010000 = 0.1.0)

    uint32_t reserved[8];              // Pad for future fields. Set zero.

    // Optional: inline declarative patches. If non-null, parsed by the loader
    // BEFORE kcdxPlugin_Load fires. Lets a C++ plugin ship its byte rewrites
    // alongside its DLL without needing a sidecar kcdx.toml. Same schema as
    // a kcdx.toml file (just the `[[patch]]` entries).
    const char* inlinePatchesToml;     // Nullable. Null-terminated.

    // Optional: dependencies. Array of {name, minVersion, flags} terminated by
    // an entry with name == nullptr. Loader topologically sorts the plugin
    // load order before issuing kcdxPlugin_Load calls.
    const kcdxPluginDependency* dependencies;  // Nullable.
} kcdxPluginVersionData;

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
    const kcdxPluginVersionData* (*GetPluginInfo)(const char* name);

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
// Plugin entry points (you export these from your DLL)
// -----------------------------------------------------------------------------
//
// Both functions are OPTIONAL but at least one must be present, OR your
// kcdxPluginVersionData.inlinePatchesToml must be non-null (a pure
// declarative-patches plugin).
//
// kcdxPlugin_Preload runs in the "preload wave" before any plugin's Load.
// Use it only for registering symbols or state that another plugin's Load
// might depend on. Most plugins should not need Preload.
//
// kcdxPlugin_Load runs in the load wave, after every Preload has returned.
// At this point all other plugins are visible (GetPluginInfo works for any
// loaded plugin). Register listeners, install hooks, call into peers here.

// Return true on success. A false return is logged but the DLL is not
// unloaded (Windows reclaims at process exit; kcdx, like SKSE, does not
// FreeLibrary).
typedef bool (*kcdxPlugin_Preload_t)(const kcdxInterface* api);
typedef bool (*kcdxPlugin_Load_t)   (const kcdxInterface* api);

// -----------------------------------------------------------------------------
// Helper: pack a KCD2 build number into the encoded form
// kcdxPluginVersionData.compatibleGameVersions expects.
// -----------------------------------------------------------------------------
//
// Encoding: (major << 24) | (minor << 16) | (build_lo16).
// E.g. 1.5.1164953 → (1 << 24) | (5 << 16) | (1164953 & 0xFFFF) = 0x010579D9.
//
// The build_lo16 truncation is intentional — KCD2 builds use the lower 16 bits
// of the patch revision as the disambiguator and the upper bits never matter
// for compatibility checks.
#define kcdxMakeGameVersion(major, minor, build) \
    ((uint32_t)((((major) & 0xFFu) << 24) | (((minor) & 0xFFu) << 16) | ((build) & 0xFFFFu)))

#ifdef __cplusplus
}  // extern "C"
#endif
