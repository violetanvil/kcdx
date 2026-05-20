# kcdx v0.1 — design

> **Status:** authoritative spec for the v0.1 release. Updated as
> implementation lands. The plan-mode plan that produced this design
> lives in the author's private `~/.claude/plans/` dir; this doc is
> the public-facing successor.

kcdx is the SKSE-class extender for Kingdom Come: Deliverance II.
Sibling project to [kcd2-mempatch][mp] (which handles declarative
same-length byte rewrites only). kcdx covers everything else: code
injection, plugin lifecycle, save serialization, console commands,
inter-plugin messaging.

The plugin API mirrors SKSE / F4SE conventions verbatim with `kcdx`
substituted for `SKSE`. Authors who've shipped an SKSE plugin should
recognize the entire surface in an hour. Where SKSE has documented
weak spots — no inter-plugin conflict detection, no declarative
escape hatch for non-coders, hidden plugin enumeration — kcdx
enhances.

[mp]: https://github.com/violetanvil/kcd2-mempatch

---

## Contents

1. [Plugin model overview](#plugin-model-overview)
2. [The C++ DLL path](#the-c-dll-path)
   - [Folder layout](#folder-layout)
   - [`kcdxPluginVersionData` exported data block](#kcdxpluginversiondata-exported-data-block)
   - [Entry points: `kcdxPlugin_Preload` and `kcdxPlugin_Load`](#entry-points-kcdxplugin_preload-and-kcdxplugin_load)
   - [Plugin lifecycle and load order](#plugin-lifecycle-and-load-order)
3. [The declarative TOML path](#the-declarative-toml-path)
   - [Coexistence with the C++ path](#coexistence-with-the-c-path)
   - [Schema overview](#schema-overview)
4. [TOML schema reference](#toml-schema-reference)
   - [`[kcdx]` top-level options](#kcdx-top-level-options)
   - [`[[patch]]` — same-length byte rewrite](#patch--same-length-byte-rewrite)
   - [`[[hook]]` — function-entry detour](#hook--function-entry-detour)
   - [`[[mid_hook]]` — mid-function hook](#mid_hook--mid-function-hook)
   - [`[[trampoline]]` — named executable region](#trampoline--named-executable-region)
   - [`[[scan]]` — diagnostic locator resolve (no apply)](#scan--diagnostic-locator-resolve-no-apply)
   - [`[[command]]` — console command](#command--console-command)
   - [`[[event]]` — lifecycle event subscription](#event--lifecycle-event-subscription)
5. [Plugin interfaces (C++)](#plugin-interfaces-c)
   - [`kcdxInterface`](#kcdxinterface)
   - [`kcdxMessagingInterface`](#kcdxmessaginginterface)
   - [`kcdxTrampolineInterface`](#kcdxtrampolineinterface)
   - [`kcdxTaskInterface`](#kcdxtaskinterface)
   - [`kcdxScriptingInterface`](#kcdxscriptinginterface)
   - [`kcdxSerializationInterface`](#kcdxserializationinterface)
6. [Lifecycle message catalog](#lifecycle-message-catalog)
7. [Cross-plugin symbol table](#cross-plugin-symbol-table)
8. [Pre-flight conflict matrix](#pre-flight-conflict-matrix)
9. [Address Library](#address-library)
10. [Logging](#logging)
11. [Worked examples](#worked-examples)
12. [Deferred to later](#deferred-to-later)

---

## Plugin model overview

A **plugin** is a folder inside
`<game>/Bin/Win64MasterMasterSteamPGO/plugins/`. The folder may
contain either or both of:

- **A C++ DLL** named anything (`<author>-<modname>.dll` by
  convention). Discovered by `LoadLibraryEx` scan; metadata read
  from an exported `kcdxPluginVersionData` data block. Full plugin
  surface — every interface, every lifecycle message, full
  serialization.
- **A `kcdx.toml` file.** Declarative — schema entries for
  `[[patch]]`, `[[hook]]`, `[[mid_hook]]`, `[[trampoline]]`,
  `[[command]]`, `[[event]]`. Limited compared to a DLL (Lua
  callbacks have constrained marshaling, no plugin handle issued, no
  serialization recording), but covers ~80% of common mod use cases
  without a C++ toolchain.

A plugin folder is **also** allowed to contain a `mempatch.toml`
file — that one is consumed by the sibling [kcd2-mempatch][mp]
engine, not by kcdx. mempatch and kcdx coexist by filename
discipline (mempatch loads only `mempatch.toml`; kcdx loads only
`kcdx.toml` and `*.dll`). Never make the two engines argue.

If you only need byte rewrites, **mempatch alone is the better tool**
— smaller, faster to load, ships independently. Use kcdx when you
need anything else, or when you want one mental model instead of two.

---

## The C++ DLL path

### Folder layout

```
<game>/Bin/Win64MasterMasterSteamPGO/plugins/
├── kcdx.asi                              ← the engine itself
├── kcdx.log                              ← runtime log
├── <plugin-name>/
│   ├── <plugin-name>.dll                 ← your DLL (any name; scanned by extension)
│   └── kcdx.toml                         ← optional, declarative entries
└── <other-plugin>/
    └── kcdx.toml                         ← TOML-only plugin (no DLL)
```

Plugin DLLs are loaded by kcdx via `LoadLibraryEx`. Each DLL gets
exactly one `PluginHandle` (opaque `uint32_t`) assigned at load
time, used to identify the plugin in every API.

### `kcdxPluginVersionData` exported data block

Every C++ plugin **must** export an instance of this struct as a
data symbol named `kcdxPluginVersionData`. Modeled on SKSE's
`SKSEPluginVersionData` (modern, AE-1.6+ era).

```cpp
struct kcdxPluginVersionData {
    uint32_t dataVersion;              // Must be kCurrentDataVersion = 1
    uint32_t pluginVersion;            // Plugin's own integer version
    char     name[256];                // STABLE PLUGIN ID. Unique across loaded plugins.
                                       // Used as messaging sender identity, serialization
                                       // record key, dependency lookup. Not the filename.
    char     author[256];
    char     supportEmail[252];

    uint32_t versionIndependenceEx;    // Reserved for forward compat. Set 0.
    uint32_t versionIndependence;      // Bitfield. See VersionIndependence flags below.

    uint32_t compatibleGameVersions[16];  // KCD2 build numbers this plugin tested against.
                                          // Encoding: (major<<24) | (minor<<16) | (build_lo16).
                                          // 1.5.1164953 → kcdxMakeGameVersion(1,5,1164953) =
                                          //   (1<<24) | (5<<16) | (1164953 & 0xFFFF) = 0x010579D9.
                                          // Zero-terminated. Empty array means "any version"
                                          // — only valid if versionIndependence has
                                          // AddressLibrary set.

    uint32_t kcdxVersionRequired;      // Minimum kcdx version (e.g. 0x00010000 = 0.1.0).

    uint32_t reserved[8];              // Pad for future fields. Set zero.

    // Optional: inline declarative patches. Pointer to a TOML string the loader
    // parses BEFORE calling kcdxPlugin_Load. Letting C++ plugins skip kcdx.toml
    // when they already ship a DLL.
    const char* inlinePatchesToml;     // Null-terminated, nullable.

    // Optional: dependencies. Pointer to a zero-terminated array of
    // { name, min_version, flags } triplets. Loader topo-sorts before
    // issuing Plugin_Load calls. See kcdxPluginDependency below.
    const kcdxPluginDependency* dependencies;  // Nullable. Array terminated by {nullptr, 0, 0}.
};

struct kcdxPluginDependency {
    const char* name;          // Other plugin's stable ID
    uint32_t    minVersion;    // Their pluginVersion must be >= this
    uint32_t    flags;         // Bit 0: kcdxDependencyFlag_Optional
};

enum VersionIndependence : uint32_t {
    kVersionIndependent_AddressLibrary = 1 << 0,  // Skip compatibleGameVersions check
                                                  // because plugin uses kcdx::ResolveAddress
    kVersionIndependent_StructsPostAE   = 1 << 1, // Reserved
};

constexpr uint32_t kCurrentDataVersion = 1;
```

Declaring is a single `__declspec(dllexport)`:

```cpp
extern "C" __declspec(dllexport)
kcdxPluginVersionData kcdxPluginVersionData = {
    .dataVersion = kcdxPluginVersionData::kCurrentDataVersion,
    .pluginVersion = 0x00010000,  // 0.1.0
    .name = "violetanvil.example-plugin",
    .author = "violetanvil",
    .supportEmail = "noreply@example.com",
    .versionIndependence = kVersionIndependent_AddressLibrary,
    .compatibleGameVersions = { /* none required since AddressLibrary set */ },
    .kcdxVersionRequired = 0x00010000,
    .inlinePatchesToml = nullptr,
    .dependencies = nullptr,
};
```

### Entry points: `kcdxPlugin_Preload` and `kcdxPlugin_Load`

Two optional exported functions. Loader checks for each in turn.

```cpp
// Optional. Called BEFORE any plugin's Load (the "preload wave").
// Use this only if you need to install something other plugins'
// Load might depend on (e.g., registering a symbol you export).
extern "C" __declspec(dllexport)
bool kcdxPlugin_Preload(const kcdxInterface* api);

// Required if not all your work is in inlinePatchesToml.
// Called after every plugin's Preload returned. Your plugin is now
// safe to register listeners, install hooks, call into other
// plugins by name.
extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api);
```

Both return `true` on success. A `false` return logs an error but
does not unload the DLL (Windows reclaims at process exit; kcdx,
like SKSE, does not `FreeLibrary`).

### Plugin lifecycle and load order

1. **Discovery.** kcdx scans `plugins/*.dll`, reads each one's
   `kcdxPluginVersionData` block via `LoadLibraryEx(...,
   LOAD_LIBRARY_AS_IMAGE_RESOURCE)` (no real load yet).
2. **Validation.** Compatibility check: each plugin's
   `compatibleGameVersions` must include the running KCD2 build, OR
   `versionIndependence` must have `kVersionIndependent_AddressLibrary`.
   Plugins failing compatibility are logged and skipped.
3. **Topo-sort.** Dependencies parsed. Cycles detected and reported.
   Missing required deps cause plugin to be skipped with a clear log
   line. Optional missing deps just log info.
4. **Real load.** Surviving plugins are `LoadLibrary`'d for real.
   `PluginHandle` assigned.
5. **Inline patches applied.** For each plugin with
   `inlinePatchesToml != nullptr`, kcdx parses it and applies any
   `[[patch]]` entries via the same pre-flight pipeline used for
   external `kcdx.toml` files.
6. **Preload wave.** `kcdxPlugin_Preload` called on every plugin
   that exports it, in topo-sort order.
7. **Load wave.** `kcdxPlugin_Load` called on every plugin in
   topo-sort order.
8. **`kMessage_PostLoad` fired.** Plugin B sees plugin A is loaded.
9. **`kMessage_PostPostLoad` fired.** Plugin B can confirm plugin A
   *finished its kMessage_PostLoad handler* — the wave is settled.
10. **Engine main loop.** The hooked `update` tick fires
    `kMessage_InputLoaded` (before the first menu frame),
    `kMessage_NewGame` / `kMessage_PreLoadGame` / etc. as the game
    state transitions.

There is **no teardown hook**. Plugins leak until process exit.
This is intentional and matches SKSE.

---

## The declarative TOML path

### Coexistence with the C++ path

A plugin folder may contain:

- A `kcdx.toml` only — purely declarative plugin. No `PluginHandle`,
  no Serialization, no Messaging participation as a sender (but the
  declarative entries' Lua callbacks can use `kcdx.events` to
  subscribe to messages).
- A DLL only — full C++ plugin.
- Both — DLL plus a `kcdx.toml`. Entries from the TOML are applied
  alongside the DLL's `kcdxPlugin_Load`. Same `PluginHandle` covers
  both. The DLL's `inlinePatchesToml` is also concatenated (DLL
  ships the TOML inline).

The `kcdx.toml` is **not** the same thing as the DLL's
`inlinePatchesToml`. The TOML on disk is the declarative
authoring surface; `inlinePatchesToml` is for DLL authors who want
to keep their byte patches in source rather than as a sidecar file.

### Schema overview

Every entry type follows the same shape:

- **Singular table name** (`[[patch]]`, `[[hook]]`, etc. — never
  `[[patches]]`). Idiomatic Rust-ecosystem TOML; matches mempatch.
- **`snake_case` keys** throughout.
- **Optional top-level `[kcdx]` table** for engine-wide config.

Entry types: `[[patch]]`, `[[hook]]`, `[[mid_hook]]`,
`[[trampoline]]`, `[[command]]`, `[[event]]`. Each documented in
its own section below.

---

## TOML schema reference

### `[kcdx]` top-level options

```toml
[kcdx]
dry_run = false   # bool, default false. If any loaded toml sets true, NO
                  # writes happen for the entire session. Locator resolution
                  # still runs and is logged.
```

### `[[patch]]` — same-length byte rewrite

Identical schema to mempatch's `[[patch]]`. Verbatim. Use this when
all you need is a byte rewrite — no DLL, no Lua, no compiler. Same
pre-flight conflict detection, same locator tiers, same idempotent
re-runs.

```toml
[[patch]]
# --- required ---
name         = "string"            # log tag; unique across all loaded patches
pattern      = "48 8B 01 ?? ..."   # AOB; hex bytes space-separated, ?? = wildcard
offset       = 0                   # int, offset from pattern start to first byte to write
original     = "44 8A F0"          # expected current bytes at (match + offset)
replacement  = "45 31 F6"          # bytes to write; must be same length as original

# --- optional ---
priority     = 100                 # int, lower applies first
module       = "WHGame.dll"        # PE module name
idempotent   = true                # bool, default true
description  = "..."               # free-form notes

# --- Tier 2: context ---
context      = "longer hex bytes"  # must contain `pattern`; must produce 1 match

# --- Tier 3: anchor (mutually exclusive) ---
anchor_string             = "..."  # unique .rdata literal; LEA xref must be unique
anchor_function_by_export = "..."  # exported function name
max_anchor_distance       = 4096   # int, default 4096
```

The full safety guarantees from mempatch's
[`writing-safe-patches.md`](../../kcd2-mempatch/docs/writing-safe-patches.md)
apply. The verification milestone for Phase 1 is that a `[[patch]]`
in `kcdx.toml` produces **identical** apply-log output to the same
entry in `mempatch.toml`.

### `[[hook]]` — function-entry detour

Hooks a function at its first instruction. The original prologue is
relocated into a trampoline; the original site gets a 5-byte rel32
`jmp` (or 14-byte abs jmp if out of range). MinHook handles the
prologue analysis.

```toml
[[hook]]
# --- identification ---
name        = "string"             # log tag; unique
description = "..."
priority    = 100
module      = "WHGame.dll"

# --- target locator: one of these blocks ---
# (a) AOB tiers (same as [[patch]])
pattern       = "48 8B 01 FF ..."
context       = "..."
anchor_string = "..."
# (b) cross-plugin symbol
target_symbol = "other_plugin.exported_name"
# (c) Address Library ID
address_id    = 12345

# --- detour body: exactly one of bytes / lua_callback ---
bytes        = "48 83 EC 28 ... C3"   # raw x86-64 machine code
# OR:
lua_callback = "MyMod.OnInventoryOpen"
signature    = "void(rcx: ptr, rdx: i32)"

# --- optional ---
call_original = "before"    # "before" (default) | "skip"
                            # "before": Lua runs first; if returns nil/false, original
                            #           is called. If returns a value matching signature's
                            #           return type, original is skipped, that value used.
                            # "skip":   original never runs; Lua's return is the result.
export       = "myplugin.my_hook"  # publish this hook's trampoline as a symbol
```

**Constraint:** Exactly one locator block, exactly one detour body.
Mixing `bytes` and `lua_callback` is a validation error.

> **Threading:** if `lua_callback` is set, the callback runs on
> whatever thread the hooked function ran on. KCD2's Lua VM is
> single-threaded and `lua_pcall` racing against the main thread
> from a worker is undefined (likely crash). Use `lua_callback`
> only on functions you know run on the main game thread. See
> [Threading constraint](#threading-constraint-hook-only-main-thread-functions)
> in `kcdxScriptingInterface` for the safe / unsafe target list.

### `[[mid_hook]]` — mid-function hook

Hooks any instruction (not just function entry). Captures named
registers and stack-expressions, passes them to a Lua callback as a
table, optionally allows the callback to mutate them on return.
Strictly more powerful than `[[hook]]` for cases where the right
patch site is a specific instruction inside a function rather than
the function as a whole.

```toml
[[mid_hook]]
name = "outfit_gate_at_combat_check"
description = "Replace the IsInCombat result with a Lua-driven gate."

# Locator (same options as [[hook]])
pattern = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
context = "..."
offset  = 13                              # offset into pattern to the hooked instruction

# Captures: named register or stack-expression values made available
# to the Lua callback. Each entry becomes a key in the table passed
# to the callback.
captures = [
    "rax",                                # general-purpose register
    "rcx",
    "[rcx+0xb60]",                        # memory expression (8 bytes by default)
    "[rsp+0x10]:i32",                     # explicit width
]

# Bytes the original instruction takes. After capture, the callback
# returns; kcdx restores registers and resumes at (pattern_match +
# offset + stack_restore_offset). Set to the length of the original
# instruction or the number of bytes you want to skip.
stack_restore_offset = 3

lua_callback = "MyMod.GateOutfitSwap"
# Callback signature: function(captures) -> table or nil
# - captures: table with keys matching the `captures` array
# - returns nil: registers unchanged
# - returns table: any key matching a capture name overwrites that register
#                  before resume. Allows in-place mutation of game state.
```

> **Threading:** the `lua_callback` runs on whatever thread reached
> the hooked instruction. Hook only main-thread instructions. See
> [Threading constraint](#threading-constraint-hook-only-main-thread-functions)
> for the full list.

> **Known v0.1 limitation:** the current MinHook-based mid-hook
> primitive re-executes the captured instruction after the callback
> returns. The capture / read / log workflow works; "skip the original
> instruction by writing a different value to its destination register"
> does NOT, because MinHook reinstates the original bytes. Tests
> intentionally fail this case (CAP-04 in the regression suite).
> A v0.2 primitive (`call_original = false` flag, or a true
> instruction-replacement detour) is needed for the override case.

### `[[trampoline]]` — named executable region

Allocates a chunk of executable memory, fills it with the supplied
bytes, optionally publishes its address as a symbol. No automatic
call-site patching — a separate `[[patch]]`, `[[hook]]`, or
external mechanism installs a reference to it.

This is the primitive that makes cross-plugin code sharing possible.
The motivating example: mod A allocates a trampoline with custom
logic and exports it; mod B `target_symbol`s into it and patches one
byte inside.

```toml
[[trampoline]]
name        = "outfit_gate_logic"
description = "Custom outfit-swap gate logic, exposed for other mods to extend."

bytes  = """
48 83 EC 28
48 8B 0D ?? ?? ?? ??      # mov rcx, [my_cvar_ptr]
E8 ?? ?? ?? ??            # call cvar_getter
3C 00                     # cmp al, 0
0F 95 C0                  # setne al
48 83 C4 28
C3
"""

size   = 256      # int, default = bytes.length. Rounded up to page boundary.
                  # Extra space NOP-filled; other plugins can patch into it.

export = "violetanvil.outfit_gate_logic"   # required for cross-plugin use

priority = 50     # int, lower allocates first
```

**Pool choice:** by default, kcdx allocates from the
`AllocateFromBranchPool` (within ±2 GB of `WHGame.dll`'s `.text` so
a 5-byte rel32 `E9` jmp can reach). Specify
`pool = "local"` to use `AllocateFromLocalPool` instead (any address,
larger budget, 14-byte abs jmp required).

### `[[scan]]` — diagnostic locator resolve (no apply)

Pure-diagnostic entry type. Resolves a locator via the same pipeline
`[[patch]]` / `[[hook]]` use, but performs NO write, NO hook install,
and NO conflict-engine participation. Logs the match count, resolved
address(es), and surrounding raw bytes.

```toml
[[scan]]
name           = "find_outfit_swap"
module         = "WHGame.dll"                     # default
pattern        = "48 81 C1 60 0B 00 00 ..."
offset         = 13                                # default 0
context        = "..."                             # optional Tier-2
anchor_string  = "..."                             # optional Tier-3 anchor
max_anchor_distance = 4096                         # optional
```

**Output to kcdx.log on resolve:**

```
[scan 'find_outfit_swap'] pattern matches: 1
[scan 'find_outfit_swap'] context matches: 1
[scan 'find_outfit_swap'] match 1: pattern at 0x00007FFCF9051738 (WHGame.dll+0x1971738);
                                   with offset +13 -> apply addr 0x00007FFCF9051745
[scan 'find_outfit_swap']   bytes -16: 48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B
[scan 'find_outfit_swap']   bytes  +0: 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0 44 89 ...
```

**Use case:** new modders writing their first AOB have no way to ask
"did this pattern resolve, and to what?" without committing a
destructive write through `[[patch]]` or `[[hook]]`. `[[scan]]` is
the discoverable answer — drop in a TOML, launch, read kcdx.log.

If `pattern` matches multiple times, all match addresses are logged
so the author can see WHY uniqueness failed and pick a more
distinctive prefix or use Tier-2 `context` to disambiguate.

Runs at first-update-tick, same moment patches apply. No effect on
the game; runs even when other entries in the same file have errors.

### `[[command]]` — console command

Registers a string command callable from KCD2's `-console` window.
Backed by the game's `pConsole.RegisterCommand` (see
[Open questions](#open-questions-for-implementation)).

```toml
[[command]]
name        = "kcdx_dump_state"
description = "Dump kcdx-loaded plugin list and conflicts to a JSON file."
lua_callback = "MyMod.OnDumpCommand"
# Callback signature: function(args: string) -> nil
# args: the raw command tail (everything after the command name)
```

### `[[event]]` — lifecycle event subscription

Subscribes a Lua callback to one of the engine's lifecycle messages.

```toml
[[event]]
name         = "kMessage_PostLoadGame"     # one of the catalog
lua_callback = "MyMod.OnSaveLoaded"
# Callback signature: function(data) -> nil
# `data` shape depends on the message; see Lifecycle message catalog below.
```

---

## Plugin interfaces (C++)

The interface header lives at `kcdx/include/kcdx/Interfaces.h`.
Plugins `#include` it and link against… nothing. The
`kcdxInterface*` pointer is passed in via `kcdxPlugin_Load`; every
other interface is fetched via `QueryInterface`.

### `kcdxInterface`

Root accessor. The only pointer your plugin receives at load.

```cpp
struct kcdxInterface {
    uint32_t kcdxVersion;            // Engine version (e.g. 0x00010000 = 0.1.0)
    uint32_t runtimeGameVersion;     // Live KCD2 build the engine is hooked into

    // Fetch a typed interface. Returns null if the interface ID is
    // unknown or the version requested is newer than the engine
    // supports.
    void* (*QueryInterface)(uint32_t interfaceID, uint32_t version);

    // Look up another plugin by stable name. Returns null if not loaded.
    const kcdxPluginVersionData* (*GetPluginInfo)(const char* name);

    // List all loaded plugins. Fills the supplied array up to `cap`
    // entries; returns the actual count. Pass `out=nullptr, cap=0` to
    // get the count without filling.
    uint32_t (*EnumeratePlugins)(PluginHandle* out, uint32_t cap);

    // Get a plugin's handle by stable name. Returns kInvalidPluginHandle
    // (0xFFFFFFFF) on miss.
    PluginHandle (*GetPluginHandle)(const char* name);

    // Look up a known address by Address Library ID.
    // Returns 0 (== nullptr) if the ID is unknown for the running game version.
    uintptr_t (*ResolveAddress)(uint64_t id);
};

enum kcdxInterfaceID : uint32_t {
    kInterface_Messaging      = 1,
    kInterface_Trampoline     = 2,
    kInterface_Task           = 3,
    kInterface_Scripting      = 4,
    kInterface_Serialization  = 5,
};
```

### `kcdxMessagingInterface`

Pub/sub message bus. Keyed on plugin stable name. Used both by the
engine (firing lifecycle messages) and by plugins (broadcasting
custom messages to peers).

```cpp
struct kcdxMessagingInterface {
    static constexpr uint32_t kVersion = 1;

    struct Message {
        const char* sender;        // null for engine-originated messages
        uint32_t    messageType;   // see kMessage_* catalog below
        const void* data;          // null or message-specific payload
        uint32_t    dataLen;       // payload size in bytes
    };

    using EventCallback = void (*)(Message* msg);

    // Subscribe to messages from a specific sender (by name), or
    // from the engine (sender = nullptr). Returns true on success.
    // Multiple listeners for the same sender are allowed.
    bool (*RegisterListener)(PluginHandle listener,
                             const char* sender,
                             EventCallback callback);

    // Send a message to one specific receiver by name, or
    // broadcast to all listeners (receiver = nullptr).
    bool (*Dispatch)(PluginHandle sender,
                     uint32_t messageType,
                     const void* data,
                     uint32_t dataLen,
                     const char* receiver);
};
```

### `kcdxTrampolineInterface`

Allocate executable memory the plugin owns.

```cpp
struct kcdxTrampolineInterface {
    static constexpr uint32_t kVersion = 1;

    // Branch pool: within ±2 GB of WHGame.dll's .text section so a
    // 5-byte rel32 E9 jmp can reach. Limited budget per game build.
    // Returns null if exhausted.
    void* (*AllocateFromBranchPool)(PluginHandle owner, size_t size);

    // Local pool: any address (VirtualAlloc). No proximity guarantee.
    // Use when you don't need rel32 reach. Effectively unlimited budget.
    void* (*AllocateFromLocalPool)(PluginHandle owner, size_t size);
};
```

### `kcdxTaskInterface`

Queue a task to run on the game's main thread (the `update` tick).
Essential because most CryEngine state is single-threaded.

```cpp
struct kcdxTaskInterface {
    static constexpr uint32_t kVersion = 1;

    // The task struct your plugin defines and submits.
    struct ITask {
        virtual ~ITask() = default;
        virtual void Run() = 0;      // Called on main thread next tick
        virtual void Dispose() = 0;  // Called after Run() (or on cancel) — clean up
    };

    // Schedule. Task is deleted via Dispose() after Run() completes.
    // Safe to call from any thread.
    void (*AddTask)(ITask* task);
};
```

### `kcdxScriptingInterface`

Register C++ functions callable from KCD2's Lua VM. Analogous to
SKSE's `SKSEPapyrusInterface`, but for KCD2's Lua 5.1 instead of
Skyrim's Papyrus.

```cpp
struct kcdxScriptingInterface {
    static constexpr uint32_t kVersion = 1;

    // Signature: subset of "type(type, type, ...)" syntax.
    // Supported types: void, i32, i64, f32, f64, ptr, bool, string.
    // Example: "i32(string, i32)" — function returning i32, taking string and i32.
    using CFunction = void (*)(void* args, void* ret);

    // Register a C function callable from Lua as `kcdx.<name>(...)`.
    // The Lua side calls `kcdx.MyFunc(arg1, arg2)`; kcdx marshals
    // args into typed slots per `signature` and dispatches to `fn`.
    bool (*RegisterFunction)(PluginHandle owner,
                             const char* name,
                             const char* signature,
                             CFunction fn);
};
```

The implementation uses asmjit-generated thunks (vendored from
ReturnOfModding, Phase 5+). Plugin authors don't see asmjit
directly — just the typed `signature` string.

#### Implementation constraint: no sol2 on the live `lua_State`

**sol2 must NOT touch KCD2's live `lua_State` at all.** Pinned
via bisect in Phase 5c.7a (2026-05-18): registering a sol2
usertype (`new_usertype<T>(...)`) — even with zero call sites
and no instances ever constructed — hard-crashes the game
during save-game deserialization. No Aftermath dump is
produced; `kcd.log` ends mid-load. Title screen and main-menu
interaction work fine; only save-load fails. The likely cause
is sol2's metatable scaffolding in `LUA_REGISTRYINDEX`
interfering with KCD2's save-load walk of Lua state.

Decision (2026-05-18, "Strict" path): the rule covers every
kcdx code path that touches the live `lua_State`, not just
`new_usertype`. `sol::make_object<UserType>` is also forbidden
because it triggers on-demand metatable creation on first push.
sol2 stays vendored at `vendor/sol2/` (for reference; the
linker dead-strips unused headers) but no kcdx translation
unit may `#include <sol/sol.hpp>`. `git grep '<sol/sol.hpp>' src/`
should always return zero lines.

What this means concretely:

- The `kcdx.*` Lua global and all sub-tables (`kcdx.memory.*`,
  `kcdx.scripting.*`, etc.) are built with raw Lua C API
  (`lua_newtable`, `lua_pushcfunction`, `lua_setfield`,
  `luaL_newmetatable`). The existing Phase 1 globals
  (`KCDX.ScanAndWrite`, `KCDX.ReadBytes`, `KCDX.GetWHGameBase`
  in `src/lua_bind.cpp`) demonstrate the pattern.
- `kcdx::scripting`'s per-target callback storage uses Lua
  registry refs (`luaL_ref` / `lua_rawgeti`) instead of
  `std::vector<sol::protected_function>`.
- `kcdx::scripting`'s dispatchers (`dynamic_hook_pre/post/mid`)
  push args onto the Lua stack with the raw C API and call
  `lua_pcall` directly.
- `kcdx::lua_memory::to_lua` / `to_lua_return` push the
  marshaled value onto the Lua stack and return void (no
  `sol::object`).
- All marshaled types (`pointer`, `value_wrapper_t`) are
  pushed as raw userdata via `lua_newuserdata` +
  `luaL_setmetatable`.
- The `kcdxScriptingInterface::RegisterFunction`
  thunk-generation machinery (asmjit) is unaffected — it
  generates raw C functions that get registered via
  `lua_pushcfunction`, same as a manually written binding.

#### Threading constraint: hook only main-thread functions

KCD2's Lua VM (CryEngine 5.2.3 bundled Lua 5.1) is single-
threaded. `lua_lock` / `lua_unlock` are no-ops in the shipped
build. `kcdx::scripting`'s pre/post/mid dispatchers invoke
`lua_pcall` directly against the captured `g_lua_state` from
inside MinHook detours — whatever thread the hooked target
ran on is the thread that ends up entering the Lua VM.

v0.1 does NOT add a runtime thread-ID guard. Plugin authors
using `kcdx.memory.dynamic_hook` (Lua-side) or
`[[hook]] lua_callback` (TOML-side, Phase 5f) are responsible
for ensuring the targeted function runs on the main game
thread. Safe targets:

  - Anything kcdx already hooks (`lua_pcall`, `update`) — main
    thread by construction, since that's where kcdx captured
    the `lua_State` pointer.
  - CryEngine gameplay-loop functions invoked from the main
    `update`. The vast majority of `WHGame.dll` code.
  - Lua-bound C functions (anything resolvable via
    `kcdx.lua.cfunction_address`) — Lua's single-threaded
    nature implies these only run from the main VM-owning
    thread.

Unsafe targets:

  - Audio mixer callbacks
  - Physics worker thread routines
  - IO/streaming worker routines

If a real plugin needs to hook an unsafe target, v0.2 adds a
runtime `GetCurrentThreadId()` guard inside the dispatcher
that skips invocation when called off-thread (logs a warn).
Until that exists, the failure mode is undefined — Lua state
race, likely crash. Verified-2026-05-18 (Phase 5d skip
decision): the rule is documented here and in
`CLAUDE.md` hard rule #16; no runtime enforcement.

### `kcdxSerializationInterface`

Persist data tied to the current save game. Storage location:
`<save_dir>/<save_name>.kcdx` co-save file (separate from KCD2's
own `.kcd2save`). Modeled on SKSE's serialization interface.

```cpp
struct kcdxSerializationInterface {
    static constexpr uint32_t kVersion = 1;

    using SaveCallback   = void (*)(PluginHandle plugin);
    using LoadCallback   = void (*)(PluginHandle plugin);
    using RevertCallback = void (*)(PluginHandle plugin);  // Called on new game / reload

    // Set a unique ID for this plugin's records. Required before
    // any OpenRecord call.
    void (*SetUniqueID)(PluginHandle plugin, uint32_t uid);

    // Register your callbacks. Each fires at the relevant lifecycle moment.
    void (*SetSaveCallback)  (PluginHandle plugin, SaveCallback);
    void (*SetLoadCallback)  (PluginHandle plugin, LoadCallback);
    void (*SetRevertCallback)(PluginHandle plugin, RevertCallback);

    // Write side — called from your SaveCallback.
    bool (*OpenRecord)     (uint32_t tag, uint32_t version);
    bool (*WriteRecordData)(const void* buf, uint32_t len);

    // Read side — called from your LoadCallback.
    bool (*GetNextRecordInfo)(uint32_t* outTag, uint32_t* outVersion, uint32_t* outLen);
    bool (*ReadRecordData)   (void* buf, uint32_t len);
};
```

---

## Lifecycle message catalog

All sent by the engine itself (the `Message::sender` field is null
on these). Plugins subscribe via
`kcdxMessagingInterface::RegisterListener(handle, nullptr,
callback)`.

Where a KCD2-equivalent of an SKSE message exists, kcdx uses
**the same enum name**. Where KCD2 has no analogue (e.g.
`kDataLoaded` in SKSE refers to ESM/ESP load — KCD2 has no
equivalent), the name is simply not present in kcdx.

| Message | Fires when | `data` payload |
|---|---|---|
| `kMessage_PostLoad` | After every plugin's `kcdxPlugin_Load` returned | null |
| `kMessage_PostPostLoad` | After every `kMessage_PostLoad` handler returned. The "plugin wave is settled" moment. | null |
| `kMessage_InputLoaded` | After KCD2's input subsystem init, before main menu shows | null |
| `kMessage_NewGame` | New game started, before first cell loads | null |
| `kMessage_PreLoadGame` | Save selected, before engine reads it | `const char*` (save name) |
| `kMessage_PostLoadGame` | Save finished loading, world is interactive | `const char*` (save name) |
| `kMessage_SaveGame` | Game being saved (either manual or quicksave) | `const char*` (save name) |
| `kMessage_DeleteGame` | A save + its `.kcdx` co-save being deleted | `const char*` (save name) |

Phase 6 of the implementation budget covers the Ghidra session to
identify the save-game and load-game entry points. Until that
lands, `kMessage_PreLoadGame` / `kPostLoadGame` / `kSaveGame` /
`kDeleteGame` may not fire — to be confirmed during Phase 6.

Custom plugin-to-plugin message types use ID values **≥ 0x10000**.
The engine reserves the lower range. Two custom messages from
different senders are distinguished by `Message::sender`.

---

## Cross-plugin symbol table

A globally-unique string-keyed map from name → runtime address.

**Population:** an entry that includes `export = "<name>"` registers
that name in the table after resolution completes. Sources:

- `[[trampoline]]` with `export = "..."` → registers
  `(name, allocated_address)`
- `[[hook]]` with `export = "..."` → registers
  `(name, trampoline_address)` where the hook's trampoline is
  callable as the "original behavior."

**Consumption:** any entry can replace its locator with
`target_symbol = "<name>"`. The engine resolves the symbol in a
two-pass pre-flight:

1. **Pass 1:** entries with concrete locators (`pattern`, `address_id`)
   resolve against `WHGame.dll`. Symbol exports register.
2. **Pass 2:** entries using `target_symbol` look up the registered
   address. Their `patchAddr` / `targetAddr` is set.

**Diagnostics:**

- **Duplicate export.** Two plugins exporting the same symbol →
  load-time error, both entries abort with a clear log line naming
  the other plugin.
- **Missing import.** Plugin imports `target_symbol = "X"` but no
  plugin exports `X` → plugin's entry aborts with a log line:
  `Plugin 'A' requires symbol 'X' but no plugin exports it.`
- **Cycle.** Plugin A's export depends on plugin B's export, B's
  depends on A's → detected by failing-to-converge after N passes;
  log lines name both plugins.

Convention (not enforced): `pluginname.symbolname`. Underscores OK
inside the suffix. The engine doesn't parse the dot — it's just a
naming discipline that scales.

---

## Pre-flight conflict matrix

mempatch's three-category model, extended to cover hooks and
trampolines. Each entry has a *write footprint* (bytes the engine
will modify when it applies the entry) and a *read footprint*
(bytes the engine reads to verify before applying).

For every pair (earlier-priority plugin A, later-priority plugin B)
where A.write overlaps B.read or B.write, one of these categories
applies:

| Overlap | Log | Outcome |
|---|---|---|
| **Incidental** — A writes inside B's pattern/context read range, but A's write site is NOT in B's verify target. | silent | Both apply. B's locator was resolved against the pristine DLL; A's writes don't move B's target. |
| **Write-on-original** — A's write overlaps B's `original` verify bytes. | `[WARN]` | B's verify fails at apply time. B aborts cleanly with a log line naming A. |
| **Write-on-write, full overlap** — A and B both write the same byte range. | `[INFO]` | Both apply in priority order; B's bytes win. |
| **Write-on-write, partial overlap** — A and B write overlapping but non-identical ranges. | `[WARN]` | Both apply; result is a mix of bytes which may be an invalid instruction. |
| **Hook-on-patch** — A's `[[patch]]` write overlaps B's `[[hook]]` prologue-relocation range. | `[INFO]` | Both apply. MinHook's relocated prologue captures the patched bytes (correct). |
| **Patch-on-hook** — B's `[[patch]]` writes inside A's `[[hook]]` prologue (5 jmp bytes) | `[WARN]` | Same as write-on-original: B's verify will fail. |
| **Hook-on-hook** — A and B both `[[hook]]` the same target function entry. | `[WARN]` | First-wins. A's hook installs; B's aborts with `MH_ERROR_ALREADY_CREATED` translated to plain English. **Chained hooks are v0.2+.** |
| **MidHook collision** — two `[[mid_hook]]` at the exact same instruction. | `[WARN]` | First-wins; B aborts. |
| **Trampoline-on-trampoline `target_symbol` write** — two patches with the same `target_symbol`. | `[INFO]` / `[WARN]` per write-on-write rules. | Standard write-on-write semantics, just inside plugin-allocated memory rather than `WHGame.dll`. |

Log lines are **plain English**, name both plugins involved, and
explain to a player what to do (remove one, reorder priority, etc.).
The mempatch precedent for this wording is the canonical reference.

---

## Address Library

In-box address database. CSV source at `address-library/database.csv`,
compiled to a binary lookup table at build time. The database lookup
API on `kcdxInterface` is:

```cpp
uintptr_t kcdxInterface::ResolveAddress(uint64_t id);
```

Returns 0 if the ID is unknown for the running game version (not in
the database, or the database has it as `removed` for this version).

CSV schema: `id, game_version, rva, status, name`

- `id` — stable integer, never recycled
- `game_version` — KCD2 build number this row applies to
  (e.g. `0x010505BC` for 1.5.1164953). Multiple rows per ID for
  different game versions.
- `rva` — address relative to `WHGame.dll` base
- `status` — `ok` (valid), `removed` (function gone in this version),
  `unverified` (community contribution awaiting confirmation)
- `name` — human-readable Ghidra-style name, for log diagnostics

Authors add IDs by editing the CSV and submitting a PR.

---

## Logging

- Log file: `<game>/Bin/Win64MasterMasterSteamPGO/plugins/kcdx.log`
- Re-truncated on each game launch.
- If KCD2 was launched with `-console`, log lines are also written
  to the spawned console window (same model as mempatch).
- Per-plugin log files: not in v0.1. Plugins write to the shared
  `kcdx.log` via the engine's logging facility (exposed in
  `kcdxInterface` as a `Log(plugin_handle, level, msg)` function —
  TBD signature). Authors who want their own log files manage them
  themselves; plugins are unsandboxed.
- Lua side: `kcdx.log_info / log_warn / log_error / log_debug`
  functions registered globally.

---

## Worked examples

### Example 1 — "I just want to flip three bytes" (the simple case)

Same outfit-swap-in-combat patch the mempatch tutorial uses, but
expressed as kcdx:

```toml
# plugins/outfit-swap-in-combat/kcdx.toml
[[patch]]
name        = "outfit_swap_in_combat"
description = "Allows switching outfits during combat."
pattern     = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
offset      = 13
original    = "44 8A F0"
replacement = "45 31 F6"
context     = "48 8B 88 90 00 00 00 48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
```

Identical to the mempatch version, just with the filename changed.
Same safety, same log lines, same outcome.

### Example 2 — `[[mid_hook]]` gating with Lua

```toml
# plugins/outfit-gate-cvar/kcdx.toml
[[mid_hook]]
name = "outfit_gate"
pattern = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
offset  = 13                       # the 44 8A F0 (mov r14b, al) instruction
captures = ["rax"]
stack_restore_offset = 3
lua_callback = "OutfitGate.Decide"
```

```lua
-- plugins/outfit-gate-cvar/main.lua  (loaded by KCDX scripting interface)
OutfitGate = {}
function OutfitGate.Decide(captures)
    -- captures.rax holds the IsInCombat() result (1 = in combat, 0 = not)
    -- Read a CVar to decide whether to override.
    if kcdx.get_cvar_bool("g_outfit_swap_allow_in_combat") then
        return { rax = 0 }   -- force "not in combat" → action allowed
    end
    return nil               -- pass through the original behavior
end
```

The cvar is checked dynamically every time — players toggle it
in-console without restarting.

### Example 3 — cross-plugin trampoline + patch

The motivating example. Mod A allocates custom logic and exports
it; mod B patches a byte inside.

```toml
# plugins/outfit-gate-base/kcdx.toml
[[trampoline]]
name   = "outfit_gate_logic"
bytes  = "48 83 EC 28 ... 3C 00 0F 95 C0 ..."  # the gate routine
export = "violetanvil.outfit_gate_logic"
size   = 256

[[hook]]
name          = "outfit_gate_install"
pattern       = "48 81 C1 60 0B 00 00 48 8B 01 FF 50 08 44 8A F0"
context       = "..."
target_symbol = "violetanvil.outfit_gate_logic"  # hook jumps to our trampoline
```

```toml
# plugins/outfit-gate-strict/kcdx.toml
# Mod B doesn't ship its own gate logic — it tweaks mod A's.
[[patch]]
name          = "outfit_gate_strict_mode"
target_symbol = "violetanvil.outfit_gate_logic"
offset        = 18                  # the `cmp al, 0` constant inside the trampoline
original      = "00"
replacement   = "01"                # now `cmp al, 1` — invert the gate
```

If mod A is missing, mod B's `target_symbol` fails to resolve, mod
B aborts cleanly with a log line naming the missing symbol. If
both load, kcdx applies them in priority order: mod A's trampoline
allocates and gets patched; mod A's hook installs the jump. Game
session uses the modified gate.

### Example 4 — Lifecycle subscription (C++)

```cpp
// hello-plugin.dll
#include <kcdx/Interfaces.h>

static PluginHandle g_handle = kInvalidPluginHandle;

void HandleEngineMessage(kcdxMessagingInterface::Message* msg) {
    if (msg->messageType == kMessage_PostLoadGame) {
        const char* save_name = static_cast<const char*>(msg->data);
        kcdx::log::info("Hello-plugin: save '%s' loaded", save_name);
    }
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_handle = api->GetPluginHandle("violetanvil.hello-plugin");
    auto* msg = static_cast<kcdxMessagingInterface*>(
        api->QueryInterface(kInterface_Messaging,
                            kcdxMessagingInterface::kVersion));
    if (!msg) return false;
    msg->RegisterListener(g_handle, nullptr, HandleEngineMessage);
    return true;
}
```

### Example 5 — Serialization

```cpp
// save-counter.dll
static PluginHandle g_handle;
static uint32_t g_counter = 0;
constexpr uint32_t kRecordTag = 'CNTR';

void OnSave(PluginHandle p) {
    auto* ser = /* fetch kcdxSerializationInterface */;
    ser->OpenRecord(kRecordTag, 1);
    ser->WriteRecordData(&g_counter, sizeof(g_counter));
}

void OnLoad(PluginHandle p) {
    auto* ser = /* fetch */;
    uint32_t tag, ver, len;
    while (ser->GetNextRecordInfo(&tag, &ver, &len)) {
        if (tag == kRecordTag && len == sizeof(g_counter)) {
            ser->ReadRecordData(&g_counter, sizeof(g_counter));
        }
    }
}

extern "C" __declspec(dllexport)
bool kcdxPlugin_Load(const kcdxInterface* api) {
    g_handle = api->GetPluginHandle("violetanvil.save-counter");
    auto* ser = static_cast<kcdxSerializationInterface*>(
        api->QueryInterface(kInterface_Serialization, 1));
    ser->SetUniqueID(g_handle, 0xC0FFEE01);
    ser->SetSaveCallback(g_handle, OnSave);
    ser->SetLoadCallback(g_handle, OnLoad);
    return true;
}
```

---

## Deferred to later

Documented here so authors know what's *not* in v0.1 and roughly
when to expect each. **Bug reports asking for these in v0.1 will be
closed; PR proposals to add them are welcome but probably better
coordinated against v0.2.**

- **Chained hooks on the same function.** v0.1 is strict
  first-wins. v0.2 will introduce a hook chain registry with
  defined ordering semantics, opt-in per `[[hook]]` via
  `chain = true`. Default stays first-wins.
- **After-hook with return-value introspection.** v0.1 supports
  "before-hook" (Lua callback runs, optionally skips original) and
  "skip" (Lua replaces original entirely). True "wrap" — call
  original first, inspect its return, optionally mutate — needs a
  different trampoline shape and ABI. v0.2+.
- **`kcdxScaleformInterface` equivalent.** KCD2's UI uses
  Scaleform/Flash (the `Apse` UI module). Hooking Flash event
  handlers from kcdx plugins is in scope for v0.2; needs its own
  Ghidra session.
- **`kcdxObjectInterface` equivalent.** SKSE's persistent C++
  object store. Easily emulated by plugins using
  `kcdxSerializationInterface`, so v0.1 ships without it.
- **NPC-interact / item-used / container-open gameplay events.**
  SKSE itself doesn't ship these as messaging events — they go
  through Papyrus. kcdx's analogue: plugins use
  `kcdxScriptingInterface::RegisterFunction` to expose C++ to
  KCD2's Lua; KCD2's own Lua scripts call into the plugin when the
  game fires the relevant event. If the user has a specific event
  they need fired by the engine itself (not by Lua), file an issue
  with the use case and we'll add it in v0.2.
- **Hot-reload of plugins.** SKSE doesn't have this; kcdx v0.1
  doesn't either. Restart the game.
- **ImGui / in-game config UI surface.** Plugins ship config files
  next to their DLL. v0.2+ may add an ImGui hook.
- **Native plugin DLLs in non-C++ languages.** Anything that can
  export the required C-ABI data symbol and entry function works,
  but kcdx ships C++ headers as the only first-class binding.
- **Cross-plugin function-call API formalization** (the SKSE
  `enb-api` / `TrueDirectionalMovementAPI` pattern). Available
  implicitly via `GetModuleHandle` + `GetProcAddress` against the
  plugin's DLL; v0.1 doesn't add a higher-level wrapper.
- **PolyHook2 as alternative detour engine.** kcdx is committed to
  MinHook for v0.1 (already vendored, working, used for engine's
  own hooks). PolyHook2 is what ReturnOfModding's vendored code
  presupposes; we adapt that code to MinHook with ~50 LOC of glue.
- **Code-cave techniques.** Trampolines via `VirtualAlloc` cover
  every functional need a cave would, with the only downside that
  the trampoline is too far from `WHGame.dll` for a rel32 jump (we
  use 14-byte abs jmp instead, same as MinHook does anyway). Cave
  discovery is its own research project, not v0.1.

---

## Open questions for implementation

These don't block the design but get answered during the relevant
phase:

1. **`pConsole.RegisterCommand` calling convention.** Not yet
   documented in `muyuanjin/kcd2-mod-docs`. Phase 7 budgets a
   ½-day Ghidra session. If varargs or unusual ownership semantics,
   the `[[command]]` schema may need adjustment (a typed args
   spec, similar to `[[hook]]`'s `signature`).
2. **Save-game serialization integration.** KCD2's save format
   isn't public; the `.kcdx` co-save (separate file) is the safer
   path and matches SKSE. Phase 6 confirms the save trigger is
   hookable stably.
3. **Address Library maintenance.** v0.1 ships kcdx's own database
   as part of the zip. Long-term, may split into a
   community-maintained sibling repo. Not blocking.
4. **Lua VM thread model.** Current mempatch hooks `lua_pcall`
   which strongly implies single-threaded, but
   `kcdxScriptingInterface::RegisterFunction` needs confirmation
   that registered functions are only called on the main thread.
   Phase 5 task.

---

## Implementation status

This doc tracks the v0.1 spec. Implementation phases:

| Phase | Status | Scope |
|---|---|---|
| 1 | **live-verified** | Foundation: locator pipeline copied from mempatch; `[[patch]]` works under `kcdx.toml` |
| 2 | **live-verified** | Plugin loader: DLL discovery, `kcdxPluginVersionData`, dependency topo-sort, hello-plugin example builds standalone |
| 3 | **live-verified** | Messaging + Task + lifecycle messages (kPostLoad/kPostPostLoad/kInputLoaded fire; save/load messages reserved for Phase 6) |
| 4a | **live-verified** | Trampoline allocator (branch + local pools) + `kcdxTrampolineInterface` + per-plugin log files with 20 MB cap |
| 4b.1 | **live-verified** | `[[hook]]` schema: raw-bytes function-entry detours via MinHook |
| 4b.2 | **live-verified** | `[[trampoline]]` schema + cross-plugin symbol table (export / target_symbol) |
| 4b.3 | **live-verified** | Unified conflict matrix (HookOnHook, PatchOverlapsEarlierHook, HookOverlapsEarlierPatch) + global apply order across all entry types |
| 5 | not started | Lua marshaling + `[[mid_hook]]` + `kcdxScriptingInterface` |
| 6 | not started | Save/load + `kcdxSerializationInterface` |
| 7 | not started | Address Library + `[[command]]` |
| 8 | not started | Docs + examples + v0.1.0 release |

Phase 4 verification recipe: [`docs/VERIFY_PHASE4.md`](VERIFY_PHASE4.md).
Three synthetic conflict-test plugins
(`examples/conflict-test-{hook-on-hook,patch-on-hook,hook-on-patch}/`)
exercise each new cross-engine conflict category. All three live-verified
on KCD2 1.5.1164953 — pre-flight predictions match apply-time behavior,
log lines name the conflicting plugins, and the orchestration honors
priority across entry kinds (a high-priority hook can apply earlier than
a low-priority patch).

The Phase 4b.3 work also introduced a separate `conflict_engine` module
that owns resolution, footprint collection, conflict classification, and
the unified apply order. Future engines (`[[mid_hook]]` in Phase 5, etc.)
plug in by registering write/read footprints — they don't need to know
about each other's conflict semantics directly.

Earlier verification recipes:
[`VERIFY_PHASE1.md`](VERIFY_PHASE1.md),
[`VERIFY_PHASE2.md`](VERIFY_PHASE2.md),
[`VERIFY_PHASE3.md`](VERIFY_PHASE3.md). Plus a mid-stride fix to runtime
version detection (parse kcd_launcher.log instead of relying on
WHGame.dll's missing VS_VERSIONINFO resource).

See `README.md` for the condensed roadmap.
