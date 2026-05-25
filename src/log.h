#pragma once

// kcdx unified logging.
//
// One API, three destinations, six severity levels, category-tagged.
//
//   LOG_INFO ("DISCOVERY", "Discovered plugin '%s' from %s", name, path);
//   LOG_WARN ("MANIFEST",  "Plugin '%s' rejected: %s", name, reason);
//   LOG_ERROR("GUARD",     "FAULTED site=%s code=%s", site, codeName);
//   LOG_DEBUG("MESSAGING", "broadcast type=%u listeners=%zu", type, n);
//
//   // Structured (key=value) form for events where field discipline matters:
//   LOG_INFO_KV("DISCOVERY", "accept",
//       KV("path", folder),
//       KV("toml", tomlPath));
//
//   // Plugin-attributed lines mirror to the per-plugin file:
//   LOG_PLUGIN_INFO(handle, "LIFECYCLE", "kcdxPlugin_Load called");
//
// Line shape on disk (every destination uses the same format):
//
//   [HH:MM:SS][LEVEL][SOURCE][CATEGORY] body
//
//   - SOURCE is "engine" for engine-side lines, or the plugin's stable
//     name (e.g. "violetanvil.hello-plugin") for plugin-attributed lines.
//   - CATEGORY is a free-form short identifier ("DISCOVERY", "GUARD", ...).
//     There is intentionally no enum — adding a category is a string
//     change at the call site. Categories ARE grepped, so keep them
//     short and stable.
//   - body is either a printf'd message or `action key=val key=val`
//     for the _KV variants.
//
// Dev-log lines additionally include `.mmm` (milliseconds) on the
// timestamp and a `tid=<N>` KV pair when emitted from a thread other
// than the main thread. The dev log destination receives the same
// underlying content, formatted slightly differently for grepping.
//
// Routing matrix (g_devMode is a runtime atomic bool):
//
//   Severity | Engine log | Dev log              | Per-plugin log
//   ---------|------------|----------------------|---------------
//   ERROR    | always     | dev mode + category  | (plugin variants)
//   WARN     | always     | dev mode + category  | (plugin variants)
//   INFO     | always     | dev mode + category  | (plugin variants)
//   DEBUG    | no         | dev mode + category  | (plugin variants)
//   TRACE    | no         | dev mode + category  | (plugin variants)
//
// "category" filter: if the user sets dev_categories = [...] in
// engine.toml, only listed categories pass IsCategoryEnabled(). When
// the list is empty, every category passes.
//
// Plugin variants additionally write to the plugin's own log file.
// INFO/WARN/ERROR plugin lines hit engine log + plugin log + (if dev)
// dev log. DEBUG/TRACE plugin lines hit plugin log + (if dev) dev log
// only — they don't pollute the engine log with verbose-grade traffic.
//
// Hot-path cost when dev mode is off:
//   - LOG_INFO/WARN/ERROR: always-on writes happen unconditionally.
//   - LOG_DEBUG/TRACE: one relaxed atomic load + branch-predicted skip.
//     The KV/format work is gated behind the IsCategoryEnabled() check
//     so off-state pays roughly nothing.

#include <atomic>
#include <cstdint>
#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace kcdx::log {

// -----------------------------------------------------------------------------
// Retention
// -----------------------------------------------------------------------------

// Maximum number of session log files to retain per stream. On Init(),
// each stream's logs/ folder is pruned to the newest kLogRetainCount
// files matching that stream's filename prefix.
constexpr int kLogRetainCount = 20;

// -----------------------------------------------------------------------------
// Severity
// -----------------------------------------------------------------------------

enum class Level : uint8_t {
    Trace = 0,  // dev-only, finest grain
    Debug = 1,  // dev-only
    Info  = 2,  // always-on
    Warn  = 3,  // always-on
    Error = 4,  // always-on
};

// -----------------------------------------------------------------------------
// Lifecycle
// -----------------------------------------------------------------------------

// Open the engine log + (if dev mode enabled by config) the dev log.
// Prunes old session files. Must be called once at startup before any
// LOG_* macro is used.
void Init();

// Returns the "YYYY-MM-DD_HH-MM-SS" stamp captured at Init() — the
// suffix used in every log file's name for this session. Used by
// the watchdog to find this session's log files at bundle time.
const std::string& SessionStamp();

// Eagerly open a plugin's per-session log. Called by the plugin loader
// after a DLL-bearing plugin is registered. Idempotent.
void OpenPluginStream(uint32_t handle);

// Turn dev mode on. After this, DEBUG/TRACE lines route to the dev log
// (subject to category filter), and the dev log file is opened.
void SetDevMode(bool on);

// Set the category allow-list. Empty list = every category passes.
// Non-empty = ONLY listed categories pass IsCategoryEnabled.
void SetCategoryFilter(const std::vector<std::string>& categories);

// True iff dev mode is on AND (no filter set OR category matches).
// Hot-path: when dev mode is off, this is one atomic load.
bool IsCategoryEnabled(const char* category);

// True iff dev mode is on (independent of category).
inline bool IsDevModeEnabled() {
    extern std::atomic<bool> g_devMode;
    return g_devMode.load(std::memory_order_relaxed);
}

// True iff the calling thread is the **engine-init thread** — the
// thread that ran `log::Init` (typically kcdx.exe's
// `CreateRemoteThread` injector thread under the Phase 1+ launcher
// model). Cheap: one TLS read + one int compare, no lock.
//
// Body reads the TU-local `g_engineInitThreadId` captured by Init().
//
// For "is this the game's main-Lua-VM thread?" use
// `IsGameMainThread()` instead. The two are DIFFERENT threads in
// kcdx — `log::Init` runs from DLL_PROCESS_ATTACH on the injector
// thread; the game main thread is captured later (after first
// update-tick) via `hook_chain::SetLuaState` → `SetGameMainThread`.
//
// Conflating them was the design bug behind Phase 3 sub-1 step 5-pre's
// invalid probe (every dispatch read `is_main=0` falsely because it
// was comparing against the injector thread). The split — variable
// + accessor per consumer — is the fix.
//
// See .claude/rules/lua-callback-threading.md for the two-variable
// model.
bool IsMainThread();

// Record the **game main thread** id — the thread that runs the game's
// per-frame update loop and dispatches Lua callbacks. One-liner:
// `g_gameMainThreadId = GetCurrentThreadId();`.
//
// Called once from `hook_chain::SetLuaState`'s first non-null L branch
// (which executes inside the first-update-tick hook in `src/hooks.cpp`
// — that callsite IS the game main thread by construction). Safe to
// call repeatedly: the first-update-tick hook fires every tick and
// every call lands the same thread id, so only the FIRST call is
// load-bearing; subsequent calls are idempotent re-writes of the same
// value.
//
// Before this runs, `g_gameMainThreadId == 0` (sentinel) and
// `IsGameMainThread()` returns false on every real thread.
void SetGameMainThread();

// True iff the calling thread is the **game main thread** — the thread
// that first called `hook_chain::SetLuaState` with a non-null L.
//
// Returns false BEFORE `SetGameMainThread()` has been called
// (`g_gameMainThreadId == 0` is the "not yet captured" sentinel; no
// real Windows thread has tid 0, so this is a clean negative).
//
// Used by the hook dispatchers in `src/hook_chain.cpp` to classify
// off-thread fires (`is_main = 0/1` tag in the HOOK_THREAD probe; in
// step 5-main, the gate for off_thread = marshal/skip/error routing).
//
// Cheap: one TLS read + one int compare, no lock.
bool IsGameMainThread();

// -----------------------------------------------------------------------------
// KV — name + typed value, emitted as `name=val` in the body.
//
// Same shape that lived in kcdx::dev::KV before unification; the
// dev::KV alias still works as a shim.
// -----------------------------------------------------------------------------

struct KV {
    const char* k;

    enum Kind {
        STR,       // const char* / std::string / std::string_view (quoted)
        BARE_STR,  // const char* (NOT quoted; for identifier-like values)
        INT,       // signed long long, formatted decimal
        UINT,      // unsigned long long, formatted decimal
        HEX,       // uintptr_t, formatted as 0x...
        BOOL,      // true/false
        DOUBLE,    // double, formatted %.17g
        BYTES,     // const uint8_t* + size, formatted as `XX XX XX`
    };

    Kind kind = STR;

    const char*    sv  = nullptr;
    size_t         svn = 0;
    long long      i   = 0;
    unsigned long long u = 0;
    uintptr_t      hex = 0;
    bool           b   = false;
    double         d   = 0.0;
    const uint8_t* bp  = nullptr;
    size_t         bn  = 0;

    KV(const char* key, const char* val);
    KV(const char* key, const std::string& val);
    KV(const char* key, std::string_view val);
    KV(const char* key, int val);
    KV(const char* key, long val);
    KV(const char* key, long long val);
    KV(const char* key, unsigned int val);
    KV(const char* key, unsigned long val);
    KV(const char* key, unsigned long long val);
    KV(const char* key, bool val);
    KV(const char* key, double val);
    KV(const char* key, float val);
    KV(const char* key, const void* val);
    KV(const char* key, void* val);

    static KV Bytes(const char* key, const uint8_t* data, size_t size);
    static KV BareStr(const char* key, const char* val);
};

// -----------------------------------------------------------------------------
// Emitters — the underlying entry points the macros expand into.
// -----------------------------------------------------------------------------

// Engine-side, free-form message (printf-formatted by the macro before
// it reaches here).
void EmitEngine(Level level, const char* category, const char* message);

// Engine-side, structured action + KVs.
void EmitEngineKV(Level level, const char* category, const char* action,
                  std::initializer_list<KV> kvs);

// Plugin-attributed. `handle` looks up the plugin's stable name; an
// unknown handle falls back to "(unknown handle N)" in the SOURCE
// field.
void EmitPlugin(Level level, uint32_t handle, const char* category,
                const char* message);

void EmitPluginKV(Level level, uint32_t handle, const char* category,
                  const char* action,
                  std::initializer_list<KV> kvs);

// printf-format helper used by the macros. Bounded at 1KiB output.
// Marked __attribute__((format(printf,...))) on GCC; MSVC has no
// equivalent attribute but the macros pass through to snprintf so
// /analyze still flags mismatches.
namespace detail {
void FormatTo(char* buf, size_t bufsize, const char* fmt, ...);
}

}  // namespace kcdx::log

// -----------------------------------------------------------------------------
// Macros
// -----------------------------------------------------------------------------
//
// Note: the _KV variants build an initializer-list of KV. The local
// scope at the macro expansion site must have `KV` resolveable. Two
// ways: write `kcdx::log::KV(...)` at the call site, or add
// `using KV = ::kcdx::log::KV;` once at the top of the TU.
//
// printf-style:
//   LOG_INFO("DISCOVERY", "Discovered plugin '%s' from %s", name, path);
//
// Structured key=value:
//   LOG_INFO_KV("DISCOVERY", "accept",
//       KV("path", folder),
//       KV("toml", tomlPath));
//
// Plugin-attributed:
//   LOG_PLUGIN_INFO(handle, "LIFECYCLE", "kcdxPlugin_Load called");
//   LOG_PLUGIN_INFO_KV(handle, "LIFECYCLE", "loaded", KV("ver", v));

#define KCDX_LOG_FORMAT_BUF_SIZE 1024

#define LOG_AT(level, category, ...)                                  \
    do {                                                              \
        char _kcdx_logbuf[KCDX_LOG_FORMAT_BUF_SIZE];                  \
        ::kcdx::log::detail::FormatTo(_kcdx_logbuf,                   \
                                      sizeof(_kcdx_logbuf),           \
                                      __VA_ARGS__);                   \
        ::kcdx::log::EmitEngine((level), (category), _kcdx_logbuf);   \
    } while (0)

#define LOG_INFO( category, ...) LOG_AT(::kcdx::log::Level::Info,  (category), __VA_ARGS__)
#define LOG_WARN( category, ...) LOG_AT(::kcdx::log::Level::Warn,  (category), __VA_ARGS__)
#define LOG_ERROR(category, ...) LOG_AT(::kcdx::log::Level::Error, (category), __VA_ARGS__)

// DEBUG/TRACE short-circuit cheaply when dev mode is off.
#define LOG_DEBUG(category, ...)                                       \
    do {                                                               \
        if (::kcdx::log::IsCategoryEnabled(category))                  \
            LOG_AT(::kcdx::log::Level::Debug, (category), __VA_ARGS__);\
    } while (0)

#define LOG_TRACE(category, ...)                                       \
    do {                                                               \
        if (::kcdx::log::IsCategoryEnabled(category))                  \
            LOG_AT(::kcdx::log::Level::Trace, (category), __VA_ARGS__);\
    } while (0)

// _KV variants — structured `action key=val ...` form.
#define LOG_INFO_KV( category, action, ...) \
    ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Info,  (category), (action), { __VA_ARGS__ })
#define LOG_WARN_KV( category, action, ...) \
    ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Warn,  (category), (action), { __VA_ARGS__ })
#define LOG_ERROR_KV(category, action, ...) \
    ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Error, (category), (action), { __VA_ARGS__ })

#define LOG_DEBUG_KV(category, action, ...)                                                  \
    do {                                                                                     \
        if (::kcdx::log::IsCategoryEnabled(category))                                        \
            ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Debug, (category), (action),       \
                                      { __VA_ARGS__ });                                      \
    } while (0)

#define LOG_TRACE_KV(category, action, ...)                                                  \
    do {                                                                                     \
        if (::kcdx::log::IsCategoryEnabled(category))                                        \
            ::kcdx::log::EmitEngineKV(::kcdx::log::Level::Trace, (category), (action),       \
                                      { __VA_ARGS__ });                                      \
    } while (0)

// Plugin-attributed forms.
#define LOG_PLUGIN_AT(level, handle, category, ...)                            \
    do {                                                                       \
        char _kcdx_logbuf[KCDX_LOG_FORMAT_BUF_SIZE];                           \
        ::kcdx::log::detail::FormatTo(_kcdx_logbuf,                            \
                                      sizeof(_kcdx_logbuf),                    \
                                      __VA_ARGS__);                            \
        ::kcdx::log::EmitPlugin((level), (handle), (category), _kcdx_logbuf);  \
    } while (0)

#define LOG_PLUGIN_INFO( handle, category, ...) LOG_PLUGIN_AT(::kcdx::log::Level::Info,  (handle), (category), __VA_ARGS__)
#define LOG_PLUGIN_WARN( handle, category, ...) LOG_PLUGIN_AT(::kcdx::log::Level::Warn,  (handle), (category), __VA_ARGS__)
#define LOG_PLUGIN_ERROR(handle, category, ...) LOG_PLUGIN_AT(::kcdx::log::Level::Error, (handle), (category), __VA_ARGS__)
#define LOG_PLUGIN_DEBUG(handle, category, ...) LOG_PLUGIN_AT(::kcdx::log::Level::Debug, (handle), (category), __VA_ARGS__)
#define LOG_PLUGIN_TRACE(handle, category, ...) LOG_PLUGIN_AT(::kcdx::log::Level::Trace, (handle), (category), __VA_ARGS__)

#define LOG_PLUGIN_INFO_KV( handle, category, action, ...) \
    ::kcdx::log::EmitPluginKV(::kcdx::log::Level::Info,  (handle), (category), (action), { __VA_ARGS__ })
#define LOG_PLUGIN_WARN_KV( handle, category, action, ...) \
    ::kcdx::log::EmitPluginKV(::kcdx::log::Level::Warn,  (handle), (category), (action), { __VA_ARGS__ })
#define LOG_PLUGIN_ERROR_KV(handle, category, action, ...) \
    ::kcdx::log::EmitPluginKV(::kcdx::log::Level::Error, (handle), (category), (action), { __VA_ARGS__ })

// -----------------------------------------------------------------------------
// Shims for the old kcdx::log:: free-function API.
//
// Every existing call to log::Info("...") / log::WarnF("...", x) etc.
// continues to compile and run, tagged with category "LEGACY" in the
// log. Migrate call sites opportunistically; once nothing references
// these, delete the shims.
// -----------------------------------------------------------------------------

namespace kcdx::log {

inline void Info (const std::string& msg) { EmitEngine(Level::Info,  "LEGACY", msg.c_str()); }
inline void Warn (const std::string& msg) { EmitEngine(Level::Warn,  "LEGACY", msg.c_str()); }
inline void Error(const std::string& msg) { EmitEngine(Level::Error, "LEGACY", msg.c_str()); }
inline void Debug(const std::string& msg) { EmitEngine(Level::Debug, "LEGACY", msg.c_str()); }

inline void PluginInfo (uint32_t h, const std::string& msg) { EmitPlugin(Level::Info,  h, "LEGACY", msg.c_str()); }
inline void PluginWarn (uint32_t h, const std::string& msg) { EmitPlugin(Level::Warn,  h, "LEGACY", msg.c_str()); }
inline void PluginError(uint32_t h, const std::string& msg) { EmitPlugin(Level::Error, h, "LEGACY", msg.c_str()); }
inline void PluginDebug(uint32_t h, const std::string& msg) { EmitPlugin(Level::Debug, h, "LEGACY", msg.c_str()); }

template <typename... Args>
inline void InfoF(const char* fmt, Args... args) {
    char buf[KCDX_LOG_FORMAT_BUF_SIZE];
    snprintf(buf, sizeof(buf), fmt, args...);
    EmitEngine(Level::Info, "LEGACY", buf);
}
template <typename... Args>
inline void WarnF(const char* fmt, Args... args) {
    char buf[KCDX_LOG_FORMAT_BUF_SIZE];
    snprintf(buf, sizeof(buf), fmt, args...);
    EmitEngine(Level::Warn, "LEGACY", buf);
}
template <typename... Args>
inline void ErrorF(const char* fmt, Args... args) {
    char buf[KCDX_LOG_FORMAT_BUF_SIZE];
    snprintf(buf, sizeof(buf), fmt, args...);
    EmitEngine(Level::Error, "LEGACY", buf);
}
template <typename... Args>
inline void DebugF(const char* fmt, Args... args) {
    char buf[KCDX_LOG_FORMAT_BUF_SIZE];
    snprintf(buf, sizeof(buf), fmt, args...);
    EmitEngine(Level::Debug, "LEGACY", buf);
}

}  // namespace kcdx::log
