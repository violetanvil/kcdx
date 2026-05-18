#pragma once
#include <cstdint>
#include <string>

namespace kcdx::log {

// Hard size cap (bytes). Applies to every log stream: kcdx.log and every
// per-plugin <plugin-folder>/<folder>.log. When a stream reaches this size,
// further writes are silently dropped (the engine emits a single WARN line
// to kcdx.log naming the offending stream so a player reading the log can
// see which file went off the rails).
constexpr uint64_t kLogSizeCapBytes = 20ull * 1024 * 1024;  // 20 MB

// Initialize: opens log file at <module_dir>/kcdx.log, optionally allocates a console
// if the game was launched with -console.
void Init(const std::wstring& moduleDir);

// Engine-internal writers (write to kcdx.log).
void Info(const std::string& msg);
void Warn(const std::string& msg);
void Error(const std::string& msg);
void Debug(const std::string& msg);

// Plugin-side writers (write to <plugin-folder>/<folder>.log, prefix-tagged
// with the plugin's stable name). The path is determined by looking up
// `handle` in the loaded-plugins registry. If the handle is invalid, the
// write falls back to kcdx.log with a `[unknown plugin handle %u]` prefix.
//
// Each plugin's stream is opened lazily on first write and stays open for
// the rest of the session.
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
