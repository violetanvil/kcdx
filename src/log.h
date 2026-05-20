#pragma once
#include <cstdint>
#include <string>

namespace kcdx::log {

// Maximum number of session log files to retain per stream. On Init(),
// each stream's logs/ folder is pruned to the newest kLogRetainCount
// files matching that stream's filename prefix; older files are deleted.
// "Per stream" means kcdx_*.log and kcdx-dev_*.log are counted
// independently — the engine's logs/ folder may hold up to
// kLogRetainCount of each.
constexpr int kLogRetainCount = 20;

// Initialize: opens a fresh per-session log file at
// <kcdx-engine>/logs/kcdx_<YYYY-MM-DD_HH-MM-SS>.log (resolved via
// paths::EngineDataDir, which must be initialized first), prunes older
// kcdx_*.log files beyond kLogRetainCount, optionally allocates a
// console if the game was launched with -console.
void Init();

// Eagerly create a plugin's logs/ folder and open its per-session log
// file. Called by the plugin loader once per discovered DLL-bearing
// plugin, after its entry is appended to g_plugins. TOML-only plugins
// are skipped by the caller (they have no code path that logs).
// Idempotent — if already opened, this is a no-op. Failure to open is
// logged to the engine log; subsequent writes for that plugin are
// silently dropped.
void OpenPluginStream(uint32_t handle);

// Engine-internal writers (write to the current session's kcdx_<ts>.log).
void Info(const std::string& msg);
void Warn(const std::string& msg);
void Error(const std::string& msg);
void Debug(const std::string& msg);

// Plugin-side writers. Each plugin gets its own per-session log at
// <plugins>/<folder>/logs/<folder>_<YYYY-MM-DD_HH-MM-SS>.log, lazily
// opened on first write and pruned to kLogRetainCount on open. Lines
// are prefix-tagged with the plugin's stable name. If the handle is
// invalid, the write falls back to the engine's kcdx log with a
// `[unknown plugin handle %u]` prefix.
void PluginInfo (uint32_t handle, const std::string& msg);
void PluginWarn (uint32_t handle, const std::string& msg);
void PluginError(uint32_t handle, const std::string& msg);
void PluginDebug(uint32_t handle, const std::string& msg);

// printf-style helpers
template <typename... Args>
void InfoF(const char* fmt, Args... args) {
    char buf[1024];
    snprintf(buf, sizeof(buf), fmt, args...);
    Info(buf);
}
template <typename... Args>
void WarnF(const char* fmt, Args... args) {
    char buf[1024];
    snprintf(buf, sizeof(buf), fmt, args...);
    Warn(buf);
}
template <typename... Args>
void ErrorF(const char* fmt, Args... args) {
    char buf[1024];
    snprintf(buf, sizeof(buf), fmt, args...);
    Error(buf);
}
template <typename... Args>
void DebugF(const char* fmt, Args... args) {
    char buf[1024];
    snprintf(buf, sizeof(buf), fmt, args...);
    Debug(buf);
}

}  // namespace kcdx::log
