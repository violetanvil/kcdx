#pragma once

// Filesystem layout for kcdx's engine-owned files.
//
// v0.1 layout (rides on Ultimate ASI Loader):
//   <game>/Bin/Win64MasterMasterSteamPGO/
//   ├── dinput8.dll          (ASI loader)
//   ├── plugins/
//   │   ├── kcdx.asi         (us — the ASI module dir == PluginsDir)
//   │   └── <plugin>/...     (user plugin folders, walked by discovery)
//   └── kcdx-engine/         (sibling of plugins/, owned by us)
//       ├── engine.toml      (engine config — was kcdx-engine.toml in v0.0)
//       ├── kcdx.log         (engine log)
//       ├── kcdx-dev.log     (dev trace, only if dev_mode = true)
//       └── address-library/ (future: Phase 7a)
//           └── database.toml
//
// Why split: keeping engine-owned data out of plugins/ means plugin
// discovery walks only real plugin folders, and uninstall-by-deleting-
// kcdx-engine/-plus-plugins/kcdx.asi is unambiguous about ownership.
// Per-plugin logs still live inside each plugin's own folder
// (<plugins>/<plugin>/<plugin>.log) — they're plugin-owned, not
// engine-owned.
//
// v0.2 collapses this — see docs/loader-architecture.md.

#include <string>
#include <filesystem>

namespace kcdx::paths {

// Initialize once at startup from dllmain. Captures the ASI module
// directory as PluginsDir, derives EngineDataDir as the sibling
// kcdx-engine/. Creates the engine-data dir if it doesn't exist yet
// (idempotent — first-launch on a fresh install creates it).
//
// Safe to call before log::Init.
void Init();

// Directory containing kcdx.asi. Also the scan root for plugin
// discovery (subdirs of this with a kcdx.toml become plugins).
// Wide-char path with a trailing path separator.
const std::wstring& PluginsDir();

// Directory holding engine-owned data files (engine.toml, kcdx.log,
// kcdx-dev.log, address-library/database.toml). Sibling of PluginsDir.
// Wide-char path with a trailing path separator.
const std::wstring& EngineDataDir();

// std::filesystem::path views of the same two dirs, for code that
// wants path-arithmetic ergonomics. No trailing separator.
std::filesystem::path PluginsDirPath();
std::filesystem::path EngineDataDirPath();

}  // namespace kcdx::paths
