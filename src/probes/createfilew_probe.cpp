// === DIAGNOSTIC (PROBE R) === see createfilew_probe.h

#include "createfilew_probe.h"

#include <windows.h>
#include <intrin.h>

#include <atomic>
#include <cstdint>
#include <cwctype>

#include "MinHook.h"

#include "../dev.h"
#include "../log.h"

namespace kcdx::probes::createfilew_probe {

namespace {

using CreateFileW_t = HANDLE (WINAPI*)(LPCWSTR, DWORD, DWORD,
                                       LPSECURITY_ATTRIBUTES, DWORD,
                                       DWORD, HANDLE);

std::atomic<CreateFileW_t> g_orig_CreateFileW{nullptr};
std::atomic<bool>          g_installed{false};

// Wide-string case-insensitive substring search. Returns true if
// `needle` appears anywhere in `hay`, comparing ASCII letters case-
// insensitively (the substrings we care about — "Kingdom Come",
// ".dmp" — are pure ASCII).
//
// `hay` may be NULL or unbounded; we cap at 32K wchars before
// giving up so a pathological caller can't loop us.
bool ContainsCaseInsensitive(LPCWSTR hay, LPCWSTR needle) {
    if (!hay || !needle) return false;
    constexpr size_t kMaxScan = 32 * 1024;

    size_t needleLen = 0;
    while (needle[needleLen] && needleLen < kMaxScan) ++needleLen;
    if (needleLen == 0) return false;

    auto lower = [](wchar_t c) -> wchar_t {
        if (c >= L'A' && c <= L'Z') return (wchar_t)(c - L'A' + L'a');
        return c;
    };

    for (size_t i = 0; i < kMaxScan && hay[i]; ++i) {
        size_t j = 0;
        while (j < needleLen
               && hay[i + j]
               && lower(hay[i + j]) == lower(needle[j])) {
            ++j;
        }
        if (j == needleLen) return true;
    }
    return false;
}

// Decode common dwDesiredAccess flags into a short readable mask.
// Only the bits we care about for diagnosing dmp file writes.
const char* AccessFlags(DWORD a) {
    DWORD rw = a & (GENERIC_READ | GENERIC_WRITE);
    if (rw == (GENERIC_READ | GENERIC_WRITE)) return "READ|WRITE";
    if (rw == GENERIC_WRITE)                  return "WRITE";
    if (rw == GENERIC_READ)                   return "READ";
    if (a == 0)                               return "QUERY";
    return "OTHER";
}

const char* CreationDispName(DWORD d) {
    switch (d) {
        case CREATE_NEW:        return "CREATE_NEW";
        case CREATE_ALWAYS:     return "CREATE_ALWAYS";
        case OPEN_EXISTING:     return "OPEN_EXISTING";
        case OPEN_ALWAYS:       return "OPEN_ALWAYS";
        case TRUNCATE_EXISTING: return "TRUNCATE_EXISTING";
        default:                return "OTHER";
    }
}

HANDLE WINAPI HookedCreateFileW(LPCWSTR               lpFileName,
                                DWORD                 dwDesiredAccess,
                                DWORD                 dwShareMode,
                                LPSECURITY_ATTRIBUTES lpSecurityAttributes,
                                DWORD                 dwCreationDisposition,
                                DWORD                 dwFlagsAndAttributes,
                                HANDLE                hTemplateFile) {
    // Capture the return address BEFORE doing anything else so a chatty
    // log call below doesn't perturb the stack we want to read.
    // _AddressOfReturnAddress() returns the address of the SLOT holding
    // the return address; deref it to get the actual return RIP.
    void* raSlot = _AddressOfReturnAddress();
    void* ra     = raSlot ? *static_cast<void**>(raSlot) : nullptr;

    if (lpFileName
        && (ContainsCaseInsensitive(lpFileName, L"Kingdom Come")
            || ContainsCaseInsensitive(lpFileName, L".dmp"))) {
        // Bounded snapshot of the path for the log. lpFileName may be
        // unterminated in pathological inputs; cap at 600 wchars
        // (Windows path limit is 260 by default, 32K under \\?\).
        constexpr size_t kCap = 600;
        wchar_t snapshot[kCap + 1];
        size_t i = 0;
        for (; i < kCap && lpFileName[i]; ++i) snapshot[i] = lpFileName[i];
        snapshot[i] = 0;

        char utf8[kCap * 3 + 1];
        int n = WideCharToMultiByte(CP_UTF8, 0, snapshot, -1,
                                    utf8, sizeof(utf8), nullptr, nullptr);
        if (n <= 0) {
            utf8[0] = '?';
            utf8[1] = 0;
        }

        // Module-context for ra: which DLL is the caller in?
        HMODULE callerMod = nullptr;
        char    modName[MAX_PATH] = {0};
        if (ra) {
            GetModuleHandleExA(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                                 | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                               static_cast<LPCSTR>(ra),
                               &callerMod);
            if (callerMod) {
                GetModuleFileNameA(callerMod, modName, sizeof(modName));
            }
        }

        // Strip the directory off modName for log brevity.
        const char* modShort = modName;
        for (const char* p = modName; *p; ++p) {
            if (*p == '\\' || *p == '/') modShort = p + 1;
        }

        LOG_DEBUG_KV("CREATEFILEW", "match",
                     log::KV("path",     utf8),
                     log::KV("rip",      (void*)ra),
                     log::KV("module",   modShort),
                     log::KV("ra_offset",
                             (uint64_t)(ra && callerMod
                                 ? (uintptr_t)ra - (uintptr_t)callerMod
                                 : 0)),
                     log::KV::BareStr("access",  AccessFlags(dwDesiredAccess)),
                     log::KV::BareStr("disp",    CreationDispName(dwCreationDisposition)));
    }

    CreateFileW_t orig = g_orig_CreateFileW.load(std::memory_order_acquire);
    if (!orig) {
        // Shouldn't happen — Install() sets this before MH_EnableHook
        // succeeds. Fall through to direct kernel32 call so behavior
        // is still correct even if our bookkeeping is wrong.
        HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
        if (k32) {
            orig = reinterpret_cast<CreateFileW_t>(
                GetProcAddress(k32, "CreateFileW"));
        }
        if (!orig) {
            SetLastError(ERROR_PROC_NOT_FOUND);
            return INVALID_HANDLE_VALUE;
        }
    }
    return orig(lpFileName, dwDesiredAccess, dwShareMode,
                lpSecurityAttributes, dwCreationDisposition,
                dwFlagsAndAttributes, hTemplateFile);
}

}  // namespace

bool Install() {
    if (!kcdx::dev::IsEnabled()) {
        // Production-quiet: zero overhead for normal users.
        return false;
    }
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed
    }

    HMODULE k32 = GetModuleHandleW(L"kernel32.dll");
    if (!k32) {
        log::Warn("PROBE R: GetModuleHandleW(kernel32.dll) returned null");
        return false;
    }
    void* target = reinterpret_cast<void*>(
        GetProcAddress(k32, "CreateFileW"));
    if (!target) {
        log::Warn("PROBE R: GetProcAddress(CreateFileW) returned null");
        return false;
    }

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedCreateFileW),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("PROBE R: MH_CreateHook(CreateFileW) failed: %d", (int)s);
        return false;
    }
    g_orig_CreateFileW.store(reinterpret_cast<CreateFileW_t>(origPtr),
                             std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("PROBE R: MH_EnableHook(CreateFileW) failed: %d", (int)s);
        return false;
    }

    log::InfoF("PROBE R: CreateFileW hook installed at %p (orig trampoline %p)",
               target, origPtr);
    return true;
}

}  // namespace kcdx::probes::createfilew_probe
