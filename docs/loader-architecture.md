# kcdx loader architecture

How kcdx gets loaded into the KCD2 process. Phase 1 of the restructure
flipped the install model from Ultimate-ASI-Loader-based injection to
kcdx's own launcher exe (`kcdx.exe`).

## Status

- **v0.2 (current):** kcdx ships its own launcher (`kcdx.exe`) that
  spawns KingdomCome.exe suspended, injects `kcdx-engine/kcdx.dll` via
  CreateRemoteThread + LoadLibraryW, then resumes the game. No
  third-party loader dependency.
- **v0.1 (historical):** rode Ultimate ASI Loader (`dinput8.dll`).
  kcdx itself shipped as `kcdx.asi` dropped into `plugins/` alongside
  user plugins. This model is retired; existing installs migrate per
  [`migration.md`](migration.md).

The v0.2 flip happened in Phase 1 of the restructure plan
([`outstanding-work/restructure-plan.md`](outstanding-work/restructure-plan.md)).
Rationale below.

## v0.2 layout (current)

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe                  (vanilla)
├── WHGame.dll                       (vanilla)
├── kcdx.exe                         (LAUNCHER — user runs this)
├── kcdx-README.txt                  (install + Steam launch options)
├── kcdx-engine/                     (everything kcdx-owned)
│   ├── kcdx.dll                     (engine — injected by launcher)
│   ├── kcdx-watchdog.exe            (crash-bundle sidecar)
│   ├── engine.toml                  (engine config)
│   ├── load_order.toml              (user load-order overrides)
│   ├── address-library/
│   │   └── database.csv
│   ├── logs/
│   │   ├── kcdx_<ts>.log            (engine log)
│   │   ├── kcdx-dev_<ts>.log        (dev trace)
│   │   ├── kcdx-launcher_<ts>.log   (launcher's own log)
│   │   ├── kcdx-watchdog_<ts>.log
│   │   ├── kcdx_<ts>.dmp            (in-process minidump on SEH-caught crash)
│   │   └── crash/
│   │       └── crash_<ts>.zip
│   └── builtin/                     (first-party engine fixes; ship with kcdx)
│       └── bugsplat-filename-fix/
│           ├── kcdx.toml
│           └── bugsplat-fix.dll     (optional; DLL-based engine fixes)
└── kcdx-plugins/                    (ONLY user/third-party plugins)
    └── <plugin-name>/
        ├── kcdx.toml
        ├── plugin.lua / <plugin>.dll
        └── logs/
            └── <manifest.name>_<ts>.log
```

### Key principles

- **`kcdx.exe` is the only kcdx file at the game-bin root** — sibling
  of `KingdomCome.exe`. The user clicks one binary to launch.
- **`kcdx-engine/` and `kcdx-plugins/` are siblings.** `kcdx-engine/`
  holds everything kcdx ships. `kcdx-plugins/` is exclusively for
  third-party user-installed plugins — nothing kcdx-owned lives there.
  Both folders use the `kcdx-` prefix to make ownership unambiguous at
  a glance and to avoid colliding with KCD2's vanilla `mods/` (pak mods)
  and the ASI-loader-era `plugins/` folder.
- **No more `.asi` extension.** The engine binary is `kcdx.dll`, loaded
  directly by `kcdx.exe` via CreateRemoteThread, not by an ASI loader
  scanning `*.asi` files.
- **No third-party loader dependency.** `dinput8.dll` (Ultimate ASI
  Loader proxy) is no longer required.

### Steam launch options

In Steam: right-click *Kingdom Come: Deliverance II* → Properties →
Launch Options. Set to the full quoted path of `kcdx.exe`, e.g.:

```
"E:\SteamLibrary\steamapps\common\KingdomComeDeliverance2\Bin\Win64MasterMasterSteamPGO\kcdx.exe"
```

Steam's overlay is preserved — `kcdx.exe` spawns the game via
`CreateProcessW`, which keeps Steam's process-tracking hooks intact.

### The launcher's mechanism

`kcdx.exe` does four things:

1. **Resolve paths**: derives its own directory from
   `GetModuleFileNameW(NULL)`. Looks for:
   - `KingdomCome.exe` as a sibling.
   - `kcdx-engine/kcdx.dll` one folder down.
   - `kcdx-engine/logs/` for its own log file.
2. **`CreateProcessW(KingdomCome.exe, ..., CREATE_SUSPENDED)`** —
   game starts frozen.
3. **`CreateRemoteThread(LoadLibraryW, "<full-path>/kcdx-engine/kcdx.dll")`** —
   inject. Wait for the thread to finish; read its exit code (HMODULE
   of the loaded DLL, or 0 on failure).
4. **`ResumeThread(main_thread)`** — game continues.

If injection fails (Windows Defender / third-party AV intercept), the
launcher logs to `kcdx-engine/logs/kcdx-launcher_<ts>.log`, kills the
suspended game process (so the user doesn't get vanilla KCD2 silently),
and shows an actionable error dialog naming the failure mode.

Source: [`src/loader/main.cpp`](../src/loader/main.cpp). One TU, no
dependencies beyond Win32. Under 150 KB stripped.

### kcdx.dll's perspective at runtime

Once injected, `kcdx.dll`'s `DllMain` runs inside KingdomCome.exe's
process. Path resolution (in [`src/paths.cpp`](../src/paths.cpp)):

- Self lives at `<game-bin>/kcdx-engine/kcdx.dll`.
- `EngineDataDir = <game-bin>/kcdx-engine/` (same folder as self).
- `PluginsDir = <game-bin>/kcdx-plugins/` (sibling of kcdx-engine/).
- Builtin discovery walks `<game-bin>/kcdx-engine/builtin/`.

## Engine-fix plugins (`kcdx-engine/builtin/`)

A **second** plugin discovery root, distinct from user-installed
plugins under `kcdx-plugins/`. Engine-fix plugins are first-party kcdx
engine fixes for issues in WHGame.dll or other shipped game binaries
(BugSplat configuration, etc.) — they ship with kcdx, not separately,
and apply unconditionally.

### What lives here

Anything kcdx wants to fix about KCD2's stock binaries that doesn't
belong in the engine source itself. Concrete first inhabitant:
`bugsplat-filename-fix/` (see
[`known-issues/`](known-issues/) — intercepts BugSplat's
`MiniDmpSender` constructor so the dmp filename doesn't contain a
colon, which Windows rejects).

### How it differs from user plugins

| Property | `kcdx-plugins/<X>/` | `kcdx-engine/builtin/<X>/` |
|---|---|---|
| Authored by | third-party modders | kcdx maintainers |
| Distributed via | Nexus / Workshop / direct download | kcdx release zip |
| Discovered by | `config::WalkForTomls` walking `<game-bin>/kcdx-plugins/` | same walker, also walks `<game-bin>/kcdx-engine/builtin/` |
| Apply order | per `[load_order].priority`, by default after engine fixes | by default applied first via Source::Engine tiebreaker; user can override |
| User opt-out | `enabled = false` in `kcdx-engine/load_order.toml` | same — disable a specific engine fix without uninstalling all of kcdx |
| Per-plugin log file | `kcdx-plugins/<X>/logs/<X>_<ts>.log` | uses the plugin's manifest `name` for the log file |

Discovery walks both roots into one unified candidate list; the
two-root walk is a small extension of the single-root walker in
`config::WalkForTomls`. Engine-fix entries are tagged internally so
log lines distinguish them.

### Why "first-party only"

`kcdx-engine/builtin/` is for **engine fixes maintained by the kcdx
project**, not a general "system plugins" folder. The deliberate
scope keeps the loader contract simple: engine fixes are part of
kcdx, ship with kcdx, get reviewed before merge, and have the same
versioning + release cadence. Third-party patches go under
`kcdx-plugins/` like any other mod.

If a kcdx user wants to write their own always-on patch, they ship
it as a user plugin and document that it should be left enabled.
There's no advantage to giving third parties write access to
`kcdx-engine/builtin/`.

### Legacy declarative-patch engine

A deprecated predecessor engine originally handled declarative byte
patches. All byte-rewrite work now flows through kcdx: user plugins
for third-party patches, and `kcdx-engine/builtin/` for first-party
engine fixes. The legacy engine's last released build remains
functional against KCD2 1.5 for anyone already using it, but no new
work targets it.

## Decisions along the way

### "Use a proxy DLL (`version.dll`) instead of a launcher"

Considered. Pros: Steam "Play" works unchanged, no user-side launch
config. Cons: masquerades as a system DLL (looks dodgy in a game
folder), can conflict with other ASI mods that proxy the same target,
breaks the "deliberate SKSE port" charter (SKSE doesn't proxy — it
launches).

Rejected in favor of the SKSE-style launcher. Honest naming +
ecosystem citizenship outweigh the one-time Steam launch-options doc
cost.

### "Name the proxy `kcdx.dll`"

Not viable as a proxy. The proxy trick requires the DLL filename to
match a system DLL Windows resolves from the exe directory first. A
DLL called `kcdx.dll` next to the game exe just sits there — nothing
loads it. That's exactly why we have a separate `kcdx.exe` doing the
injection.

### "Folder names: kcdx-engine/ + kcdx-plugins/ (vs engine/ + plugins/)"

The v0.2 layout uses `kcdx-engine/` and `kcdx-plugins/`, both with the
explicit `kcdx-` prefix. Reasons:

- **Ownership is unambiguous.** Anyone browsing
  `<game>/Bin/Win64MasterMasterSteamPGO/` sees the kcdx-prefixed
  folders and knows what put them there. A bare `engine/` or `plugins/`
  is generic enough to be confused with KCD2's own folders (the game's
  `mods/` folder is already a pak-mod sibling at the game root).
- **No collision with the ASI-loader era.** v0.1 dropped `kcdx.asi`
  into a `plugins/` folder that already held random `.asi` files from
  other mods. Naming the v0.2 plugin scan root `kcdx-plugins/` makes
  it unmistakably a kcdx-owned folder, separate from any leftover
  `plugins/` an existing user might have.
- **Symmetry between the two folders kcdx owns.** Both `kcdx-engine/`
  and `kcdx-plugins/` share the prefix — they read as a pair.

### "Just put the address library DB in the engine binary as constexpr"

Rejected because it forces a full kcdx release for any new ID. Loose
data files (csv under `kcdx-engine/address-library/`) let the community
PR database updates that ship independently of engine binaries.

## What this means for code accessing files

Engine config, logs, and address library DB live under
`<game-bin>/kcdx-engine/`. Code accesses them via
`kcdx::paths::EngineDataDir()` / `EngineDataDirPath()`. After Phase 1
these resolve to `kcdx-engine/`. Any code referring to literal
`kcdx-engine/` path strings was updated in Phase 1 (`src/paths.cpp`,
the load_order builtin-classifier substring check).

User plugins live under `<game-bin>/kcdx-plugins/` (accessed via
`kcdx::paths::PluginsDir()`).
