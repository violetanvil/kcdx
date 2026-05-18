#pragma once
#include <string>

namespace kcdx::log {

// Initialize: opens log file at <module_dir>/kcdx.log, optionally allocates a console
// if the game was launched with -console.
void Init(const std::wstring& moduleDir);

void Info(const std::string& msg);
void Warn(const std::string& msg);
void Error(const std::string& msg);
void Debug(const std::string& msg);

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
