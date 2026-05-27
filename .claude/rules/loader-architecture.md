---
paths:
  - "src/main.cpp"
  - "src/loader.*"
  - "src/dll_main.cpp"
  - "src/watchdog.*"
  - "kcdx-watchdog/**"
  - "package-release.ps1"
  - "build.ps1"
---

# Loader / install layout

## Current (Phase 1+) — own launcher, no ASI loader

Authoritative artifact→destination mapping: `package-release.ps1`. Under `<game>/Bin/Win64MasterMasterSteamPGO/`:

- `kcdx.exe` (launcher) at the bin root, next to `KingdomCome.exe`. Injects the engine via `CreateRemoteThread` + `LoadLibrary` — no third-party loader.
- `kcdx-engine/kcdx.dll` (engine) + `kcdx-engine/kcdx-watchdog.exe` (crash-bundle sidecar).
- `kcdx-engine/builtin/<fix>/` — first-party engine-fix plugins, shipped in the release zip.
- `kcdx-engine/` also holds engine-owned data (`engine.toml`, `load_order.toml`, `logs/`).
- `kcdx-plugins/<name>/` — user-installed third-party plugins.
- `dinput8.dll` (Ultimate ASI Loader) is GONE; no `.asi` extension.
- Engine fixes + user plugins load through one discovery pipeline; engine fixes apply first.

## Rules

- New engine-owned files go under `kcdx-engine/`, never loose at the bin root or in `kcdx-plugins/`.
- Rejected alternatives: `version.dll`-style proxy (looks dodgy, conflicts), `dinput8.dll` ASI dependency (dropped), `mods/` folder (collides with KCD2's pak folder).

## Full doc

`docs/loader-architecture.md` for rationale + alternatives.
