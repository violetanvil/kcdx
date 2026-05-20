// kcdx unified logging — see log.h.
//
// One router, three destinations:
//
//   1. Engine log    <kcdx-engine>/logs/kcdx_<ts>.log     — always open
//   2. Dev log       <kcdx-engine>/logs/kcdx-dev_<ts>.log — opened on SetDevMode(true)
//   3. Per-plugin    <plugins>/<folder>/logs/<folder>_<ts>.log — per-plugin handle
//
// Each Emit* call composes the line once and dispatches to whichever
// destinations the (severity, dev-mode-state, category-filter,
// is-plugin-form) tuple selects. See the routing matrix in log.h.

#include "log.h"

#include <windows.h>
#include <shellapi.h>

#include <algorithm>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include "paths.h"
#include "plugin_loader.h"

namespace kcdx::log {

namespace fs = std::filesystem;

// Defined non-inline because IsDevModeEnabled() in the header takes
// its address via extern. Atomic so SetDevMode is safe from any thread.
std::atomic<bool> g_devMode{false};

namespace {

// -----------------------------------------------------------------------------
// Stream state
// -----------------------------------------------------------------------------

// One independent stream (FILE*-backed for the same reasons documented
// in the pre-unification log.cpp: std::ofstream silently dropped writes
// on this platform/runtime; dev.cpp's FILE* path was proven good).
struct Stream {
    std::mutex   mutex;
    FILE*        fp = nullptr;
    std::wstring path;
    std::string  streamName;  // for diagnostics
};

Stream g_engineStream;
Stream g_devStream;

std::mutex                          g_pluginStreamsMutex;
std::unordered_map<uint32_t, Stream*> g_pluginStreams;

// Console mirroring (set at engine startup if -console is on the cmdline).
HANDLE g_consoleOut = nullptr;
bool   g_consoleEnabled = false;

// Session timestamp ("YYYY-MM-DD_HH-MM-SS") captured at Init() and
// reused for every file opened during this session so they all sort
// together lexicographically.
std::string g_sessionStamp;

// Main thread id captured at Init(). Used by the dev-log formatter to
// decide whether to emit `tid=N` on dev-log lines (only when not the
// main thread, since "main" is implicit).
DWORD g_mainThreadId = 0;

// Category allow-list. Empty = every category passes. Set once during
// engine.toml load; readers don't need a mutex after that.
std::vector<std::string> g_categoryFilter;

// -----------------------------------------------------------------------------
// Helpers
// -----------------------------------------------------------------------------

bool HasConsoleArg() {
    LPWSTR cmdLine = GetCommandLineW();
    int argc = 0;
    LPWSTR* argv = CommandLineToArgvW(cmdLine, &argc);
    bool found = false;
    if (argv) {
        for (int i = 1; i < argc; ++i) {
            if (_wcsicmp(argv[i], L"-console") == 0) { found = true; break; }
        }
        LocalFree(argv);
    }
    return found;
}

const char* LevelName(Level lv) {
    switch (lv) {
        case Level::Trace: return "TRACE";
        case Level::Debug: return "DEBUG";
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERROR";
    }
    return "?";
}

// "HH:MM:SS.mmm" — all destinations use millisecond precision.
size_t FormatTimestampHMSms(char* out, size_t cap) {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto tt  = clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    std::tm tm{};
    localtime_s(&tm, &tt);
    return (size_t)snprintf(out, cap, "%02d:%02d:%02d.%03lld",
                            tm.tm_hour, tm.tm_min, tm.tm_sec,
                            (long long)ms);
}

std::string FormatSessionStamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm);
    return std::string(ts);
}

// Delete oldest files matching "<prefix>*<suffix>" beyond `keep` count.
// Errors swallowed silently (retention failure is non-fatal).
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

// Look up a plugin's stable name by handle. Returns nullptr on
// unknown handle. Caller does not hold any lock.
const char* PluginName(uint32_t handle) {
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle && !p.manifest.name.empty()) {
            return p.manifest.name.c_str();
        }
    }
    return nullptr;
}

// Write a complete line to a Stream. Caller holds the stream's mutex.
// Appends '\n' itself so callers don't have to.
void WriteLineLocked(Stream& s, const char* line, size_t len) {
    if (!s.fp) return;
    fwrite(line, 1, len, s.fp);
    fputc('\n', s.fp);
    fflush(s.fp);
}

void MirrorToConsole(const char* line, size_t len) {
    if (g_consoleEnabled && g_consoleOut) {
        DWORD written = 0;
        WriteConsoleA(g_consoleOut, line, (DWORD)len, &written, nullptr);
        WriteConsoleA(g_consoleOut, "\n", 1, &written, nullptr);
    }
}

// -----------------------------------------------------------------------------
// Per-plugin stream creation (lazy on first write, eager via OpenPluginStream)
// -----------------------------------------------------------------------------

Stream* GetOrOpenPluginStream(uint32_t handle) {
    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) return it->second;
    }

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

    fs::path logsDir = fs::path(folderPath) / L"logs";
    std::error_code ec;
    fs::create_directories(logsDir, ec);

    std::string prefix = folderName + "_";
    std::string suffix = ".log";
    PruneOldSessionFiles(logsDir, prefix, suffix, kLogRetainCount);

    std::string filename = prefix + g_sessionStamp + suffix;
    fs::path logPath = logsDir / filename;

    auto* s = new Stream();
    s->path = logPath.wstring();
    s->streamName = filename;
    s->fp = _wfopen(logPath.c_str(), L"wb");

    // Report (success or failure) to the engine log. EmitEngine is
    // safe to call here since g_engineStream is opened first by Init().
    if (!s->fp) {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "could not open log file %s; lines for this plugin will be dropped",
                 s->streamName.c_str());
        EmitEngine(Level::Warn, "LOGGING", msg);
    } else {
        char msg[256];
        snprintf(msg, sizeof(msg),
                 "plugin handle %u: opened log at %s/logs/%s",
                 handle, folderName.c_str(), s->streamName.c_str());
        EmitEngine(Level::Info, "LOGGING", msg);
    }

    {
        std::lock_guard<std::mutex> lock(g_pluginStreamsMutex);
        auto it = g_pluginStreams.find(handle);
        if (it != g_pluginStreams.end()) {
            // Race-loser: close + delete ours.
            if (s->fp) fclose(s->fp);
            delete s;
            return it->second;
        }
        g_pluginStreams.emplace(handle, s);
    }
    return s;
}

// -----------------------------------------------------------------------------
// Line composition
// -----------------------------------------------------------------------------
//
// We compose two slightly different line shapes:
//
//   Engine/plugin destinations:  [HH:MM:SS][LEVEL][SOURCE][CATEGORY] body
//   Dev destination:             [HH:MM:SS.mmm][LEVEL][SOURCE][CATEGORY] body  [tid=N]
//
// The body part is shared (printf'd message or KV-formatted action).
// Each destination format wraps the shared body with its own prefix.

// Compose the body of a structured ("action key=val key=val") line.
size_t FormatKVBody(char* buf, size_t cap, const char* action,
                    std::initializer_list<KV> kvs) {
    size_t n = (size_t)snprintf(buf, cap, "%s", action ? action : "");

    auto append = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        int w = snprintf(buf + n, cap - n, fmt, args...);
        if (w > 0) n += (size_t)w;
    };

    for (const KV& kv : kvs) {
        if (n >= cap) break;
        switch (kv.kind) {
            case KV::STR:
                append(" %s=\"%.*s\"", kv.k, (int)kv.svn, kv.sv ? kv.sv : "");
                break;
            case KV::BARE_STR:
                append(" %s=%s", kv.k, kv.sv ? kv.sv : "");
                break;
            case KV::INT:    append(" %s=%lld",  kv.k, kv.i); break;
            case KV::UINT:   append(" %s=%llu",  kv.k, kv.u); break;
            case KV::HEX:    append(" %s=0x%llX", kv.k, (unsigned long long)kv.hex); break;
            case KV::BOOL:   append(" %s=%s",    kv.k, kv.b ? "true" : "false"); break;
            case KV::DOUBLE: append(" %s=%.17g", kv.k, kv.d); break;
            case KV::BYTES:
                append(" %s=", kv.k);
                for (size_t i = 0; i < kv.bn && n + 3 < cap; ++i) {
                    int w = snprintf(buf + n, cap - n,
                                     i ? " %02X" : "%02X", kv.bp[i]);
                    if (w > 0) n += (size_t)w;
                }
                break;
        }
    }
    if (n >= cap) n = cap - 1;
    buf[n] = 0;
    return n;
}

// Compose the engine/plugin-log shape. Returns line length.
// Uses millisecond timestamps — modders correlating frame-time events
// across kcdx.log and their plugin's log need the precision.
int FormatEngineLine(char* out, size_t outSize, Level level,
                     const char* source, const char* category,
                     const char* body) {
    char ts[24];
    FormatTimestampHMSms(ts, sizeof(ts));
    int n = snprintf(out, outSize, "[%s][%s][%s][%s] %s",
                     ts, LevelName(level),
                     source ? source : "?",
                     category ? category : "?",
                     body ? body : "");
    if (n < 0) return 0;
    if ((size_t)n >= outSize) return (int)outSize - 1;
    return n;
}

// Compose the dev-log shape (millis + optional tid suffix).
int FormatDevLine(char* out, size_t outSize, Level level,
                  const char* source, const char* category,
                  const char* body) {
    char ts[24];
    FormatTimestampHMSms(ts, sizeof(ts));
    int n = snprintf(out, outSize, "[%s][%s][%s][%s] %s",
                     ts, LevelName(level),
                     source ? source : "?",
                     category ? category : "?",
                     body ? body : "");
    if (n < 0) return 0;
    // Append tid=N if not the main thread.
    DWORD tid = GetCurrentThreadId();
    if (tid != g_mainThreadId && (size_t)n < outSize) {
        int w = snprintf(out + n, outSize - n, " tid=%lu", (unsigned long)tid);
        if (w > 0) n += w;
    }
    if ((size_t)n >= outSize) return (int)outSize - 1;
    return n;
}

// -----------------------------------------------------------------------------
// Dispatch — the single routing function. Documented in docs/logging.md.
// -----------------------------------------------------------------------------
//
// Routing rules (one independent decision per destination):
//
//   kcdx.log     yes iff level >= INFO
//   kcdx-dev.log yes iff dev_mode AND (filter empty OR category matches)
//   plugin file  yes iff plugin-attributed AND (
//                    level >= WARN                        // always
//                    OR level >= pluginLogLevelFloor      // honors author's floor
//                    OR dev_mode                          // dev mode bypasses floor
//                )
//
// `pluginLogLevelFloor` is the plugin's manifest log_level. The
// special value kNoFloor (UINT32_MAX) means "engine-side call, no
// plugin attribution, plugin-file destination not in play."

constexpr uint32_t kNoFloor = UINT32_MAX;

void Dispatch(Level level, const char* source, const char* category,
              const char* body, Stream* pluginStream,
              uint32_t pluginLogLevelFloor) {
    uint32_t lvlNum = static_cast<uint32_t>(level);
    bool isEngineLogWorthy = (level >= Level::Info);
    bool devOn             = g_devMode.load(std::memory_order_relaxed);

    bool catPasses = false;
    if (devOn) {
        if (g_categoryFilter.empty()) {
            catPasses = true;
        } else if (category) {
            for (const auto& c : g_categoryFilter) {
                if (c == category) { catPasses = true; break; }
            }
        }
    }

    // Plugin-file gate. The per-plugin file is the modder's bug-report
    // channel. WARN and ERROR ALWAYS reach it (the floor never gates
    // problems). Lower severities pass when the author's floor allows
    // OR dev mode is on (which bypasses the floor for them).
    bool toPlugin = false;
    if (pluginStream != nullptr) {
        if (level >= Level::Warn) {
            toPlugin = true;
        } else if (devOn) {
            toPlugin = true;
        } else if (pluginLogLevelFloor != kNoFloor &&
                   lvlNum >= pluginLogLevelFloor) {
            toPlugin = true;
        }
    }

    bool toEngine = isEngineLogWorthy;
    bool toDev    = devOn && catPasses;

    if (toEngine) {
        char line[KCDX_LOG_FORMAT_BUF_SIZE + 256];
        int n = FormatEngineLine(line, sizeof(line), level, source, category, body);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(g_engineStream.mutex);
            WriteLineLocked(g_engineStream, line, (size_t)n);
            MirrorToConsole(line, (size_t)n);
        }
    }
    if (toDev) {
        char line[KCDX_LOG_FORMAT_BUF_SIZE + 256];
        int n = FormatDevLine(line, sizeof(line), level, source, category, body);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(g_devStream.mutex);
            WriteLineLocked(g_devStream, line, (size_t)n);
        }
    }
    if (toPlugin) {
        char line[KCDX_LOG_FORMAT_BUF_SIZE + 256];
        // Per-plugin file uses the engine-log shape (no tid noise).
        int n = FormatEngineLine(line, sizeof(line), level, source, category, body);
        if (n > 0) {
            std::lock_guard<std::mutex> lock(pluginStream->mutex);
            WriteLineLocked(*pluginStream, line, (size_t)n);
        }
    }
}

}  // namespace

// -----------------------------------------------------------------------------
// Public Emit entry points
// -----------------------------------------------------------------------------

// Look up the plugin's manifest log_level floor by handle. Returns
// kNoFloor on unknown handle (treated as "no floor" since we don't
// know what the author intended — the WARN/ERROR-always rule still
// applies; INFO/DEBUG/TRACE will be gated by dev mode only).
namespace {
uint32_t PluginLogLevelFloor(uint32_t handle) {
    for (const auto& p : plugins::g_plugins) {
        if (p.handle == handle) return p.manifest.logLevel;
    }
    return kNoFloor;
}
}  // namespace

void EmitEngine(Level level, const char* category, const char* message) {
    Dispatch(level, "engine", category, message ? message : "",
             nullptr, kNoFloor);
}

void EmitEngineKV(Level level, const char* category, const char* action,
                  std::initializer_list<KV> kvs) {
    char body[KCDX_LOG_FORMAT_BUF_SIZE];
    FormatKVBody(body, sizeof(body), action, kvs);
    Dispatch(level, "engine", category, body, nullptr, kNoFloor);
}

void EmitPlugin(Level level, uint32_t handle, const char* category,
                const char* message) {
    const char* name = PluginName(handle);
    char fallback[64];
    if (!name) {
        snprintf(fallback, sizeof(fallback), "(unknown handle %u)", handle);
        name = fallback;
    }
    Stream* ps = GetOrOpenPluginStream(handle);
    uint32_t floor = PluginLogLevelFloor(handle);
    Dispatch(level, name, category, message ? message : "", ps, floor);
}

void EmitPluginKV(Level level, uint32_t handle, const char* category,
                  const char* action,
                  std::initializer_list<KV> kvs) {
    const char* name = PluginName(handle);
    char fallback[64];
    if (!name) {
        snprintf(fallback, sizeof(fallback), "(unknown handle %u)", handle);
        name = fallback;
    }
    char body[KCDX_LOG_FORMAT_BUF_SIZE];
    FormatKVBody(body, sizeof(body), action, kvs);
    Stream* ps = GetOrOpenPluginStream(handle);
    uint32_t floor = PluginLogLevelFloor(handle);
    Dispatch(level, name, category, body, ps, floor);
}

namespace detail {

void FormatTo(char* buf, size_t bufsize, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    int n = vsnprintf(buf, bufsize, fmt, args);
    va_end(args);
    if (n < 0) {
        if (bufsize > 0) buf[0] = 0;
    } else if ((size_t)n >= bufsize) {
        buf[bufsize - 1] = 0;
    }
}

}  // namespace detail

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

void Init() {
    g_sessionStamp   = FormatSessionStamp();
    g_mainThreadId   = GetCurrentThreadId();

    fs::path logsDir = kcdx::paths::EngineDataDirPath() / L"logs";
    std::error_code ec;
    fs::create_directories(logsDir, ec);

    // Engine log: prune old + open fresh.
    PruneOldSessionFiles(logsDir, "kcdx_", ".log", kLogRetainCount);
    std::string engineName = "kcdx_" + g_sessionStamp + ".log";
    fs::path enginePath = logsDir / engineName;
    g_engineStream.path = enginePath.wstring();
    g_engineStream.streamName = engineName;
    g_engineStream.fp = _wfopen(enginePath.c_str(), L"wb");

    if (HasConsoleArg()) {
        g_consoleEnabled = true;
        AllocConsole();
        g_consoleOut = GetStdHandle(STD_OUTPUT_HANDLE);
        SetConsoleTitleA("kcdx.asi");
        COORD bufSize{120, 9000};
        SetConsoleScreenBufferSize(g_consoleOut, bufSize);
    }
}

void OpenPluginStream(uint32_t handle) {
    GetOrOpenPluginStream(handle);
}

const std::string& SessionStamp() {
    return g_sessionStamp;
}

void SetDevMode(bool on) {
    g_devMode.store(on, std::memory_order_relaxed);
    if (!on) return;

    // Open the dev log on first enable. Idempotent.
    {
        std::lock_guard<std::mutex> lock(g_devStream.mutex);
        if (g_devStream.fp) return;

        fs::path logsDir = kcdx::paths::EngineDataDirPath() / L"logs";
        std::error_code ec;
        fs::create_directories(logsDir, ec);

        PruneOldSessionFiles(logsDir, "kcdx-dev_", ".log", kLogRetainCount);
        std::string devName = "kcdx-dev_" + g_sessionStamp + ".log";
        fs::path devPath = logsDir / devName;
        g_devStream.path = devPath.wstring();
        g_devStream.streamName = devName;
        g_devStream.fp = _wfopen(devPath.c_str(), L"wb");

        if (g_devStream.fp) {
            // Banner so modders can find session boundaries.
            const char* banner =
                "================================================================\n"
                "kcdx dev mode session start\n"
                "================================================================";
            fwrite(banner, 1, strlen(banner), g_devStream.fp);
            fputc('\n', g_devStream.fp);
            fflush(g_devStream.fp);
        }
    }

    EmitEngine(Level::Info, "LOGGING", "dev mode ON");
}

void SetCategoryFilter(const std::vector<std::string>& categories) {
    g_categoryFilter = categories;
}

bool IsCategoryEnabled(const char* category) {
    if (!g_devMode.load(std::memory_order_relaxed)) return false;
    if (g_categoryFilter.empty()) return true;
    if (!category) return false;
    for (const auto& c : g_categoryFilter) {
        if (c == category) return true;
    }
    return false;
}

// -----------------------------------------------------------------------------
// KV ctors (moved verbatim from dev.cpp)
// -----------------------------------------------------------------------------

KV::KV(const char* key, const char* val)
    : k(key), kind(STR), sv(val), svn(val ? strlen(val) : 0) {}
KV::KV(const char* key, const std::string& val)
    : k(key), kind(STR), sv(val.data()), svn(val.size()) {}
KV::KV(const char* key, std::string_view val)
    : k(key), kind(STR), sv(val.data()), svn(val.size()) {}
KV::KV(const char* key, int val)
    : k(key), kind(INT), i(val) {}
KV::KV(const char* key, long val)
    : k(key), kind(INT), i(val) {}
KV::KV(const char* key, long long val)
    : k(key), kind(INT), i(val) {}
KV::KV(const char* key, unsigned int val)
    : k(key), kind(UINT), u(val) {}
KV::KV(const char* key, unsigned long val)
    : k(key), kind(UINT), u(val) {}
KV::KV(const char* key, unsigned long long val)
    : k(key), kind(UINT), u(val) {}
KV::KV(const char* key, bool val)
    : k(key), kind(BOOL), b(val) {}
KV::KV(const char* key, double val)
    : k(key), kind(DOUBLE), d(val) {}
KV::KV(const char* key, float val)
    : k(key), kind(DOUBLE), d(val) {}
KV::KV(const char* key, const void* val)
    : k(key), kind(HEX), hex(reinterpret_cast<uintptr_t>(val)) {}
KV::KV(const char* key, void* val)
    : k(key), kind(HEX), hex(reinterpret_cast<uintptr_t>(val)) {}

KV KV::Bytes(const char* key, const uint8_t* data, size_t size) {
    KV out(key, (const char*)nullptr);
    out.kind = BYTES;
    out.bp = data;
    out.bn = size;
    return out;
}

KV KV::BareStr(const char* key, const char* val) {
    KV out(key, val);
    out.kind = BARE_STR;
    return out;
}

}  // namespace kcdx::log
