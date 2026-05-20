// dev — see dev.h.
//
// Implementation choices:
//   - Single std::mutex around the FILE*. dev mode is opt-in and the
//     cost of mutex contention is paid by authors who accepted it;
//     not worth an async queue in v0.1.
//   - Each session writes to its own file:
//     <kcdx-engine>/logs/kcdx-dev_<YYYY-MM-DD_HH-MM-SS>.log. On open,
//     older kcdx-dev_*.log files beyond kLogRetainCount are pruned.
//   - The log file path is derived from kcdx::paths::EngineDataDir.

#include "dev.h"

#include <windows.h>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <filesystem>
#include <mutex>
#include <string>
#include <vector>

#include "log.h"      // kLogRetainCount
#include "paths.h"

namespace kcdx::dev {

// ---------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------

std::atomic<bool> g_enabled{false};

namespace {

namespace fs = std::filesystem;

std::mutex          g_lock;
FILE*               g_fp = nullptr;
fs::path            g_log_path;

// Category filter. Empty = all categories pass. Non-empty = only listed
// categories pass IsCategoryEnabled. Set once during config load; read
// from many threads thereafter, no further mutation — so we don't need
// a mutex around access after init.
std::vector<std::string> g_category_filter;

// "YYYY-MM-DD_HH-MM-SS" — filesystem-safe, sortable, human-readable.
std::string FormatSessionStamp() {
    std::time_t now = std::time(nullptr);
    std::tm tm{};
    localtime_s(&tm, &now);
    char ts[32];
    std::strftime(ts, sizeof(ts), "%Y-%m-%d_%H-%M-%S", &tm);
    return std::string(ts);
}

// Delete oldest kcdx-dev_*.log files in `dir` beyond `keep` entries.
// Errors are swallowed silently.
void PruneOldDevLogs(const fs::path& dir, int keep) {
    std::error_code ec;
    if (!fs::exists(dir, ec)) return;

    const std::string prefix = "kcdx-dev_";
    const std::string suffix = ".log";
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

// Opens a fresh per-session kcdx-dev log (truncating). Caller holds g_lock.
// Returns whether we have a writable g_fp afterward.
bool OpenLocked() {
    if (g_fp) return true;

    fs::path logsDir = kcdx::paths::EngineDataDirPath() / L"logs";
    std::error_code ec;
    fs::create_directories(logsDir, ec);

    PruneOldDevLogs(logsDir, kcdx::log::kLogRetainCount);

    std::string filename = "kcdx-dev_" + FormatSessionStamp() + ".log";
    g_log_path = logsDir / filename;
    g_fp = _wfopen(g_log_path.c_str(), L"wb");
    return g_fp != nullptr;
}

// Format current timestamp + thread id into `buf`, return length written.
size_t FormatHeader(char* buf, size_t cap) {
    using clock = std::chrono::system_clock;
    auto now = clock::now();
    auto tt  = clock::to_time_t(now);
    auto ms  = std::chrono::duration_cast<std::chrono::milliseconds>(
                   now.time_since_epoch()).count() % 1000;
    std::tm tm_buf{};
    localtime_s(&tm_buf, &tt);
    return (size_t)snprintf(buf, cap, "[%02d:%02d:%02d.%03lld T:%lu] ",
                            tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec,
                            (long long)ms,
                            (unsigned long)GetCurrentThreadId());
}

// Append one KV's `name=val` form (with a leading space) into the
// accumulator. Returns number of chars actually written.
size_t FormatKV(char* buf, size_t cap, const KV& kv) {
    if (cap == 0) return 0;
    size_t n = 0;
    auto write = [&](const char* fmt, auto... args) {
        if (n >= cap) return;
        int w = snprintf(buf + n, cap - n, fmt, args...);
        if (w > 0) n += (size_t)w;
    };
    switch (kv.kind) {
        case KV::STR:
            write(" %s=\"%.*s\"", kv.k, (int)kv.svn, kv.sv ? kv.sv : "");
            break;
        case KV::BARE_STR:
            write(" %s=%s", kv.k, kv.sv ? kv.sv : "");
            break;
        case KV::INT:    write(" %s=%lld",  kv.k, kv.i); break;
        case KV::UINT:   write(" %s=%llu",  kv.k, kv.u); break;
        case KV::HEX:    write(" %s=0x%llX", kv.k, (unsigned long long)kv.hex); break;
        case KV::BOOL:   write(" %s=%s",    kv.k, kv.b ? "true" : "false"); break;
        case KV::DOUBLE: write(" %s=%.17g", kv.k, kv.d); break;
        case KV::BYTES:
            n += (size_t)snprintf(buf + n, cap - n, " %s=", kv.k);
            for (size_t i = 0; i < kv.bn && n + 3 < cap; ++i) {
                n += (size_t)snprintf(buf + n, cap - n,
                                      i ? " %02X" : "%02X", kv.bp[i]);
            }
            break;
    }
    return n;
}

void WriteLineLocked(const char* line, size_t len) {
    if (!g_fp) {
        if (!OpenLocked()) return;
    }
    fwrite(line, 1, len, g_fp);
    fputc('\n', g_fp);
    fflush(g_fp);
}

}  // namespace

// ---------------------------------------------------------------------
// Lifecycle accessors
// ---------------------------------------------------------------------

void SetEnabled(bool on) {
    g_enabled.store(on, std::memory_order_relaxed);
    if (on) {
        std::lock_guard<std::mutex> lk(g_lock);
        if (!g_fp) OpenLocked();
        // First-ever line: banner so authors can find session boundaries.
        const char* banner =
            "================================================================\n"
            "kcdx dev mode session start\n"
            "================================================================";
        if (g_fp) {
            fwrite(banner, 1, strlen(banner), g_fp);
            fputc('\n', g_fp);
            fflush(g_fp);
        }
    }
}

void SetCategoryFilter(const std::vector<std::string>& categories) {
    g_category_filter = categories;
}

bool IsCategoryEnabled(const char* category) {
    if (!g_enabled.load(std::memory_order_relaxed)) return false;
    if (g_category_filter.empty()) return true;
    if (!category) return false;
    for (const auto& c : g_category_filter) {
        if (c == category) return true;
    }
    return false;
}

// ---------------------------------------------------------------------
// KV ctors
// ---------------------------------------------------------------------

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

// ---------------------------------------------------------------------
// Emit
// ---------------------------------------------------------------------

void Emit(const char* category, const char* action,
          std::initializer_list<KV> kvs) {
    // Format off the lock when we can; the file lock only protects
    // the write itself.
    char line[1024];
    size_t n = FormatHeader(line, sizeof(line));
    n += (size_t)snprintf(line + n, sizeof(line) - n, "%s.%s",
                          category, action);
    for (const KV& kv : kvs) {
        if (n >= sizeof(line) - 1) break;
        n += FormatKV(line + n, sizeof(line) - n, kv);
    }
    if (n >= sizeof(line)) n = sizeof(line) - 1;
    line[n] = 0;

    std::lock_guard<std::mutex> lk(g_lock);
    WriteLineLocked(line, n);
}

}  // namespace kcdx::dev
