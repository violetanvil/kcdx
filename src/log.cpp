#include "log.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "paths.h"           // for paths::PluginsDir / EngineDataDir
#include "plugin_loader.h"   // for kcdx::plugins::g_plugins lookup

namespace kcdx::log {
namespace {

namespace fs = std::filesystem;

// One independent log stream. The engine has one of these for the
// session kcdx_<ts>.log; each plugin gets one keyed on its handle.
// Streams are mutex-protected individually (each plugin's writes don't
// contend with engine writes, which matters for plugins that log
// frequently from a worker thread).
//
// Backed by FILE* (via _wfopen) rather than std::ofstream — dev.cpp
// uses the same pattern and we know it works against this game. The
// std::ofstream path silently dropped writes here despite is_open()
// returning true; rather than chase the cause, copy what works.
struct Stream {
    std::mutex   mutex;
    FILE*        fp = nullptr;
    std::wstring path;       // for diagnostics
    std::string  streamName; // human-readable name, e.g. "kcdx_2026-05-20_14-30-15.log"
};

// The engine's own log stream.
Stream g_engineStream;

// Per-plugin streams, keyed by PluginHandle. Created lazily on first write.
// Top-level mutex protects the map itself (keys added/looked up); each
// Stream has its own mutex for actual write contention.
std::mutex                          g_pluginStreamsMutex;
std::unordered_map<uint32_t, Stream*> g_pluginStreams;

// Console mirroring (set at engine startup if -console is on the cmdline).
HANDLE g_consoleOut = nullptr;
bool   g_consoleEnabled = false;

// Session timestamp string ("YYYY-MM-DD_HH-MM-SS"), captured once at
// Init() and reused for both the engine log filename and every plugin
// log opened during this session. Capturing it once means all of this
// session's files sort together lexicographically.
std::string g_sessionStamp;

bool HasConsoleArg() {
    LPWSTR cmdLine = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    bool found = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"-console") == 0) {
                found = true;
                break;
            }
        }
        LocalFree(argv);
    }
    return found;
}

std::string FormatTimestamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%H:%M:%S", &tm);
    return std::string(ts);
}

// "YYYY-MM-DD_HH-MM-SS" — filesystem-safe (no colons), lexicographically
// sortable, human-readable. Used in the filename per session.
std::string FormatSessionStamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm);
    return std::string(ts);
}

// Prune `dir` to the newest `keep` files whose filename matches
// "<prefix>*<suffix>" (i.e., starts with prefix and ends with suffix).
// Older files are deleted. Errors are swallowed silently — failure to
// prune is not fatal (worst case: old logs accumulate). Caller does
// NOT hold any lock.
void PruneOldSessionFiles(const fs::path& dir,
                          const std::string& prefix,
                          const std::string& suffix,
                          int keep) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    std::vector<fs::path> matches;
    for (auto& entry : fs::directory_iterator(dir, ec)) {
        if (ec) break;
        if (!entry.is_regular_file(ec)) continue;
        std::string name = entry.path().filename().string();
        if (name.size() < prefix.size() + suffix.size()) continue;
        if (name.compare(0, prefix.size(), prefix) != 0) continue;
        if (name.compare(name.size() - suffix.size(),
                         suffix.size(), suffix) != 0) continue;
        matches.push_back(entry.path());
    }

    if ((int)matches.size() <= keep) return;

    // Sort by filename ascending — because the timestamp format is
    // lexicographically chronological, this gives us oldest-first.
    std::sort(matches.begin(), matches.end(),
              [](const fs::path& a, const fs::path& b) {
                  return a.filename().wstring() < b.filename().wstring();
              });

    int to_delete = (int)matches.size() - keep;
    for (int i = 0; i < to_delete; ++i) {
        std::error_code rm_ec;
        fs::remove(matches[i], rm_ec);
    }
}

// Write to a Stream. Caller holds the stream's mutex.
void WriteToStream(Stream& s, const char* fullLine, size_t len) {
    if (!s.fp) return;
    fwrite(fullLine, 1, len, s.fp);
    fflush(s.fp);
}

// Compose "[HH:MM:SS][LEVEL] msg\n" into `out`. Returns the byte length.
int FormatLine(char* out, size_t outSize, const char* level, const std::string& msg) {
    std::string ts = FormatTimestamp();
    int n = snprintf(out, outSize, "[%s][%s] %s\n",
                     ts.c_str(), level, msg.c_str());
    if (n < 0) return 0;
    if (static_cast<size_t>(n) >= outSize) {
        return static_cast<int>(outSize - 1);
    }
    return n;
}

// Mirror to console if enabled. Console writes share the engine stream's
// mutex (the caller's lock); no separate console mutex needed.
void MirrorToConsole(const char* line, int len) {
    if (g_consoleEnabled && g_consoleOut) {
        DWORD written = 0;
        WriteConsoleA(g_consoleOut, line, static_cast<DWORD>(len), &written, nullptr);
    }
}

// Engine-stream write. Mirrors to console.
void WriteEngine(const char* level, const std::string& msg) {
    char line[1280];
    int n = FormatLine(line, sizeof(line), level, msg);
    if (n <= 0) return;

    std::lock_guard<std::mutex> lock(g_engineStream.mutex);
    WriteToStream(g_engineStream, line, n);
    MirrorToConsole(line, n);
}

// Per-plugin stream: open it on first use. Returns the Stream*, or null
// if the plugin folder can't be resolved.
Stream* GetOrOpenPluginStream(uint32_t handle) {
    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) return it->second;
    }

    // Need to create. Look up the plugin's absolute folder path + leaf
    // name via the loader registry. folderPath is authoritative for the
    // on-disk location (plugins can live nested, e.g.
    // plugins/test-suite/cap-01-patch/); folderName is just the leaf
    // for filename construction.
    std::wstring folderPath;
    std::string  folderName;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle) {
            folderPath = p.folderPath;
            folderName = p.folderName;
            break;
        }
    }
    if (folderPath.empty() || folderName.empty()) return nullptr;

    // Build the per-plugin logs/ dir:
    //   <plugin-folder>/logs/<folderName>_<sessionStamp>.log
    fs::path logsDir = fs::path(folderPath) / L"logs";
    std::error_code ec;
    fs::create_directories(logsDir, ec);

    // Prune old session files for this plugin before opening the new one.
    std::string prefix = folderName + "_";
    std::string suffix = ".log";
    PruneOldSessionFiles(logsDir, prefix, suffix, kLogRetainCount);

    std::string filename = prefix + g_sessionStamp + suffix;
    fs::path logPath = logsDir / filename;

    auto* s = new Stream();
    s->path = logPath.wstring();
    s->streamName = filename;
    s->fp = _wfopen(logPath.c_str(), L"wb");
    if (!s->fp) {
        WriteEngine("WARN",
            "plugin handle " + std::to_string(handle) +
            ": could not open log file " + s->streamName +
            "; further plugin log lines for this plugin will be dropped silently.");
    } else {
        WriteEngine("INFO",
            "plugin handle " + std::to_string(handle) +
            ": opened log at " + folderName + "/logs/" + s->streamName);
    }

    // Insert into map. If a concurrent caller raced, prefer the existing entry.
    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) {
            if (s->fp) fclose(s->fp);
            delete s;
            return it->second;
        }
        g_pluginStreams.emplace(handle, s);
    }
    return s;
}

// Per-plugin write. Looks up the plugin's name for the prefix.
void WritePlugin(uint32_t handle, const char* level, const std::string& msg) {
    const char* pluginName = nullptr;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle && !p.manifest.name.empty()) {
            pluginName = p.manifest.name.c_str();
            break;
        }
    }

    char line[1280];
    int n;
    if (pluginName) {
        std::string prefixed = std::string("[") + pluginName + "] " + msg;
        n = FormatLine(line, sizeof(line), level, prefixed);
    } else {
        std::string fallback = "[unknown plugin handle " + std::to_string(handle) + "] " + msg;
        n = FormatLine(line, sizeof(line), level, fallback);
    }
    if (n <= 0) return;

    Stream* s = GetOrOpenPluginStream(handle);
    if (!s) {
        // Plugin can't be resolved — fall back to engine log.
        std::lock_guard<std::mutex> lock(g_engineStream.mutex);
        WriteToStream(g_engineStream, line, n);
        MirrorToConsole(line, n);
        return;
    }

    std::lock_guard<std::mutex> lock(s->mutex);
    WriteToStream(*s, line, n);
}

}  // namespace

void Init() {
    g_sessionStamp = FormatSessionStamp();

    fs::path logsDir = kcdx::paths::EngineDataDirPath() / L"logs";
    std::error_code ec;
    fs::create_directories(logsDir, ec);

    // Prune older kcdx_*.log before opening this session's file so
    // the new file isn't accidentally counted in the keep limit.
    PruneOldSessionFiles(logsDir, "kcdx_", ".log", kLogRetainCount);

    std::string filename = "kcdx_" + g_sessionStamp + ".log";
    fs::path logPath = logsDir / filename;
    g_engineStream.path = logPath.wstring();
    g_engineStream.streamName = filename;
    g_engineStream.fp = _wfopen(logPath.c_str(), L"wb");

    if (HasConsoleArg()) {
        g_consoleEnabled = true;
        AllocConsole();
        g_consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTitleA("kcdx.asi");
        COORD bufSize{120, 9000};
        SetConsoleScreenBufferSize(g_consoleOut, bufSize);
    }
}

// Public wrapper: eagerly open a plugin's log stream so its logs/ folder
// and per-session file exist even if the plugin never calls Log itself.
// Plugin loader calls this once per plugin after the plugin is registered
// in g_plugins.
void OpenPluginStream(uint32_t handle) {
    GetOrOpenPluginStream(handle);
}

void Info(const std::string& msg)  { WriteEngine("INFO",  msg); }
void Warn(const std::string& msg)  { WriteEngine("WARN",  msg); }
void Error(const std::string& msg) { WriteEngine("ERROR", msg); }
void Debug(const std::string& msg) { WriteEngine("DEBUG", msg); }

void PluginInfo (uint32_t handle, const std::string& msg) { WritePlugin(handle, "INFO",  msg); }
void PluginWarn (uint32_t handle, const std::string& msg) { WritePlugin(handle, "WARN",  msg); }
void PluginError(uint32_t handle, const std::string& msg) { WritePlugin(handle, "ERROR", msg); }
void PluginDebug(uint32_t handle, const std::string& msg) { WritePlugin(handle, "DEBUG", msg); }

}  // namespace kcdx::log
