#pragma once

// Filesystem layout for kcdx's engine-owned files.
//
// v0.2 layout:
//   <game>/Bin/Win64MasterMasterSteamPGO/
//   ├── KingdomCome.exe                      (vanilla)
//   ├── WHGame.dll                           (vanilla)
//   ├── kcdx.exe                             (LAUNCHER — user runs this)
//   ├── kcdx-engine/                         (everything kcdx-owned)
//   │   ├── kcdx.dll                         (us — the engine; injected by kcdx.exe)
//   │   ├── kcdx-watchdog.exe                (crash-bundle sidecar)
//   │   ├── engine.toml                      (engine config)
//   │   ├── load_order.toml                  (user load-order overrides)
//   │   ├── address-library/
//   │   │   └── database.csv
//   │   ├── logs/
//   │   │   ├── kcdx_<ts>.log                (engine log)
//   │   │   ├── kcdx-dev_<ts>.log            (dev trace)
//   │   │   ├── kcdx-launcher_<ts>.log       (launcher's own log)
//   │   │   ├── kcdx-watchdog_<ts>.log       (watchdog's own log)
//   │   │   └── crash/
//   │   │       └── crash_<ts>.zip
//   │   └── builtin/                         (first-party engine fixes)
//   │       └── <fix-name>/
//   │           ├── kcdx.toml
//   │           └── <fix>.dll
//   └── kcdx-plugins/                        (ONLY user/third-party plugins)
//       └── <plugin>/
//           ├── kcdx.toml
//           ├── plugin.lua / <plugin>.dll
//           └── logs/
//               └── <plugin>_<ts>.log
//
// Where <ts> is "YYYY-MM-DD_HH-MM-SS" of the session start. Old session
// files are pruned to kcdx::log::kLogRetainCount per stream on open.
//
// Key change from v0.1: only kcdx.exe sits at the game-bin root. The
// engine DLL, watchdog, configs, logs, builtin fixes all live one folder
// down under kcdx-engine/. The kcdx-plugins/ folder is exclusively for
// user-installed plugins — nothing kcdx-owned lives there. Both kcdx-*
// folder names use the "kcdx-" prefix to make ownership unambiguous at
// a glance and to avoid colliding with the existing KCD2-vanilla "mods/"
// concept (pak mods) and ASI-loader-era "plugins/" folder.
//
// kcdx.dll's perspective at runtime:
//   - Self is at <game-bin>/kcdx-engine/kcdx.dll.
//   - EngineDataDir = <game-bin>/kcdx-engine/ (same folder as self).
//   - PluginsDir = <game-bin>/kcdx-plugins/ (sibling of kcdx-engine/).

#include <string>
#include <filesystem>

namespace kcdx::paths {

// Initialize once at startup from dllmain. Captures kcdx.dll's directory
// as EngineDataDir, derives PluginsDir as the sibling
// <game-bin>/kcdx-plugins/. Creates engine-data + plugins + builtin
// dirs if they don't exist yet (idempotent — first-launch on a fresh
// install creates them).
//
// Safe to call before log::Init.
void Init();

// Directory containing user plugins (subdirs of this with a kcdx.toml
// become plugins). Wide-char path with a trailing path separator.
//
// Sibling of EngineDataDir. <game-bin>/kcdx-plugins/.
const std::wstring& PluginsDir();

// Directory holding engine-owned data files (engine.toml, load_order.toml,
// logs/, address-library/, builtin/). Also the directory containing
// kcdx.dll itself. Wide-char path with a trailing path separator.
//
// <game-bin>/kcdx-engine/.
const std::wstring& EngineDataDir();

// std::filesystem::path views of the same two dirs, for code that
// wants path-arithmetic ergonomics. No trailing separator.
std::filesystem::path PluginsDirPath();
std::filesystem::path EngineDataDirPath();

// Lossless UTF-8 std::string view of a path. Under C++20+, path::u8string()
// returns std::u8string (char8_t), which no longer implicitly converts to the
// std::string most call sites + the logger want; this bridges it (the bytes are
// identical UTF-8, only the element type differs). Use this anywhere a path
// needs to become a std::string / a const char* for logging or a UTF-8 API.
std::string ToUtf8(const std::filesystem::path& p);

// The KCD2 GAME ROOT — the install directory that holds system.cfg, the
// mods/ folder, and the Bin/ tree. Derived from EngineDataDir by climbing
// out of the bin layout: EngineDataDir is <game-root>/Bin/<flavour>/
// kcdx-engine/, so game-root = EngineDataDir / ".." / ".." / "..".
// No trailing separator.
//
//   <game-root>/                         <- this
//   └── Bin/
//       └── Win64MasterMasterSteamPGO/   (game-bin)
//           └── kcdx-engine/             (EngineDataDir)
//
// Used to locate game-root-relative files the engine reads but does not own
// (e.g. system.cfg's wh_sys_version, read by the unified version gate).
std::filesystem::path GameRootDirPath();

// The Steam library root that contains the KCD2 install — the directory
// holding both steamapps/ and libraryfolder.vdf. Detection is a filesystem
// walk from GameRootDirPath: KCD2 sits at <lib>/steamapps/common/
// KingdomComeDeliverance2/, so three parent_path() climbs land on <lib>;
// the presence of <lib>/libraryfolder.vdf confirms it is a Steam library
// root. Returns empty if KCD2 was not installed via Steam (Epic / GOG /
// standalone — those install layouts do not produce a libraryfolder.vdf
// at that ancestor). No registry I/O.
std::filesystem::path SteamLibraryRoot();

// True iff KCD2 is installed via Steam (SteamLibraryRoot() returns
// non-empty). The single signal Workshop callers need to decide whether
// Workshop content even applies.
bool IsSteamInstall();

// Steam Workshop content directory for KCD2's appid (1771300):
//   <Steam>/steamapps/workshop/content/1771300/
//
// The Steam library root is located via SteamLibraryRoot() — a filesystem
// walk that looks for libraryfolder.vdf in KCD2's library ancestor (NO
// registry I/O). Each immediate subdirectory of the returned path is a
// Steam Workshop item folder keyed by the Workshop file ID (e.g.
// 3728570527/), and contains a vanilla pak mod (mod.manifest + Data/*.pak).
//
// Returns an EMPTY string in any of these cases — none is an error, all are
// valid installs (a player on a non-Steam install — Epic, GOG, standalone —
// or a Steam install with no KCD2 Workshop subscriptions):
//   - KCD2 is not a Steam install (libraryfolder.vdf absent in the expected
//     ancestor; the caller logs the skip with that reason).
//   - The composed workshop content path does not exist on disk.
//
// A non-empty return is a directory_iterator-walkable path WITH a trailing
// path separator. The filesystem walk is cheap; no inter-launch caching.
std::wstring WorkshopContentDir();

}  // namespace kcdx::paths
