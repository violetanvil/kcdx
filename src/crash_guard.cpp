// crash_guard — see crash_guard.h.
//
// Compiled with /EHa via per-file CMake setting so __try/__except is
// allowed in the same TU that includes C++ headers.

#include "crash_guard.h"

#include <windows.h>

#include <atomic>
#include <cstdio>

#include "log.h"
#include "plugin_loader.h"  // for plugins::g_plugins (name -> handle lookup)

namespace kcdx::guard {

// Bring KV into local scope so LOG_*_KV macros expand cleanly.
using KV = ::kcdx::log::KV;

namespace {

// Per-thread breadcrumb of the most-recently-entered guarded site. The
// process-level filter reads this to attribute an otherwise-unattributed
// crash to a guarded entry point that didn't catch the exception in
// time (e.g. a fault on a thread the guard couldn't unwind out of).
thread_local const char* tls_lastSite       = nullptr;
thread_local const char* tls_lastPlugin     = nullptr;

// Saved previous filter — captured at install time so we can chain.
// LPTOP_LEVEL_EXCEPTION_FILTER, raw because we need to call it from
// inside the filter without C++ unwinding semantics.
LPTOP_LEVEL_EXCEPTION_FILTER g_priorFilter = nullptr;
std::atomic<bool>            g_filterInstalled{false};

// Format the SEH exception code into a short identifier the log
// reader can grep for. Most common codes get a name; unknowns fall
// back to "0x...".
const char* ExceptionCodeName(DWORD code) {
    switch (code) {
        case EXCEPTION_ACCESS_VIOLATION:         return "ACCESS_VIOLATION";
        case EXCEPTION_DATATYPE_MISALIGNMENT:    return "DATATYPE_MISALIGNMENT";
        case EXCEPTION_BREAKPOINT:               return "BREAKPOINT";
        case EXCEPTION_ILLEGAL_INSTRUCTION:      return "ILLEGAL_INSTRUCTION";
        case EXCEPTION_INT_DIVIDE_BY_ZERO:       return "INT_DIVIDE_BY_ZERO";
        case EXCEPTION_PRIV_INSTRUCTION:         return "PRIV_INSTRUCTION";
        case EXCEPTION_STACK_OVERFLOW:           return "STACK_OVERFLOW";
        case EXCEPTION_IN_PAGE_ERROR:            return "IN_PAGE_ERROR";
        case 0xE06D7363:                         return "CPP_EXCEPTION";
        default:                                 return nullptr;
    }
}

// Look up the module name + base for an arbitrary RIP. Used by both
// the in-guard fault log and the process-level filter to attribute a
// crash to a specific DLL.
bool ModuleForAddress(void* addr, char* outName, size_t outNameLen,
                      void** outBase) {
    HMODULE hMod = nullptr;
    if (!GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
            GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(addr),
            &hMod)) {
        return false;
    }
    wchar_t wide[MAX_PATH * 2];
    DWORD n = GetModuleFileNameW(hMod, wide, _countof(wide));
    if (n == 0) return false;
    // Keep just the leaf filename to avoid 200-char log lines for game
    // path prefixes.
    int leaf = static_cast<int>(n);
    for (int i = static_cast<int>(n) - 1; i >= 0; --i) {
        if (wide[i] == L'\\' || wide[i] == L'/') { leaf = i + 1; break; }
    }
    int wrote = WideCharToMultiByte(CP_UTF8, 0, wide + leaf, -1,
                                    outName, static_cast<int>(outNameLen),
                                    nullptr, nullptr);
    if (wrote <= 0) return false;
    if (outBase) *outBase = reinterpret_cast<void*>(hMod);
    return true;
}

// Resolve a plugin's stable name to its handle, so a GUARD line about
// that plugin can also land in the plugin's own log file. Returns
// kcdxInvalidPluginHandle (== UINT32_MAX) on unknown name.
uint32_t HandleFromName(const char* name) {
    if (!name) return UINT32_MAX;
    for (const auto& p : kcdx::plugins::g_plugins) {
        if (p.manifest.name == name) return p.handle;
    }
    return UINT32_MAX;
}

// Build the FAULTED log line. Always lands in the engine log
// (engine view of the fault). When pluginName resolves to a loaded
// plugin handle, the same line ALSO lands in that plugin's own
// log file — the consumer's bug-report channel.
void LogFault(const char* site, const char* pluginName,
              const EXCEPTION_RECORD* er) {
    void* rip = er ? er->ExceptionAddress : nullptr;
    DWORD code = er ? er->ExceptionCode : 0;
    const char* codeName = ExceptionCodeName(code);

    char moduleName[128] = "?";
    void* moduleBase = nullptr;
    uint64_t rva = 0;
    if (rip && ModuleForAddress(rip, moduleName, sizeof(moduleName),
                                &moduleBase)) {
        rva = static_cast<uint64_t>(reinterpret_cast<uint8_t*>(rip) -
                                    reinterpret_cast<uint8_t*>(moduleBase));
    }

    // Engine log + dev log (if dev mode + GUARD category enabled).
    if (codeName) {
        LOG_ERROR_KV("GUARD", "FAULTED",
            KV::BareStr("site",   site ? site : "?"),
            KV::BareStr("plugin", pluginName ? pluginName : "(none)"),
            KV::BareStr("code",   codeName),
            KV("rip",    reinterpret_cast<void*>(rip)),
            KV::BareStr("module", moduleName),
            KV("module_rva", rva),
            KV("thread", (unsigned long)GetCurrentThreadId()));
    } else {
        LOG_ERROR_KV("GUARD", "FAULTED",
            KV::BareStr("site",   site ? site : "?"),
            KV::BareStr("plugin", pluginName ? pluginName : "(none)"),
            KV("code",   (unsigned long)code),
            KV("rip",    reinterpret_cast<void*>(rip)),
            KV::BareStr("module", moduleName),
            KV("module_rva", rva),
            KV("thread", (unsigned long)GetCurrentThreadId()));
    }

    // Also surface the fault in the offending plugin's own log file
    // (the consumer's bug-report channel). The line is identical
    // structure; the plugin's name becomes the SOURCE field.
    uint32_t handle = HandleFromName(pluginName);
    if (handle != UINT32_MAX) {
        if (codeName) {
            ::kcdx::log::EmitPluginKV(::kcdx::log::Level::Error,
                handle, "GUARD", "FAULTED",
                { KV::BareStr("site",   site ? site : "?"),
                  KV::BareStr("code",   codeName),
                  KV("rip",    reinterpret_cast<void*>(rip)),
                  KV::BareStr("module", moduleName),
                  KV("module_rva", rva),
                  KV("thread", (unsigned long)GetCurrentThreadId()) });
        } else {
            ::kcdx::log::EmitPluginKV(::kcdx::log::Level::Error,
                handle, "GUARD", "FAULTED",
                { KV::BareStr("site",   site ? site : "?"),
                  KV("code",   (unsigned long)code),
                  KV("rip",    reinterpret_cast<void*>(rip)),
                  KV::BareStr("module", moduleName),
                  KV("module_rva", rva),
                  KV("thread", (unsigned long)GetCurrentThreadId()) });
        }
    }
}

// The actual guarded invocation. Lives in its own function so the
// __try frame doesn't span any code with C++ unwinding semantics.
// Returns 0 on clean run, exception code on fault. The caller logs
// the fault — we just have to capture the EXCEPTION_RECORD here.
DWORD InvokeGuarded(GuardedFn fn, void* userdata,
                    EXCEPTION_RECORD* outRecord) {
    __try {
        fn(userdata);
        return 0;
    } __except (
        // Copy the record out so the C++-side fault formatter can use
        // it after the __except block (where GetExceptionInformation
        // is no longer valid). Then execute the handler.
        (*outRecord = *(GetExceptionInformation()->ExceptionRecord)),
        EXCEPTION_EXECUTE_HANDLER) {
        return outRecord->ExceptionCode;
    }
}

LONG WINAPI UnhandledFilter(EXCEPTION_POINTERS* info) {
    // One last log line attributing the crash. Use the breadcrumb
    // (tls_lastSite/tls_lastPlugin) so even an unguarded escape from
    // a recently-guarded site still gets attribution.
    if (info && info->ExceptionRecord) {
        LogFault(tls_lastSite ? tls_lastSite : "unhandled",
                 tls_lastPlugin,
                 info->ExceptionRecord);
    } else {
        LOG_ERROR("GUARD", "UNHANDLED (no exception info available)");
    }

    // Chain to whatever filter was installed before us (BugSplat's, if
    // KCD2 has installed it by now). If there's no prior filter,
    // return EXCEPTION_CONTINUE_SEARCH so Windows still walks any
    // vectored handlers.
    if (g_priorFilter) return g_priorFilter(info);
    return EXCEPTION_CONTINUE_SEARCH;
}

}  // namespace

bool Call(const char* site, const char* pluginName,
          GuardedFn fn, void* userdata) {
    if (!fn) return true;

    // Record breadcrumb before entering — even if the guard itself
    // somehow escapes (won't, but defense in depth), the process-
    // level filter still has a name to log.
    const char* savedSite   = tls_lastSite;
    const char* savedPlugin = tls_lastPlugin;
    tls_lastSite   = site;
    tls_lastPlugin = pluginName;

    EXCEPTION_RECORD rec{};
    DWORD code = InvokeGuarded(fn, userdata, &rec);

    bool ok = (code == 0);
    if (!ok) {
        LogFault(site, pluginName, &rec);
    }

    tls_lastSite   = savedSite;
    tls_lastPlugin = savedPlugin;
    return ok;
}

Breadcrumb SetBreadcrumb(const char* site, const char* pluginName) {
    Breadcrumb prev{tls_lastSite, tls_lastPlugin};
    tls_lastSite   = site;
    tls_lastPlugin = pluginName;
    return prev;
}

void ClearBreadcrumb(const Breadcrumb& prev) {
    tls_lastSite   = prev.prevSite;
    tls_lastPlugin = prev.prevPlugin;
}

void InstallUnhandledExceptionFilter() {
    bool expected = false;
    if (!g_filterInstalled.compare_exchange_strong(expected, true)) {
        return;  // already installed
    }
    g_priorFilter = ::SetUnhandledExceptionFilter(UnhandledFilter);
    LOG_INFO("GUARD", "installed unhandled-exception filter (prior=0x%p)",
             reinterpret_cast<void*>(g_priorFilter));
}

}  // namespace kcdx::guard
