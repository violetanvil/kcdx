#include "log.h"

#include <windows.h>
#include <shellapi.h>

#include <cstdio>
#include <cstring>
#include <ctime>
#include <fstream>
#include <mutex>
#include <string>
#include <unordered_map>

#include "plugin_loader.h"  // for kcdx::plugins::g_plugins lookup

namespace kcdx::log {
namespace {

// One independent log stream. The engine has one of these for kcdx.log;
// each plugin gets one keyed on its handle. Streams are mutex-protected
// individually (each plugin's writes don't contend with engine writes,
// which matters for plugins that log frequently from a worker thread).
struct Stream {
    std::mutex     mutex;
    std::ofstream  file;
    std::wstring   path;       // for diagnostics + the cap-reached message
    std::string    streamName; // human-readable name for the cap warning
    uint64_t       bytesWritten = 0;
    bool           capWarned = false;
};

// The engine's own log stream (kcdx.log).
Stream g_engineStream;

// Per-plugin streams, keyed by PluginHandle. Created lazily on first write.
// Top-level mutex protects the map itself (keys added/looked up); each
// Stream has its own mutex for actual write contention.
std::mutex                          g_pluginStreamsMutex;
std::unordered_map<uint32_t, Stream*> g_pluginStreams;

// Console mirroring (set at engine startup if -console is on the cmdline).
HANDLE g_consoleOut = nullptr;
bool   g_consoleEnabled = false;

// Module directory (kept after Init so we can construct plugin log paths).
std::wstring g_moduleDir;

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

// Write to a Stream with size-cap enforcement.
//   - Returns true if the write was attempted, false if dropped due to cap.
//   - When the cap is first hit on a stream, marks `capWarned` and the
//     caller is expected to log a single notification to the engine
//     stream pointing the user at the offending stream.
//   - Caller holds the stream's mutex.
bool WriteToStream(Stream& s, const char* fullLine, size_t len) {
    if (!s.file.is_open()) return false;
    if (s.bytesWritten >= kLogSizeCapBytes) return false;
    s.file.write(fullLine, len);
    s.file.flush();
    s.bytesWritten += len;
    return true;
}

// Compose "[HH:MM:SS][LEVEL] msg\n" into `out`. Returns the byte length.
// `out` is sized for a kilobyte; messages longer than that get truncated.
int FormatLine(char* out, size_t outSize, const char* level, const std::string& msg) {
    std::string ts = FormatTimestamp();
    int n = snprintf(out, outSize, "[%s][%s] %s\n",
                     ts.c_str(), level, msg.c_str());
    if (n < 0) return 0;
    if (static_cast<size_t>(n) >= outSize) {
        // Truncated. snprintf returned the would-be length; clamp.
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

    bool dropped = false;
    {
        std::lock_guard<std::mutex> lock(g_engineStream.mutex);
        bool wrote = WriteToStream(g_engineStream, line, n);
        if (!wrote && g_engineStream.file.is_open() && !g_engineStream.capWarned) {
            g_engineStream.capWarned = true;
            dropped = true;
        }
        // Console gets the line whether or not the file took it — players
        // running with -console still see live output even past cap.
        MirrorToConsole(line, n);
    }

    if (dropped) {
        // Try to write the cap-reached notification itself. If the engine
        // stream is full, the notification gets dropped too — that's fine
        // because the console mirror will still show it to a -console user.
        std::string note = "kcdx.log reached " + std::to_string(kLogSizeCapBytes / (1024 * 1024))
                         + " MB cap; further writes to this file dropped for the session.";
        char capLine[256];
        int cn = FormatLine(capLine, sizeof(capLine), "WARN", note);
        std::lock_guard<std::mutex> lock(g_engineStream.mutex);
        if (cn > 0) MirrorToConsole(capLine, cn);
    }
}

// Per-plugin stream: open it on first use. Returns the Stream*, or null if
// the plugin folder can't be resolved (e.g. invalid handle, plugin already
// rejected). Caller does NOT hold any lock on entry.
Stream* GetOrOpenPluginStream(uint32_t handle) {
    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) return it->second;
    }

    // Need to create. Look up the plugin's folder name via the loader registry.
    const std::string* folder = nullptr;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle) {
            folder = &p.folderName;
            break;
        }
    }
    if (!folder || folder->empty()) return nullptr;

    // Build the log path:
    //   <plugins-dir>/<folderName>/<folderName>.log  (folder case)
    //   <plugins-dir>/<folderName>.log               (loose-DLL case — folder
    //     is actually the parent path, but for now we always treat
    //     folderName as a subdirectory under plugins/. Loose-DLL case is
    //     handled by DiscoverAndLoad using the .dll filename as folder.)
    // Build wide path for std::ofstream's open().
    std::wstring wide;
    wide.reserve(g_moduleDir.size() + 2 + folder->size() * 2);
    wide += g_moduleDir;
    for (char c : *folder) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    wide += L"\\";
    for (char c : *folder) wide.push_back(static_cast<wchar_t>(static_cast<unsigned char>(c)));
    wide += L".log";

    auto* s = new Stream();
    s->path = wide;
    s->streamName = *folder + ".log";
    s->file.open(wide, std::ios_base::out | std::ios_base::trunc);
    if (!s->file.is_open()) {
        // Fall back: log a one-time warning to the engine stream and don't
        // try again for this plugin (we cache a null sentinel... well, we
        // cache the broken Stream so we don't keep retrying open).
        WriteEngine("WARN",
            "plugin handle " + std::to_string(handle) +
            ": could not open log file at " + std::string(s->streamName) +
            "; further plugin log lines for this plugin will be dropped silently.");
    } else {
        WriteEngine("INFO",
            "plugin handle " + std::to_string(handle) +
            ": opened log at " + s->streamName);
    }

    // Insert into map. If a concurrent caller raced, prefer the existing entry.
    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) {
            // Lost the race; close+delete ours.
            if (s->file.is_open()) s->file.close();
            delete s;
            return it->second;
        }
        g_pluginStreams.emplace(handle, s);
    }
    return s;
}

// Per-plugin write. Looks up the plugin's name for the prefix.
void WritePlugin(uint32_t handle, const char* level, const std::string& msg) {
    // Resolve the plugin's stable name for the prefix (best-effort; if
    // resolution fails we still write a sensible fallback).
    const char* pluginName = nullptr;
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle && p.versionData) {
            pluginName = p.versionData->name;
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

    bool justHitCap = false;
    std::string capNote;
    {
        std::lock_guard<std::mutex> lock(s->mutex);
        bool wrote = WriteToStream(*s, line, n);
        if (!wrote && s->file.is_open() && !s->capWarned) {
            s->capWarned = true;
            justHitCap = true;
            capNote = "plugin log '" + s->streamName + "' reached "
                    + std::to_string(kLogSizeCapBytes / (1024 * 1024))
                    + " MB cap; further writes to that file dropped for the session.";
        }
    }
    if (justHitCap) {
        WriteEngine("WARN", capNote);
    }
}

}  // namespace

void Init(const std::wstring& moduleDir) {
    g_moduleDir = moduleDir;
    std::wstring logPath = moduleDir + L"kcdx.log";
    g_engineStream.path = logPath;
    g_engineStream.streamName = "kcdx.log";
    g_engineStream.file.open(logPath, std::ios_base::out | std::ios_base::trunc);

    if (HasConsoleArg()) {
        g_consoleEnabled = true;
        AllocConsole();
        g_consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTitleA("kcdx.asi");
        COORD bufSize{120, 9000};
        SetConsoleScreenBufferSize(g_consoleOut, bufSize);
    }
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
