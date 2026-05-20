# kcdx loader architecture

How kcdx gets loaded into the KCD2 process, what we ship today, and
the planned end-state. Captures a 2026-05-19 design discussion so
the rationale doesn't get lost.

## Status

- **v0.1 (current):** rides on Ultimate ASI Loader (`dinput8.dll`).
  kcdx itself ships as `kcdx.asi` dropped into `plugins/` alongside
  user plugins.
- **v0.2+ (planned):** SKSE-style — our own launcher
  (`kcdx-loader.exe`) injects our own engine (`kcdx.dll`). Drops the
  third-party loader dependency. Ships a UI for load-order
  management.

We deliberately stay on the v0.1 model until we're ready to ship the
UI, because (a) the launcher exe only earns its keep if it has a UI
to host, and (b) the v0.1 model is working live-verified today.

## v0.1 layout (current)

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe
├── WHGame.dll
├── dinput8.dll                       (Ultimate ASI Loader proxy)
├── plugins/
│   ├── kcdx.asi                      (engine binary, loaded by ASI loader)
│   ├── kcdx-watchdog.exe             (external crash-bundle sidecar)
│   └── <plugin-name>/
│       ├── kcdx.toml
│       ├── *.dll
│       └── logs/
│           └── <manifest.name>_<ts>.log
└── kcdx-engine/                      (engine-owned data, sibling of plugins/)
    ├── engine.toml                   (engine config)
    └── logs/
        ├── kcdx_<ts>.log             (one per session)
        ├── kcdx-dev_<ts>.log         (only when dev_mode = true)
        ├── kcdx-watchdog_<ts>.log    (watchdog's own diagnostic log)
        └── crash/
            └── crash_<ts>.zip        (only when game exit code != 0)
```

Notes:
- The engine binary `kcdx.asi` lives **inside** `plugins/` because
  that's where Ultimate ASI Loader scans. Plugin discovery walks
  subdirectories of `plugins/` and skips loose files at depth 0,
  so `kcdx.asi` and `kcdx-watchdog.exe` are ignored by discovery
  (neither tries to load itself as a plugin).
- `kcdx-watchdog.exe` is a tiny (~280KB) external sidecar that
  kcdx.asi spawns at startup. It blocks on the game's process
  handle with `WaitForSingleObject` (zero CPU) and, on game
  crash, zips engine + plugin + game logs and crash artifacts
  into `kcdx-engine/logs/crash/crash_<ts>.zip`. See
  [`logging.md`](logging.md) §"Crash bundles".
- Engine-owned data files (config, logs, address library) live in a
  **sibling** `kcdx-engine/` folder. Keeping them out of `plugins/`
  means uninstall-by-deleting is unambiguous: delete `kcdx-engine/`
  + `plugins/kcdx.asi` + `plugins/kcdx-watchdog.exe` to remove
  kcdx, leave the rest of `plugins/` alone (other ASI mods or
  kcdx plugin folders).
- `kcdx-engine/` is auto-created on first launch by `paths::Init`.
  Users don't need to mkdir it manually. The `logs/` and
  `logs/crash/` subfolders are created on demand by `log::Init`
  and the watchdog respectively.
- Per-plugin log files
  (`<plugins>/<plugin>/logs/<manifest.name>_<ts>.log`) stay inside
  each plugin's own folder — they're plugin-owned, not
  engine-owned. Filename uses the plugin's `[plugin] name` from
  its `kcdx.toml`, not the install folder name.
- `dinput8.dll` is Ultimate ASI Loader. The user is responsible for
  having installed it (or installs it from our README). It proxies
  the system `dinput8.dll` and additionally LoadLibrary's every
  `*.asi` in the folder it lives in.

## v0.2+ layout (planned, NOT current)

```
<game>/Bin/Win64MasterMasterSteamPGO/
├── KingdomCome.exe
├── WHGame.dll
├── kcdx-loader.exe                   (launcher — user runs this)
├── kcdx.dll                          (engine — injected by loader)
├── kcdx-README.txt                   (install + Steam launch options)
└── kcdx/
    ├── engine.toml
    ├── kcdx.log
    ├── kcdx-dev.log
    ├── address-library/
    │   └── database.toml
    └── plugins/
        └── <plugin-name>/...
```

Mirrors SKSE exactly: launcher exe creates the game process
suspended, injects the engine DLL via `CreateRemoteThread` +
`LoadLibrary`, resumes the game. Engine binary name and launcher
name are both unambiguous about authorship.

User-side change: launch via `kcdx-loader.exe` (or configure Steam
launch options to do it automatically) instead of Steam's "Play"
button. SKSE users have done this for over a decade; the documented
Steam launch-options snippet is one paragraph.

## Decisions along the way

The v0.2 design was worked out 2026-05-19. Alternatives considered
and rejected:

### "Just put the address library DB in the engine binary as constexpr"

Rejected because it forces a full kcdx release for any new ID. Loose
data files lets the community PR database updates that ship
independently of engine binaries.

### "Use a proxy DLL (`version.dll`) instead of a launcher"

Considered. Pros: Steam "Play" works unchanged, no user-side launch
config. Cons: masquerades as a system DLL (looks dodgy in a game
folder), can conflict with other ASI mods that proxy the same
target, breaks the "deliberate SKSE port" charter (SKSE doesn't
proxy — it launches).

Rejected in favor of the SKSE-style launcher. Honest naming +
ecosystem citizenship + Hard Rule #1 parity outweigh the one-time
Steam launch-options doc cost.

### "Name the proxy `kcdx.dll`"

Not viable. The proxy trick requires the DLL filename to match a
system DLL Windows resolves from the exe directory first. A DLL
called `kcdx.dll` next to the game exe just sits there — nothing
loads it.

This was the key realization that led to the launcher decision. If
we want a binary called `kcdx.dll` (and we do, for clarity), it
can't be a proxy — it has to be injected, which means a launcher.

### "Folder name: plugins/ or mods/"

`plugins/`. SKSE-ecosystem convention (SKSE, F4SE, all
CommonLibSSE-using projects). "Mods" in the KCD2 context already
means pak mods at `<game>/mods/` — using the same word for our
DLL-loading audience would directly collide with that mental model.

### "Engine binary inside `kcdx-engine/` subfolder during v0.1"

Considered moving `kcdx.asi` from `plugins/kcdx.asi` to
`kcdx-engine/kcdx.asi` during v0.1 so the engine binary lives apart
from the plugins it scans. Rejected because Ultimate ASI Loader
scans the folder it's in, and moving `kcdx.asi` would require also
configuring or shipping an ASI loader INI redirect — work that
disappears in v0.2 anyway. The binary stays in `plugins/`; only the
engine-owned **data** moves to `kcdx-engine/`.

## What this means for current work

Engine config, logs, and address library DB live in
`<game>/Bin/Win64MasterMasterSteamPGO/kcdx-engine/`. New
engine-owned files (e.g. address library database when Phase 7a
lands) go there too. Code accesses them via
`kcdx::paths::EngineDataDir()` / `EngineDataDirPath()`.

The v0.2 shift is a coordinated rename across:
- ASI extension drop (`kcdx.asi` → `kcdx.dll`)
- Engine binary moves from `plugins/` to root (with launcher)
- Plugin scan root changes from `plugins/` to `kcdx/plugins/`
- `kcdx-engine/` collapses into `kcdx/` (engine binary, config,
  logs, address library, and plugins all become children of one
  top-level folder named after us)

Best to do that all at once when shipping v0.2, not piecemeal during
v0.1.
