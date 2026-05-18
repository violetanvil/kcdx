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
} kcdxInterface;

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
