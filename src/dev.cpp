// dev — see dev.h.
//
// Implementation choices:
//   - Single std::mutex around the FILE* + rotation state. dev mode is
//     opt-in and the cost of mutex contention is paid by authors who
//     accepted it; not worth an async queue in v0.1.
//   - Rotation happens lazily, checked on each Emit. When the current
//     log exceeds cap, we close, rename .N->.N+1 (oldest dropped if
//     exceeds max_files), open a fresh kcdx-dev.log, continue.
//   - The log file path is derived from kcdx::log's module directory.
//     We grab it via a callback from log::Init (set during kcdx's
//     dllmain init) rather than re-discovering it.

#include "dev.h"

#include <windows.h>

#include <atomic>
#include <chrono>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <mutex>
#include <string>

namespace kcdx::dev {

// ---------------------------------------------------------------------
// Module-private state
// ---------------------------------------------------------------------

std::atomic<bool> g_enabled{false};

namespace {

std::mutex          g_lock;
FILE*               g_fp        = nullptr;
size_t              g_cap_bytes = 50ull * 1024 * 1024;   // 50 MB default
int                 g_max_files = 20;
std::filesystem::path g_log_path;
std::filesystem::path g_log_dir;
size_t              g_bytes_written = 0;

// Resolve the kcdx-dev.log path. log::Init() in log.cpp captures the
// module directory at startup; we re-derive it the same way (via
// GetModuleHandle for kcdx.asi and PathRemoveFileSpec) so we don't
// need to plumb a callback.
std::filesystem::path ResolveLogPath() {
    HMODULE hMod = nullptr;
    GetModuleHandleExW(
        GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
        GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
        reinterpret_cast<LPCWSTR>(&ResolveLogPath),
        &hMod);
    wchar_t buf[MAX_PATH];
    DWORD n = GetModuleFileNameW(hMod, buf, MAX_PATH);
    if (n == 0 || n == MAX_PATH) return {};
    std::filesystem::path p(buf);
    return p.parent_path() / L"kcdx-dev.log";
}

// Cycle .log -> .log.1 -> .log.2 ... up to max_files. Drop the oldest.
// Caller holds g_lock. Caller has already closed g_fp.
void RotateLocked() {
    namespace fs = std::filesystem;
    if (g_log_path.empty()) return;

    // If unbounded retention, find the highest existing .N and keep
    // going. Otherwise drop the one at max_files first.
    if (g_max_files > 0) {
        fs::path drop = g_log_path;
        drop += L"." + std::to_wstring(g_max_files);
        std::error_code ec;
        fs::remove(drop, ec);
    }

    // Walk from N-1 down to 1: .N-1 -> .N, .N-2 -> .N-1, ... .1 -> .2
    // For unbounded (max_files == 0), pick a sane upper bound to walk.
    int max_walk = (g_max_files > 0) ? g_max_files : 9999;
    for (int i = max_walk - 1; i >= 1; --i) {
        fs::path from = g_log_path;
        from += L"." + std::to_wstring(i);
        if (!fs::exists(from)) continue;
        fs::path to = g_log_path;
        to += L"." + std::to_wstring(i + 1);
        std::error_code ec;
        fs::rename(from, to, ec);
    }

    // Finally .log -> .log.1
    if (fs::exists(g_log_path)) {
        fs::path to = g_log_path;
        to += L".1";
        std::error_code ec;
        fs::rename(g_log_path, to, ec);
    }
}

// Opens kcdx-dev.log fresh (truncating). Caller holds g_lock.
// Returns whether we have a writable g_fp afterward.
bool OpenLocked() {
    if (g_log_path.empty()) g_log_path = ResolveLogPath();
    if (g_log_path.empty()) return false;
    g_log_dir = g_log_path.parent_path();
    if (g_fp) { fclose(g_fp); g_fp = nullptr; }
    g_fp = _wfopen(g_log_path.c_str(), L"ab");
    if (!g_fp) return false;
    g_bytes_written = 0;
    // Probe current file size so we don't immediately rotate after
    // reopening an existing file from a prior session.
    if (fseek(g_fp, 0, SEEK_END) == 0) {
        long sz = ftell(g_fp);
        if (sz > 0) g_bytes_written = (size_t)sz;
    }
    return true;
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
    // Check rotation BEFORE writing so the new line lands in the
    // freshly-opened file rather than pushing the old one over cap.
    if (g_cap_bytes > 0 && g_bytes_written + len + 1 > g_cap_bytes) {
        fclose(g_fp);
        g_fp = nullptr;
        RotateLocked();
        if (!OpenLocked()) return;
    }
    fwrite(line, 1, len, g_fp);
    fputc('\n', g_fp);
    fflush(g_fp);
    g_bytes_written += len + 1;
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
            g_bytes_written += strlen(banner) + 1;
        }
    }
}

void SetCapBytes(size_t cap_bytes) {
    g_cap_bytes = cap_bytes;
}

void SetMaxFiles(int max_files) {
    g_max_files = max_files;
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
