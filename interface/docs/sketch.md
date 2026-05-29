# kcdx interface — sketch

Working document. Decisions get promoted to a stable spec once locked.

## What this is

The desktop app the user double-clicks to launch a kcdx-enabled game session. MO2-class plugin/profile manager. The user sees one app called "kcdx"; under the hood, it is a runtime-agnostic shell hosting a kcdx runtime plugin.

The model: **the interface is the host; kcdx is one runtime among many it could host.** Today there is only kcdx. The architecture is built so adding a second runtime later is "ship a runtime DLL" — not "refactor the interface."

## The non-negotiable architectural constraint

When/if a second runtime appears, adding a game selector must be a **UI affordance becoming visible**, not a refactor. The interface today already talks to kcdx through the same contract it would use to talk to any future runtime. The selector being absent in v1 is a hidden affordance, not a missing layer. The layer exists with exactly one entry.

## Scope

**In scope, v1:**
- Plugin list — runtime supplies, interface renders as cards (icon, name, version, author, status).
- Reorderable load order — drag-to-reorder, persisted by the interface, applied via the runtime contract.
- Enable/disable per plugin — interface tells runtime which plugins are active for the active profile.
- Launch button — interface tells runtime "start the game"; runtime decides how.
- Plugin detail modal — full description, images, conflict info, link out to source. Card payload is runtime-supplied.

**In scope, later:**
- **Profiles** — interface-owned, named sets. A profile holds: an enabled-set + load order, and (eventually) user-data context. Two purposes: (1) mod sets (different gameplay configurations), (2) Steam/Xbox-style user separation (different save data per profile). The second is a future capability — kcdx has not implemented per-user-data save partitioning yet.
- **Save management** — list saves, attach to a profile, copy/move/delete, view metadata.
- **Multi-runtime / multi-game** — runtime selector becomes visible; plugins namespace by target game (`plugins/KingdomComeDeliverance2/`, etc.).

**Out of scope:**
- Mod downloading (no Nexus integration, no built-in browser) — v1.
- Editing plugin source or manifests.
- Replacing the engine's runtime conflict resolution — interface writes intent; runtime/engine still own merge and apply semantics.

## Platform decision (locked)

**Tauri 2 + React + TypeScript.**

- Rust backend: filesystem ops, process launch, dynamic-library loading (the runtime DLL).
- React + TypeScript frontend.
- WebView2 dependency accepted; ship with `embedBootstrapper` (~2 MB cost) so the ~1% of users without WebView2 get a silent first-launch install (no MSI, no admin prompt).
- Self-contained: one binary at the bin root, drag-drop install.

**Cross-platform future:** Tauri + Rust runs natively on Windows / macOS / Linux. The runtime contract (C ABI vtable) is platform-independent — `.dll` on Windows, `.so` on Linux, `.dylib` on macOS. The kcdx runtime itself is Windows-only because KCD2 is Windows-only, but the **interface architecture does not assume Windows.** A future Linux-native game would ship a `.so` runtime; the interface would load it unchanged. Avoid Windows-isms in the contract design (paths as `PathBuf`-ish opaque strings, no hardcoded `\\` separators in the contract, no Win32-specific types in the public ABI).

Rejected:
- ImGui — wrong tool for image-heavy cards, theming, modals, file dialogs.
- Qt — agents write React/TS far more fluently than Qt/QML; web ecosystem covers drag-reorder, image cards, modals out of the box.
- Electron — 150 MB cost without a feature win over Tauri.

## Runtime model — in-process vtable, reactive (passive-mode in v1)

The runtime is **a DLL the interface loads into its own process** via dynamic linking, exposing a known C ABI. Not a child process. Not JSON-RPC. Pure in-process function calls.

### Runtime discovery contract (single source of truth: the disk layout)

- Every runtime is a folder under `runtimes/`.
- **The folder name IS the runtime's ID.** No embedded `name()` lookup overrides it. What the user sees in Explorer is what the interface uses.
- Inside each folder, **the runtime entry file is named exactly `runtime.dll`** (`runtime.so` on Linux, `runtime.dylib` on macOS). Fixed. The interface loads only this file.
- **Adjacent files in the folder are runtime-private.** The runtime ships whatever side DLLs / data files it needs (vendored Lua, helper exes, config); the interface does not enumerate, classify, or warn about them. Windows resolves the runtime's dependencies via `LoadLibraryExW(LOAD_WITH_ALTERED_SEARCH_PATH)` so sibling DLLs in the folder are found automatically.
- A missing `runtime.dll` in a folder under `runtimes/` is a clean, well-defined error ("`runtimes/<name>/runtime.dll` not found; folder ignored"). The folder is skipped; other runtimes still load.

This gives the user a complete predictable model: every runtime is a folder; the folder's name is the runtime's identity; inside it, `runtime.dll` is the entry point; everything else belongs to the runtime. Rename a folder → rename the runtime. Copy a folder → make a sibling runtime (e.g. `runtimes/kcdx-dev/` next to `runtimes/kcdx/`). Delete a folder → uninstall the runtime. Filesystem gestures are the install/uninstall/rename UX. No hidden rule the user has to learn.

```
interface process (kcdx-interface.exe):
  startup → enumerate runtimes/*/
  for each subfolder:
    runtime_id = folder name                            (folder IS the ID)
    dll_path   = "<root>/runtimes/<id>/runtime.dll"
    if !exists(dll_path): log "runtime <id>: runtime.dll missing, skipped"; continue
    LoadLibraryExW(dll_path, LOAD_WITH_ALTERED_SEARCH_PATH)
    GetProcAddress("kcdx_runtime_v1")
    call it → get vtable
    register runtime under runtime_id
  if exactly one runtime registered AND no multi-runtime UI enabled → auto-select
  call runtime.init(callbacks)  — runtime gets a callback table for live updates
  rest of session: call into runtime vtable for actions; runtime MAY fire callbacks for live state
  on shutdown: call runtime.shutdown() — runtime promises no callbacks after this returns
```

Note: because the folder name IS the runtime ID, the vtable does **not** need a `name()` entry point. `display_name()`, `version()`, `target_game()` still exist (those are presentation data the runtime owns) but identity comes from disk.

The ABI is **reactive** (Architecture 2): the interface drives explicit actions (list plugins, set load order, launch), AND the runtime CAN push live updates to the interface via the callback table given at init (plugin status changed, conflicts recomputed, save list changed, progress on a slow op, log messages).

This is a strict superset of pure request/response. A runtime that wants to be purely passive implements `init` as "store the callbacks and never call them" — the interface still works, it just doesn't get live updates.

**v1 kcdx runtime is passive-mode.** All four reactive callbacks the contract supports — live conflict detection mid-session, plugin-status push, progress on slow ops, log streaming — require a runtime ↔ in-game-engine channel that doesn't exist today (the engine writes `kcdx-engine/logs/kcdx-dev_<ts>.log` and that's it; there's no pipe, shared memory, or debug protocol back to the host). v1 kcdx implements `init` as "store the callbacks; never call them" and ships passive. The reactive callbacks are designed into the v1 ABI so that a future runtime ↔ engine live channel (v2 capability) can use them without an ABI bump — building the channel is the work, not extending the contract.

Picked over a passive-only ABI because:
- Versioned vtables are append-only (`anti-patterns.md` AP11); adding the callback table later requires a `kcdx_runtime_v2` and every runtime in existence has to add a new export to opt into push. Designing it now costs trivially (v1 kcdx stores a struct of dead fn-ptrs) and saves the bump later.
- Long ops, when they ship in v2 (`detect_conflicts` over 200 plugins, slow plugin scans), don't freeze the UI — runtime works in its own thread, fires a callback when done.
- React's data-flow model maps directly onto callback-driven updates (Tauri's `emit` is thread-safe and built for exactly this — interface's callback impls emit Tauri events, React subscribes, UI re-renders).

**Runtime authors must document their callback behavior in their identity strings or capability declaration.** A runtime that ships passive-mode says so (so a UI feature relying on live updates degrades gracefully); a runtime that ships reactive says so. The interface adapts polling intervals / refresh affordances accordingly.

## Capability negotiation

The runtime declares what it supports via **nullable function pointers in the vtable.** A function pointer that is null means the capability is unsupported; the interface adapts (e.g. hides the Saves panel if `list_saves` is null).

```c
struct KcdxInterfaceCallbacks {
    // Interface guarantees: all callbacks are thread-safe.
    // Runtime guarantees: no callback fires after runtime.shutdown() returns.
    void (*notify_plugin_list_changed)(void* ctx);
    void (*notify_plugin_status_changed)(void* ctx, const char* plugin_id, PluginStatus);
    void (*notify_conflicts_changed)(void* ctx, const ConflictList*);
    void (*notify_save_list_changed)(void* ctx);
    void (*notify_progress)(void* ctx, const char* op_id, float fraction, const char* message);
    void (*notify_log)(void* ctx, LogLevel, const char* message);
    void* ctx;  // opaque interface handle, passed back into each callback
};

struct ActivePluginEntry {
    const char* plugin_id;          // [plugin].name from kcdx.toml
    const char* path;               // absolute path to the plugin folder on disk
    bool        enabled;            // user intent: load or skip
    const char* zone;               // "before_game" or "after_game"
    int         priority;           // load order within zone (lower = earlier)
};

struct KcdxInterfaceRuntime {
    // --- Required (interface treats missing as load failure) ---

    // Identity (presentation only — the runtime ID comes from the
    // containing folder name, not from the runtime).
    const char* (*display_name)(void);           // "kcdx — KCD2 Script Extender"
    const char* (*target_game)(void);            // "KingdomComeDeliverance2"
    Version     (*version)(void);

    // Lifecycle
    int  (*init)(const KcdxInterfaceCallbacks*);  // callbacks may be null → passive mode
    void (*shutdown)(void);                       // no callbacks after this returns

    // Plugin enumeration: interface tells runtime where to look; runtime
    // returns what it finds.
    PluginList* (*list_plugins_at)(const char* const* library_paths, size_t count);
    CardData*   (*get_card_data)(const char* plugin_id);

    // Plugin load decision: interface tells runtime the full active set,
    // each with its path. Runtime translates into engine input (e.g. writes
    // kcdx-engine/load_order.toml with paths). Array order IS the load order
    // (zone+priority are explicit but the array carries the canonical order).
    int  (*set_active_plugins)(const ActivePluginEntry* entries, size_t count);

    // Launch
    int  (*launch_game)(const LaunchOptions*);

    // Memory
    void (*free)(void* runtime_owned_ptr);

    // --- Optional (null = capability not supported) ---
    SaveList*     (*list_saves)(void);
    int           (*set_save_path)(const char* path);
    ConflictList* (*detect_conflicts)(void);
    // ... more as capabilities accrue
};
```

The interface checks `vtable.list_saves != null` before showing the saves UI, `vtable.detect_conflicts != null` before showing the conflict panel, etc.

**v1 kcdx runtime exposes (firm):** identity + lifecycle + `list_plugins_at` + `get_card_data` + `set_active_plugins` + `launch_game` + memory.

**v1 kcdx runtime — capabilities pending implementation choice:**
- `detect_conflicts` — nullable. The engine's `src/conflict_engine.cpp` does not appear to expose a queryable programmatic surface today (it logs at load time). If the runtime supplies conflict detection in v1 (engine-side query surface, log tail, or host-side re-derivation), `detect_conflicts` is non-null and the interface renders the conflict UI. If not, it's null and the conflict UI is hidden. Decision pinned during runtime implementation, not now.
- `list_saves` / `set_save_path` — null in v1. Save management is a later capability.

## Plugin card data — full payload upfront, cached per plugin version

`runtime.list_plugins()` returns the **full card payload for every plugin** in one call:

```c
struct CardData {
    const char* plugin_id;             // [plugin].name from kcdx.toml
    const char* display_name;          // human-readable (may equal plugin_id)
    const char* version;               // e.g. "1.2.3"
    const char* author;
    const char* short_description;     // one-line summary
    const char* long_description_md;   // markdown body (from README.md, see below)
    const char* icon_path;             // absolute path on disk, interface displays via file://
    const char* const* screenshot_paths;
    size_t       screenshot_count;
    PluginStatus status;               // enabled / disabled / engine-rejected / etc.
    const char* const* badges;         // short tags ("requires kcdx 0.6+", "experimental", ...)
    size_t       badge_count;
    // conflict info, etc. — see open questions
};
```

One call, one round trip, single struct. The interface caches the result. **Cache key: `(plugin_id, version)`** — the runtime advertises the cache key when it returns the payload; the interface only re-fetches a plugin's card when its version changes (i.e. the user updated the plugin on disk). Plugin enable/disable does NOT invalidate the cache — that's interface-state intent, not runtime-supplied card data.

Cache invalidation triggers (any of these → interface drops cache for that plugin and re-calls `list_plugins()`):
- Runtime fires `notify_plugin_list_changed()` callback (a plugin was added/removed/updated; runtime detects via filesystem watcher or scan).
- User explicitly invokes "rescan plugins" from the UI.
- Profile switch (load-order intent might change badges/status; card payload may differ).

### Where the runtime gets rich content from — convention sidecars

The kcdx runtime reads each plugin folder for these files alongside `kcdx.toml`:

| Source                                | Card field populated                      |
|---------------------------------------|--------------------------------------------|
| `kcdx.toml` `[plugin].name`           | `plugin_id`                                |
| `kcdx.toml` `[plugin].name` (display fallback) or future optional display key | `display_name` |
| `kcdx.toml` `[plugin].version`        | `version`                                  |
| `kcdx.toml` `[plugin].author`         | `author`                                   |
| `README.md` (first non-empty paragraph) | `short_description`                      |
| `README.md` (full body)               | `long_description_md`                      |
| `icon.png`                            | `icon_path` (absolute path)                |
| `screenshots/*.png` (sorted)          | `screenshot_paths` (absolute paths)        |
| Engine-state (enabled? rejected?)     | `status`                                   |
| Engine-state + manifest fields        | `badges`                                   |

Mod authors don't learn a new schema for UI metadata — they drop standard files into the plugin folder. `kcdx.toml` stays minimal. A plugin with no `README.md` / `icon.png` / `screenshots/` renders as a bare card with name + version + author (interface degrades gracefully).

This is a **runtime implementation choice**, not part of the interface contract. A future runtime is free to source card data from a database, an HTTP endpoint, or any other store — the contract is only the `CardData` struct shape.

## Versioning — "just works" via append-only versioned vtables

**Pattern: versioned entry points (`kcdx_runtime_v1`, `_v2`, ...), append-only within a version.**

The runtime DLL exports one or more versioned entry points. The interface tries the highest version it knows; falls back to lower if missing. Each version's vtable is **append-only** — new fields go at the end, older interfaces simply don't read them.

This is invisible plumbing for users. Versioning earns its keep on the one scenario that breaks "just works" without it:

- **Mismatched versions across an update.** User has interface v1.5, runtime built against interface v1.3 (because they updated one without the other).
  - With versioning: v1.5 interface sees a v1 vtable from the v1.3-built runtime, uses v1 features, skips v1.5 features. Graceful degrade.
  - Without versioning: undefined behavior, crash, or "version mismatch — please update." The opposite of just-works.

Runtime authors always link against the latest interface SDK header and ship; mismatches only happen if users update one half independently. The version negotiation is what makes that survivable. Same lesson kcdx's own plugin interface already encodes — same mental model, no second model to learn.

For v1: one entry point, `kcdx_runtime_v1`. Interface and runtime ship together in one zip — mismatches impossible until shipping them separately becomes a thing.

## ABI ownership rules

**Memory:** Runtime owns everything it returns. Interface calls `runtime.free(ptr)` when done. Runtime can use any allocator internally. Mirrors Rust/C++ FFI best practice.

**Threading — interface → runtime:** Interface guarantees serial calls into the runtime vtable. Runtime can assume any single vtable call won't race against another vtable call. (Runtime is still free to spawn its own internal threads — see below.)

**Threading — runtime → interface (callbacks):** Interface guarantees all callbacks in `KcdxInterfaceCallbacks` are thread-safe — the runtime may invoke any callback from any thread it owns. Implementation: the interface's callback wrappers emit Tauri events (`app_handle.emit(...)`), which is thread-safe by design; React subscribes; UI re-renders on the main thread. Runtime authors never marshal threads themselves.

**Lifecycle:**
- Interface calls `runtime.init(callbacks)` exactly once, before any other vtable call (except identity). Runtime may store the callback table, spawn internal threads, register engine hooks.
- During the session, interface calls vtable functions; runtime fires callbacks freely from any thread.
- Interface calls `runtime.shutdown()` exactly once, before `FreeLibrary`. Runtime promises: **no callback fires after `shutdown()` returns.** Runtime joins its threads, releases resources. After shutdown returns, the callback function pointers are dead — runtime must never call them again.
- The `ctx` opaque handle in the callback struct is owned by the interface; valid from `init` through `shutdown`, undefined after.

**Error model:**
- Vtable calls returning `int` return 0 for success, non-zero for failure (specific codes TBD per call).
- Callbacks are `void` — interface guarantees not to throw back into runtime. UI errors are caught and logged interface-side.
- Backpressure is the runtime author's discipline: do not fire `notify_*` in tight loops; coalesce / debounce at the runtime side. The interface is allowed to drop or coalesce repeated events if needed.

**Strings:** UTF-8, null-terminated, owned by whoever returned them (free via `runtime.free`).

**Paths:** Opaque UTF-8 strings. No platform-specific path types. Runtime interprets them per its own platform conventions.

## Profile-scoped state

**Profiles are interface-owned, runtime-unaware.** The runtime never knows what profile is active. The interface holds the profile concept; profiles are persisted by the interface.

A profile holds:
- The **plugin libraries** to scan (default + any user-added) and the duplicate-resolution choices.
- An **enabled-set** of plugin IDs.
- A **load order** for those plugins (zone + priority per plugin).
- (Eventually) A **save folder path** for the runtime to use (per-profile saves).
- (Eventually) Per-plugin config overrides.

**Source of truth for the load decision: the interface.** The runtime translates that decision into engine input.

Each session start (or each profile switch), the interface:

1. Scans every library in the profile via `runtime.list_plugins_at(library_paths, count)`. Cache by `(plugin_id, version)`.
2. Resolves duplicates (user choices remembered per profile).
3. Constructs the active set: `ActivePluginEntry[]` — for each enabled plugin, its `(plugin_id, absolute_path, enabled, zone, priority)`.
4. Calls `runtime.set_active_plugins(entries, count)`. The runtime translates this into whatever the engine reads.
5. If per-profile saves are supported: `runtime.set_save_path(profile.save_path)`.
6. On Launch: `runtime.launch_game(opts)`.

For the kcdx runtime, step 4 means **writing `kcdx-engine/load_order.toml`** as the complete list of plugins to load — each `[[plugin]]` row carrying name + path + enabled + zone + priority. The engine reads exactly that file at startup; nothing else feeds the engine's plugin list. The runtime's existing autonomous walk of `<game-bin>/kcdx-plugins/` goes away when the interface is wired in (engine change at integration time).

This is the precise shape of "interface drives intent; runtime decides implementation." The runtime never decides which plugins are active — it reflects the interface's decision into the engine. If the engine later grows a different intake (a programmatic API instead of a TOML file), only the runtime's translation changes; the contract is untouched.

**For save paths (future capability):** interface tells the runtime "use save path A for this profile." The runtime decides HOW (symlink, config edit, copy files, refuse if it cannot). Runtime that doesn't support this declares `set_save_path` null; the interface still tracks save-path intent per profile but doesn't surface the UI for that runtime.

## What the interface has baked in

The interface defines the slots; runtime fills the contents:

- The **plugin list** view exists, schema (rough): `{plugin_id, name, version, enabled, load_order_index, card_data}`.
- The **plugin card** schema exists: a canonical struct the runtime fills (name, version, author, short_description, long_description_md, icon_path, screenshot_paths, status, badges, conflict_info). Optional fields the runtime may omit; interface degrades gracefully on missing fields. **The runtime can source these fields however it wants** — read TOML, read an engine endpoint, query a database, combine multiple sources. TOML metadata in a plugin folder is **one possible implementation strategy**, not part of the interface contract.
- The **launch button** exists; behavior is `runtime.launch_game()`.
- The **profile model** exists (enabled-set, load order, eventually save path); applied via `set_active_plugins` and optional `set_save_path`.
- The **load order** model exists; the runtime applies it.
- The **saves panel** exists; visible only if the runtime supports `list_saves`.

What is NOT baked in:
- The plugin manifest format. Runtime owns it.
- Where plugins live on disk. Runtime owns it (interface's `plugins/<game>/` directory convention is a recommendation for runtimes that want it, not a contract requirement).
- How load order is persisted. Runtime owns it.
- How saves are detected and listed. Runtime owns it.
- How the game is launched. Runtime owns it.

## Disk layout — v1 shipped state

```
<interface-install>/                      # the interface install root
  kcdx-interface.exe                      # the Tauri app, user's entry point
  kcdx.exe                                # engine injector (existing binary, unchanged)
  runtimes/
    kcdx/
      runtime.dll                         # kcdx runtime adapter (host-side)
  plugins/                                # DEFAULT plugin library (built in)
    KingdomComeDeliverance2/              # namespaced by target game
      <plugin>/                           # one folder per plugin
        kcdx.toml
        plugin.lua / <plugin>.dll
        README.md                         # optional — long description for the card
        icon.png                          # optional — card icon
        screenshots/*.png                 # optional — card screenshots
  profiles/                               # interface-owned
    Default/profile.json                  # enabled-set, load order, library set, save path
  kcdx-engine/                            # engine-owned files only
    kcdx.dll                              # the engine; injected into KingdomCome.exe
    kcdx-watchdog.exe
    engine.toml
    load_order.toml                       # runtime writes this; engine reads it
    address-library/database.csv
    logs/
    builtin/<fix>/                        # first-party engine-fix plugins (still here)
  interface-config.json                   # window state, theme, last-used profile, plugin libraries
```

kcdx engine development continues on its existing `<game-bin>/kcdx-plugins/` layout during pre-v1 dev (engine code untouched by the interface project). When the interface is wired up and the integration shape is concrete, the engine drops its hardcoded plugin walk in favor of "load exactly what `load_order.toml` says, at the paths it specifies." That's a small engine change at integration time; until then, the existing engine works as-is. No user-facing migration because kcdx is prerelease and has no shipped users.

## Plugin libraries — Steam-style, default + user-added

The interface manages a set of **plugin libraries** — directories it scans for plugins. Modeled on Steam library folders.

- **One default library:** `<interface-install>/plugins/KingdomComeDeliverance2/`. Built in. Always present. Drag-drop adds drop here by default.
- **User-added libraries:** the user adds folders in Settings → Plugin Libraries → Add Folder. Each added folder is scanned for plugins. Common use case: plugins on a different drive ("D:\Mods\KingdomComeDeliverance2\").
- **Discovery:** interface scans every library in order, asks the runtime to enumerate plugins it finds (`runtime.list_plugins_at(library_paths, count)`). The runtime decides what's a plugin (kcdx looks for `kcdx.toml` in each folder).

**Per-game libraries.** A plugin library is namespaced by target game. `D:\Mods\KingdomComeDeliverance2\` is a library for KCD2; `D:\Mods\Skyrim\` would be a library for a hypothetical second runtime. The interface only feeds the active runtime libraries whose game matches its target.

### Duplicate plugin across libraries — surface as warning, user picks

If the same `plugin_id` is found in two or more libraries (user copied it, or pointed two libraries at overlapping locations), the interface:

1. Shows a "Duplicate plugin" modal at scan time listing every location with its path.
2. User picks one to activate; others are tagged "duplicate (inactive)" in the UI and skipped from the active set.
3. Choice is persisted per plugin ID per profile.

Not silent — duplicate plugins are an install hygiene issue worth surfacing.

## Naming and binary layout — separate binaries through pre-v1; merge later if wanted

**Through pre-v1 development:**
- `kcdx.exe` (at the bin root) stays exactly as it is today — the existing engine injector ([src/loader/main.cpp](../../src/loader/main.cpp)): `CreateProcessW(KingdomCome.exe, CREATE_SUSPENDED)` → `CreateRemoteThread(LoadLibraryW, kcdx-engine/kcdx.dll)` → `ResumeThread`. Steam users with `"<path>\kcdx.exe" %command%` in launch options keep working unchanged. kcdx engine development continues in parallel without launcher disruption.
- `kcdx-interface.exe` (at the bin root) is the new Tauri app. The user double-clicks it (or it auto-launches on first boot — TBD) to manage profiles, plugins, etc. When the user hits Launch in the UI, the interface spawns `kcdx.exe` (which then does the injection it already does).
- `runtimes/kcdx/runtime.dll` is loaded into `kcdx-interface.exe`'s process. The runtime adapter reads plugin manifests, writes `kcdx-engine/load_order.toml`, and spawns `kcdx.exe` on `launch_game()`. It does NOT contain the injection logic — that stays in `kcdx.exe`.

**By v1 release:**
- The two binaries may merge into one user-facing `kcdx.exe` (the Tauri app, with the injection logic ported into the runtime adapter or a private helper). Decision deferred until the interface is built and the integration shape is concrete. Steam `%command%` behavior gets designed at that point.
- Or they may stay separate if that's the cleaner shape after the interface is done.

This keeps the path forward open without committing to a `src/loader/main.cpp` rewrite while engine development is in flight.

**Three-binary picture (current, plus the new one):**

```
kcdx-interface.exe         NEW — Tauri app, loads runtime.dll, host-side
runtimes/kcdx/runtime.dll  NEW — host-side adapter: reads manifests, writes
                                  load_order.toml, spawns kcdx.exe
kcdx.exe                   EXISTING — engine injector (~100 KB, no UI)
kcdx-engine/kcdx.dll       EXISTING — the engine, injected into KingdomCome.exe
kcdx-engine/kcdx-watchdog.exe  EXISTING — spawned by kcdx.dll post-injection
```

The engine (`kcdx.dll`) is **inside the game process**, not the interface process. The runtime DLL the interface loads is a separate host-side thing that drives files + spawns processes — it cannot itself be the engine because the engine has to live inside KingdomCome.exe to do its work.

The runtime → engine channel today is asynchronous and one-way: the runtime writes `load_order.toml`, spawns `kcdx.exe`, and reads `kcdx-engine/logs/kcdx-dev_<ts>.log` after the game exits. A future live channel (pipe, shared memory, debug protocol) would let the runtime push real-time engine state to the interface as callbacks — v2 capability, not v1.

## UI shape (first pass, will refine)

```
+--------------------------------------------------------------+
| kcdx                                    [Profile: Default ▾] |
+-------------------+------------------------------------------+
| ▸ Plugins         |  [search…]                               |
| ▸ Saves           |                                          |
| ▸ Settings        |  ┌──────────────────────────────────────┐|
| ▸ About           |  | [img] PluginName            v1.2.3  ||
|                   |  |       Author — short description    ||
|                   |  |       [enabled ✓]  [⋮]              ||
|                   |  └──────────────────────────────────────┘|
|                   |  ┌──────────────────────────────────────┐|
|                   |  | [img] AnotherPlugin         v0.4.0  ||
|                   |  |       ...                           ||
|                   |  └──────────────────────────────────────┘|
|                   |                                          |
|                   |                          [▶ Launch game] |
+-------------------+------------------------------------------+
```

Open layout questions (defer until later):
- Load order: separate column from enable/disable, or one combined drag-list?
- Conflict surfacing: inline on the card, only in detail modal, or both?
- Profiles: dropdown in title bar, or first-class left-rail section?
- Save management's relationship to profiles: save belongs to profile? profile snapshots a save? both?

## What the interface owns vs the runtime owns

| Concern                              | Owner                                          |
|--------------------------------------|------------------------------------------------|
| Window, chrome, theme                | Interface                                      |
| Plugin list view                     | Interface                                      |
| Plugin card schema                   | Interface                                      |
| Plugin card field values             | Runtime                                        |
| Plugin libraries (default + user-added) | Interface                                   |
| Plugin discovery WHERE (libraries)   | Interface (passes to runtime)                  |
| Plugin discovery HOW (manifest parse) | Runtime                                       |
| Drag-drop add plugin                 | Interface (drops into the default library)     |
| Duplicate plugin resolution          | Interface (surfaces; user picks)               |
| Load-order intent                    | Interface                                      |
| Load-order persistence               | Runtime (writes `kcdx-engine/load_order.toml`) |
| Enable-set intent                    | Interface                                      |
| Enable-set persistence               | Runtime (same file)                            |
| Launch action                        | Runtime (spawns `kcdx.exe` injector)           |
| Profile concept                      | Interface                                      |
| Profile persistence                  | Interface                                      |
| Per-profile save path intent         | Interface                                      |
| Per-profile save path mechanism      | Runtime                                        |
| Conflict detection                   | Runtime (optional capability)                  |
| Conflict UI rendering                | Interface                                      |
| Capability advertisement             | Runtime (nullable fn-ptr in vtable)            |
| Capability adaptation                | Interface (UI surfaces hidden if null)         |

The key axis: **interface drives intent; runtime translates intent into engine input.** The interface never reaches around the runtime to touch engine files directly; the runtime never decides what plugins should be active.

## Bring-up plan — mock runtime first, real runtime last

The interface is built end-to-end against mock runtimes before the kcdx runtime adapter exists. Rationale: the **consumer (interface) drives what the contract needs to look like**, not the producer. Every UI surface the user clicks pulls on the mock, which IS the contract specification — written in code, in the consuming language. When the interface is done, the mock's shape IS the contract; the C header (`include/kcdx-interface/runtime.h`) gets derived from the Rust trait the interface ended up needing.

### Order of work

1. **Interface scaffolding** — Tauri app shell, React frontend, Rust backend with a `Runtime` trait (or equivalent Rust shape — TBD) matching the planned vtable surface (`list_plugins_at`, `get_card_data`, `set_active_plugins`, `launch_game`, `set_save_path`, `detect_conflicts`, lifecycle, callbacks).
2. **Two mock runtimes ship with the interface** (details below) — the interface loads them as if they were real `runtime.dll`s, but they're built into the interface binary itself for the bring-up phase.
3. **Build every UI surface against the mocks.** Plugin list, cards, drag-reorder, enable/disable, profile switch, library management, drag-drop add, duplicate-resolution modal, conflict panel, save panel, launch button — all driven by mock data.
4. **Derive the C header** (`include/kcdx-interface/runtime.h`) from the Rust trait once the interface is functional. Hand-translate Rust signatures + ownership comments into C; lock the ABI.
5. **Build the real kcdx runtime adapter** (`runtimes/kcdx/runtime.dll`) against the locked header. Reads `kcdx.toml` files, writes `kcdx-engine/load_order.toml`, spawns `kcdx.exe`. Plugged into the same `Runtime` trait the mocks implemented.
6. **Engine-side change at the same step**: `kcdx-engine/kcdx.dll` drops its autonomous walk of `kcdx-plugins/` and `kcdx-engine/builtin/`, loads exactly what `load_order.toml` lists at the paths given (§"How the kcdx runtime translates..." below). Lands paired with step 5.
7. **Integration verification**: every UI surface that worked against mocks works against the real kcdx runtime. The mock data shapes (designed in step 2) are what the kcdx runtime returns at step 5 — same fields, same conventions, same edge cases.

### The two mock runtimes

**Mock A — `runtimes/mock-full/runtime.dll`**: every optional capability enabled. Returns plausible-looking data for a fake KCD2-like target.

- 8-12 fake plugins with realistic `kcdx.toml` content covering the existing `cap-NN-*` test-plugin shapes (some `before_game` and some `after_game`, varied priorities, some `enabled=false`).
- Real-looking README markdown, icons (PNG), and a screenshots set per plugin so cards render with full payload.
- Fake conflicts (two plugins claiming the same hook site, etc.) so the conflict panel is exercised even before `detect_conflicts` has a real implementation.
- Two plugin libraries' worth of plugins to exercise library management + drag-drop add + duplicate-resolution.
- `list_saves` returns a fake save list; `set_save_path` accepts and echoes.

**Mock B — `runtimes/mock-reduced/runtime.dll`**: minimum-required surface only, every optional capability null.

- Identity + lifecycle + `list_plugins_at` + `get_card_data` + `set_active_plugins` + `launch_game` + memory — and nothing else.
- `detect_conflicts`, `list_saves`, `set_save_path` all null.
- Different `target_game()` string (e.g. "MockReducedGame") so the runtime selector visibly distinguishes the two.

Mock B forces the capability-adaptation code paths to be built and tested during bring-up: the saves panel hides, the conflict panel hides, the save-path UI is absent. If we only had Mock A, we'd find out at kcdx integration that the interface silently assumes everything is supported.

Mock B also proves the multi-runtime selector layer works even though only kcdx will ever be the real one. Selector visibility (the affordance hidden when only one runtime is registered) is exercised — switch to "one mock visible" mode to verify the auto-select behavior; switch to "both visible" to verify the selector UI.

### Mock data shapes mirror kcdx

The mock plugins use the same field names and conventions kcdx will use: `[plugin].name` as plugin_id, `README.md` for description, `icon.png` for icon, etc. When the real kcdx runtime is wired in at step 5, existing kcdx test-plugins (`cap-01-bytes`, `cap-08-messaging`, …) should slot directly into the mock's card-rendering paths because the shapes match. The mock data set IS the integration test data for step 7.

### Output: the contract is the Rust trait + the locked C header

By the time step 4 runs, the contract has been exercised by hundreds of UI interactions. Edge cases the sketch missed surface organically (e.g. "what does `set_active_plugins` return when the same plugin_id appears twice in `entries`?" — surfaces when the interface tries to do something stupid and the trait has to define a return value). The C header drafted at step 4 reflects everything the interface actually needs, not what the sketch predicted it would need.

## How the kcdx runtime translates contract calls into engine input (verified)

Facts read from the code, so the runtime implementer doesn't re-research:

| Contract call             | Runtime translation                                                                            |
|---------------------------|------------------------------------------------------------------------------------------------|
| `list_plugins_at(libs)`   | For each library path, scan its immediate subdirs for `kcdx.toml`. Each match is a plugin. Build a `PluginList` keyed by `[plugin].name`. ([load_order.cpp:23](../../src/load_order.cpp#L23)) |
| `get_card_data(plugin_id)`| Read the plugin folder: `kcdx.toml` for static fields, `README.md` for descriptions, `icon.png` and `screenshots/*.png` if present. |
| `set_active_plugins(entries)` | Write `kcdx-engine/load_order.toml` as the complete plugin list. Each entry → one `[[plugin]]` row with `name`, `path`, `enabled`, `zone`, `priority`. ([load_order.cpp:48](../../src/load_order.cpp#L48), [paths.h:14](../../src/paths.h#L14)) |
| `launch_game(opts)`       | Spawn `<install>/kcdx.exe` with the args the user/Steam wants on `KingdomCome.exe`. The injector does the rest. ([loader/main.cpp](../../src/loader/main.cpp)) |
| `detect_conflicts()` (optional) | `src/conflict_engine.cpp` does not expose a queryable surface today (logs at load time). For v1 the runtime either (a) builds an engine-side query surface, (b) tails the dev log post-launch, or (c) re-derives host-side from manifests. If any path lands, the entry point is non-null; if none, it stays null and the conflict UI is hidden. |
| Live callbacks (`notify_*`) | None in v1. No out-of-process channel from in-game engine exists; runtime stores callbacks at init and never fires them. v2 capability when a runtime ↔ engine live channel is built. |

**Engine-side change required at integration time:** the engine stops auto-walking `<game-bin>/kcdx-plugins/` and `<game-bin>/kcdx-engine/builtin/` ([plugin_loader.cpp](../../src/plugin_loader.cpp)). Instead, it loads exactly what `load_order.toml` lists, at the paths given. Small focused change; the engine already reads `load_order.toml` ([load_order.cpp:48](../../src/load_order.cpp#L48)) — this widens its role from "user overrides" to "authoritative plugin list."

## Repo location — decided: stays in this repo

The interface project lives at `interface/` under this repo (the kcdx repo) through interface bring-up and v1. Reasons: contract churn is highest during the design phase, so the lockstep value of one repo (one PR can touch engine + runtime + interface together) dominates the toolchain-mix cost. The orchestrator/governance discipline already built in `.claude/` transfers directly to the interface work. The `publish-public.ps1` allowlist machinery is already there.

Adding `interface/` to the public allowlist when it's ready to ship is one line; the `interface/docs/` content stays private until the contract is stable and we want to expose runtime-author documentation.

A future split (interface as its own repo, kcdx as a consumer of a vendored `runtime.h`) is reasonable when (a) the contract is locked and not churning, and (b) the interface evolves on its own schedule. Not now.

## Open questions

None. Every contract-shape, ownership, discovery, and bring-up decision is settled. The next step is bring-up step 1: interface scaffolding.

(Decisions locked over the course of this sketch, listed for posterity:)
- **Bring-up order**: interface scaffold → two mock runtimes → build every UI surface against mocks → derive C header from the Rust trait → build real kcdx runtime + engine-side change → integration verification. Mock-first, header-derived, real-runtime-last.
- **Two mock runtimes**: Mock A (`runtimes/mock-full/`) with every capability enabled and rich data; Mock B (`runtimes/mock-reduced/`) with only required capabilities, every optional one null. Mock B forces capability-adaptation code paths to be exercised before integration.
- **Build integration**: separate `interface/build-interface.ps1`. Engine `build.ps1` stays focused on the engine; interface build script handles cargo + npm + Tauri pipeline. Optional top-level `build-all.ps1` runs both if wanted.
- **Binary merge timing**: defer the `kcdx.exe` + `kcdx-interface.exe` merge decision until the interface is functional. Build as separate binaries; evaluate merge pre-v1 based on the working system. Steam `%command%` story decided at that point, not before.
- **Repo location**: stays in this repo through interface bring-up and v1. Future split (interface as its own repo) considered only when contract is locked AND interface evolves on its own schedule. Not now.

## Engine-side work needed for integration (not interface concerns)

These items don't shape, block, or constrain interface development. They're kcdx engine prereqs that have to land at bring-up step 5/6 (real-runtime integration), tracked here so the integration debt stays visible:

1. **`detect_conflicts` implementation in the kcdx runtime adapter.** Three plausible paths: (a) add a programmatic query surface to `src/conflict_engine.cpp`, (b) tail `kcdx-engine/logs/kcdx-dev_<ts>.log` for conflict lines, (c) re-derive host-side from plugin manifests. Pick at integration time. If none lands, the runtime declares `detect_conflicts` null and the interface hides the conflict panel — the contract handles this gracefully.
2. **`kcdx-engine/load_order.toml` widening.** Engine currently uses the file as user overrides on top of an autonomous walk of `<game-bin>/kcdx-plugins/` and `<game-bin>/kcdx-engine/builtin/`. At integration: engine drops the walk; loads exactly what `load_order.toml` lists at the per-row `path` field. Schema gains a `path` field per `[[plugin]]` row. Small focused engine change.

## Requirements (won't get lost)

Captured here as a flat list so nothing's missed when this gets turned into a spec:

### ABI shape

1. Interface is the host; runtime is an in-process DLL plugin.
2. Runtime exposes a versioned C ABI vtable (`kcdx_runtime_v1`), append-only within a version.
3. Capabilities advertised via nullable function pointers; interface adapts to what's present (e.g. hides conflict UI if `detect_conflicts` is null).
4. Memory: runtime owns + frees what it returns; interface calls `runtime.free`.
5. Threading interface → runtime: serial calls into the vtable; runtime may assume no two vtable calls race.
6. Threading runtime → interface: callbacks are thread-safe; runtime may invoke them from any thread; interface marshals to UI thread via Tauri events.
7. Lifecycle: `init(callbacks)` once before other vtable calls; `shutdown()` once before `FreeLibrary`; no callbacks fire after `shutdown()` returns.
8. Strings UTF-8, paths opaque, no platform-specific types in the ABI.
9. ABI is reactive (Architecture 2): vtable for interface-driven actions PLUS callbacks for runtime-pushed live updates. A runtime that doesn't need push implements `init` as no-op storage; same ABI, passive behavior.
10. v1 kcdx runtime is passive-mode (Architecture 1 behavior within an Architecture 2 ABI): no out-of-process channel from in-game engine exists today, so the runtime stores callbacks at init and never fires them. Live push is a v2 capability.
11. Cross-platform considered in ABI design (runtime contract is platform-independent; kcdx runtime is Windows-only because KCD2 is, not because the interface is).

### Runtime discovery

12. **Folder name IS the runtime ID.** Single source of truth = the disk layout. Filesystem gestures (rename, copy, delete) are the install/uninstall/rename UX. The vtable has no `name()` entry point.
13. **Runtime entry filename: `runtime.dll`** (`.so` on Linux, `.dylib` on macOS). Fixed. Adjacent files in the folder are runtime-private; the interface loads only `runtime.dll`. Sibling DLLs resolve via `LoadLibraryExW(LOAD_WITH_ALTERED_SEARCH_PATH)`.
14. v1: kcdx preinstalled, single runtime, no selector visible — selector layer present with one entry, affordance hidden.
15. When a second runtime appears, adding the selector is "ship a runtime + flip a UI flag", not a refactor.

### Ownership

16. **Source of truth for the load decision: the interface.** Interface decides which plugins are active, in what order, from which library. Runtime translates that decision into engine input.
17. Interface owns: window, plugin list view, card schema, plugin libraries, plugin discovery WHERE, drag-drop add, duplicate resolution, profile concept, load-order intent, enable-set intent, save-path intent, capability adaptation, callback marshaling to UI thread.
18. Runtime owns: manifest format, plugin discovery HOW (interprets folders into plugins), card field values, load-order persistence (writes `kcdx-engine/load_order.toml`), enable-set persistence (same file), save-path mechanism, conflict detection, launch (spawns `kcdx.exe`), all internal threading.
19. Runtime card data is **runtime-supplied**, not parsed from a fixed manifest format by the interface. TOML-in-plugin-folder is one possible runtime implementation, not part of the contract.

### Plugin libraries + cards

20. **Plugin libraries are interface-managed: one default + user-added.** Default = `<install>/plugins/KingdomComeDeliverance2/`; users add more in Settings (Steam-library style, support different drives).
21. **Plugin discovery: interface tells runtime where to look.** `list_plugins_at(library_paths, count)`. The runtime never has hardcoded plugin locations.
22. **Per-game library namespacing**: a plugin library is for one target game. Interface feeds the active runtime only libraries whose game matches its `target_game()`.
23. **Drag-drop add** drops a plugin folder into the default library, then re-scans.
24. **Duplicate plugin across libraries surfaces as a warning, user picks**; choice persisted per plugin ID per profile.
25. **Card payload is full upfront**: `list_plugins_at()` returns the complete card payload per plugin. Interface caches by `(plugin_id, version)`; cache invalidates on `notify_plugin_list_changed()`, explicit rescan, or profile switch.
26. **Card data sources (kcdx runtime convention)**: `kcdx.toml` for static identity, `README.md` for descriptions, `icon.png` for icon, `screenshots/*.png` for screenshots, engine-state for status/badges. Runtime implementation choice — contract is only the `CardData` struct shape.

### Profiles + active set

27. Profiles are interface-owned. A profile holds: plugin libraries (default + user-added) + duplicate resolutions + enabled-set + load order + eventual save path + eventual per-plugin overrides. Two purposes: mod sets, Steam/Xbox-style user separation.
28. **`set_active_plugins(entries, count)`** carries per-plugin `{plugin_id, path, enabled, zone, priority}`. Array order is the canonical load order. Runtime translates into engine input. For kcdx, that's writing `kcdx-engine/load_order.toml` with one `[[plugin]]` row per entry.
29. **`set_save_path(path)`** (optional capability): interface tells runtime "use this save path"; runtime decides HOW (symlink, config edit, copy files, refuse).

### Binaries + disk layout

30. **Three binaries at the interface install root**: `kcdx-interface.exe` (Tauri app), `kcdx.exe` (existing engine injector, unchanged), `runtimes/kcdx/runtime.dll` (host-side adapter that the interface loads).
31. The kcdx engine (`kcdx-engine/kcdx.dll`) lives **inside the game process** (injected by `kcdx.exe`), not inside the interface process. The runtime DLL the interface loads is a separate host-side adapter; it is not the engine.
32. Watchdog (`kcdx-engine/kcdx-watchdog.exe`) unchanged — spawned by kcdx.dll post-injection.
33. Disk layout: `<install>/{kcdx-interface.exe, kcdx.exe, runtimes/<id>/runtime.dll, plugins/<game>/<plugin>/, profiles/<name>/profile.json, kcdx-engine/..., interface-config.json}`.
34. Pre-v1, the `kcdx.exe` + `kcdx-interface.exe` merge is evaluated — decision deferred until the interface is built and the integration shape is concrete. Steam `%command%` handling designed at that point.

### Engine-side change at integration time

35. Engine drops its autonomous walk of `<game-bin>/kcdx-plugins/` and `<game-bin>/kcdx-engine/builtin/`. Instead, it loads exactly what `kcdx-engine/load_order.toml` lists, at the paths the file specifies. `load_order.toml`'s schema gains a per-row `path` field. Small focused engine change; lands when the interface is wired in.

### Prerelease

36. kcdx is prerelease with no shipped users; no backwards-compat or migration story for `kcdx-plugins/`. Build forward. The v1 layout is the only layout that ever ships.

### Bring-up sequence

37. **Mock runtime first, real runtime last.** Interface is built end-to-end against mock runtimes before the kcdx runtime adapter exists. The consumer drives what the contract needs to look like; the contract is derived from the working interface.
38. **C header is derived, not predicted.** `include/kcdx-interface/runtime.h` is drafted AFTER the interface is functional against mocks — by reading the Rust trait the interface ended up needing and hand-translating to C with ownership + threading annotations. Not before.
39. **Two mock runtimes ship in the bring-up phase.** Mock A (`runtimes/mock-full/`) — every optional capability enabled, rich plausible data, two libraries' worth of plugins, fake conflicts, fake saves. Mock B (`runtimes/mock-reduced/`) — only required surface, every optional capability null. Mock B forces capability-adaptation paths to be tested before kcdx integration.
40. **Mock data shapes mirror kcdx.** Field names and conventions match what kcdx will produce so that, at integration, kcdx test-plugins slot into the same rendering paths as mock plugins. The mock data set is the integration test data.
41. **Engine-side load_order.toml widening is paired with real-runtime bring-up.** Engine drops `kcdx-plugins/` + `kcdx-engine/builtin/` autonomous walks; reads exactly what `load_order.toml` lists at the per-row `path`. Lands at the same step as the real kcdx runtime adapter, not before.
