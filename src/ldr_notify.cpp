#include "ldr_notify.h"

#include <windows.h>
#include <winternl.h>  // NTSTATUS, NTAPI

#include <cstring>
#include <string>

#include "hook_engine.h"  // Source enum lives via config.h chain
#include "load_order.h"
#include "log.h"
#include "patch_engine.h"
#include "plugin_loader.h"

namespace kcdx::ldr_notify {

namespace {

// ---------------------------------------------------------------------------
// Local declarations of the LDR notification structs / typedefs. The
// LDR_DLL_NOTIFICATION_* types aren't in the public Windows SDK headers
// for our target version, so we declare them locally. UNICODE_STRING +
// NTSTATUS + NTAPI come from <winternl.h>.
// ---------------------------------------------------------------------------

constexpr ULONG kLdrDllNotificationReasonLoaded   = 1;
constexpr ULONG kLdrDllNotificationReasonUnloaded = 2;

typedef struct _LDR_DLL_LOADED_NOTIFICATION_DATA {
    ULONG            Flags;
    PUNICODE_STRING  FullDllName;
    PUNICODE_STRING  BaseDllName;
    PVOID            DllBase;
    ULONG            SizeOfImage;
} LDR_DLL_LOADED_NOTIFICATION_DATA_LOCAL;

typedef union _LDR_DLL_NOTIFICATION_DATA {
    LDR_DLL_LOADED_NOTIFICATION_DATA_LOCAL Loaded;
    LDR_DLL_LOADED_NOTIFICATION_DATA_LOCAL Unloaded;
} LDR_DLL_NOTIFICATION_DATA_LOCAL, *PLDR_DLL_NOTIFICATION_DATA_LOCAL;

typedef VOID (CALLBACK *PLDR_DLL_NOTIFICATION_FUNCTION_LOCAL)(
    ULONG                              NotificationReason,
    PLDR_DLL_NOTIFICATION_DATA_LOCAL   NotificationData,
    PVOID                              Context);

typedef NTSTATUS (NTAPI *PFN_LdrRegisterDllNotification)(
    ULONG                                 Flags,
    PLDR_DLL_NOTIFICATION_FUNCTION_LOCAL  NotificationFunction,
    PVOID                                 Context,
    PVOID*                                Cookie);

typedef NTSTATUS (NTAPI *PFN_LdrUnregisterDllNotification)(
    PVOID Cookie);

// ---------------------------------------------------------------------------
// State
// ---------------------------------------------------------------------------

PVOID g_cookie = nullptr;  // registration cookie; non-null after Register()

// Manual-reset event signaled when WHGame.dll has been mapped into the
// process. Created by Register(); set by NotificationCallback() the
// first time it sees WHGame.dll. The worker thread waits on this
// before calling hooks::Install (which targets WHGame.dll).
HANDLE g_whgameLoadedEvent = nullptr;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

std::string Utf16ToUtf8(PCWSTR data, size_t len) {
    if (!data || len == 0) return {};
    int needed = WideCharToMultiByte(CP_UTF8, 0, data, (int)len,
                                     nullptr, 0, nullptr, nullptr);
    if (needed <= 0) return {};
    std::string out(needed, '\0');
    WideCharToMultiByte(CP_UTF8, 0, data, (int)len,
                        out.data(), needed, nullptr, nullptr);
    return out;
}

// Case-insensitive ASCII compare. Module names are filesystem-style,
// always ASCII in practice (WHGame.dll, dinput8.dll, ...).
bool EqualIgnoreCase(const std::string& a, const std::string& b) {
    if (a.size() != b.size()) return false;
    for (size_t i = 0; i < a.size(); ++i) {
        char ca = a[i], cb = b[i];
        if (ca >= 'A' && ca <= 'Z') ca = (char)(ca - 'A' + 'a');
        if (cb >= 'A' && cb <= 'Z') cb = (char)(cb - 'A' + 'a');
        if (ca != cb) return false;
    }
    return true;
}

// True iff this patch entry's owning plugin sits in the before_game zone.
// Anonymous patches (no [plugin] table) default to after_game and are
// skipped here.
bool IsBeforeGame(const patch::PatchEntry& p) {
    if (p.pluginName.empty()) return false;
    // Gate through IsPluginEnabled so a zone_gate rejection
    // (engineAccepted = false) is honored alongside the user's enable
    // choice — direct .userEnabled reads here would bypass it.
    if (!load_order::IsPluginEnabled(p.pluginName)) return false;
    return load_order::Of(p.pluginName).zone == load_order::Zone::BeforeGame;
}

// Apply one entry; logs success / failure. The patch::Resolve +
// ApplyResolvedPatch pair is loader-safe — pure read + VirtualProtect +
// memcpy. We don't use the conflict-engine pre-flight here; the
// before_game zone applies entries one-at-a-time as their modules
// arrive, so the apply-order question is "first applied wins" which
// matches the sort key.
size_t ApplyEntriesForModule(const std::string& moduleName) {
    size_t applied = 0;
    for (auto& p : patch::g_patches) {
        if (!IsBeforeGame(p)) continue;
        if (!EqualIgnoreCase(p.module, moduleName)) continue;
        if (p.appliedOK) continue;

        // patch::Resolve() relies on GetModuleHandleW for the named
        // module — safe under loader lock for an already-mapped module.
        patch::ResolvedPatch r = patch::Resolve(p);
        bool ok = patch::ApplyResolvedPatch(p, r);
        if (ok) {
            ++applied;
            // Note: ApplyResolvedPatch already sets p.appliedOK on
            // success, but mark explicitly for paranoia. Reading is
            // safe; this is the only writer for before_game entries.
            p.appliedOK = true;
        }
    }
    return applied;
}

VOID CALLBACK NotificationCallback(ULONG                              reason,
                                   PLDR_DLL_NOTIFICATION_DATA_LOCAL   data,
                                   PVOID                              /*ctx*/) {
    // Only act on Loaded; unloaded is a no-op for byte patches (the
    // bytes go with the module when it unmaps).
    if (reason != kLdrDllNotificationReasonLoaded) return;
    if (!data || !data->Loaded.BaseDllName) return;

    const auto& bn = *data->Loaded.BaseDllName;
    // Length is in BYTES (UNICODE_STRING convention), not characters.
    size_t wlen = bn.Length / sizeof(WCHAR);
    if (wlen == 0) return;

    std::string name = Utf16ToUtf8(bn.Buffer, wlen);
    if (name.empty()) return;

    size_t n = ApplyEntriesForModule(name);
    if (n > 0) {
        // log::InfoF is loader-safe pre-Init (deferred-log buffer
        // queues + mirrors to OutputDebugStringA).
        log::InfoF("ldr_notify: applied %zu before_game patch(es) "
                   "to '%s' immediately after map",
                   n, name.c_str());
    }

    // Signal the WHGame-loaded event so any thread waiting in
    // WaitForGameDll() can proceed. SetEvent is loader-lock-safe
    // (documented in MSDN as a kernel-only operation that doesn't
    // load other DLLs).
    if (g_whgameLoadedEvent && EqualIgnoreCase(name, "WHGame.dll")) {
        SetEvent(g_whgameLoadedEvent);
        log::Info("ldr_notify: WHGame.dll mapped; signaled gate event");
    }
}

}  // namespace

size_t ApplyAlreadyLoaded() {
    size_t total = 0;
    // For each before_game entry, check if its module is already
    // mapped (ntdll, kernel32, and kcdx.asi itself are always loaded
    // by the time we run; everything else depends on the loader).
    for (auto& p : patch::g_patches) {
        if (!IsBeforeGame(p)) continue;
        if (p.appliedOK) continue;

        std::wstring wmod;
        wmod.assign(p.module.begin(), p.module.end());
        if (GetModuleHandleW(wmod.c_str()) == nullptr) {
            // Not yet mapped — LDR callback will catch it.
            continue;
        }

        patch::ResolvedPatch r = patch::Resolve(p);
        if (patch::ApplyResolvedPatch(p, r)) {
            ++total;
            p.appliedOK = true;
        }
    }
    if (total > 0) {
        log::InfoF("ldr_notify: applied %zu before_game patch(es) "
                   "to already-loaded module(s) at DllMain time", total);
    }
    return total;
}

bool Register() {
    if (g_cookie) return true;

    // Create the WHGame-loaded gate event before registering the
    // callback. Manual-reset so multiple waiters can be released by
    // one signal; initially unsignaled. If WHGame.dll is somehow
    // already loaded at this point (would only happen if kcdx.dll got
    // injected after KCD2's startup ran some, which our launcher
    // doesn't do — but defensive), set it immediately so the worker
    // thread doesn't deadlock.
    if (!g_whgameLoadedEvent) {
        g_whgameLoadedEvent = CreateEventW(nullptr, /*manualReset=*/TRUE,
                                           /*initialState=*/FALSE, nullptr);
        if (g_whgameLoadedEvent && GetModuleHandleW(L"WHGame.dll") != nullptr) {
            SetEvent(g_whgameLoadedEvent);
            log::Info("ldr_notify: WHGame.dll already loaded at Register; "
                      "gate event pre-signaled");
        }
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        log::Warn("ldr_notify: GetModuleHandleW(ntdll.dll) returned null; "
                  "before_game patches deferred to first-update-tick");
        return false;
    }
    auto pRegister = reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!pRegister) {
        log::Warn("ldr_notify: LdrRegisterDllNotification not exported by "
                  "ntdll.dll; before_game patches deferred to "
                  "first-update-tick");
        return false;
    }

    NTSTATUS s = pRegister(0, NotificationCallback, nullptr, &g_cookie);
    if (s != 0 /*STATUS_SUCCESS*/) {
        log::WarnF("ldr_notify: LdrRegisterDllNotification returned "
                   "NTSTATUS=0x%08lx; before_game patches deferred to "
                   "first-update-tick", (unsigned long)s);
        g_cookie = nullptr;
        return false;
    }

    log::Info("ldr_notify: registered LdrRegisterDllNotification "
              "callback for before_game patches");
    return true;
}

void Unregister() {
    if (!g_cookie) return;

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) { g_cookie = nullptr; return; }
    auto pUnregister = reinterpret_cast<PFN_LdrUnregisterDllNotification>(
        GetProcAddress(ntdll, "LdrUnregisterDllNotification"));
    if (pUnregister) pUnregister(g_cookie);
    g_cookie = nullptr;
}

bool WaitForGameDll(unsigned long timeoutMs) {
    // Fast path: WHGame.dll is already mapped (engine restart, late
    // injection, etc.).
    if (GetModuleHandleW(L"WHGame.dll") != nullptr) return true;

    // If Register() never ran or failed to allocate the event, we
    // can't wait. Fall back to spin-checking the module list briefly
    // before giving up.
    if (!g_whgameLoadedEvent) {
        log::Warn("ldr_notify: WaitForGameDll called without a registered "
                  "gate event; spin-checking GetModuleHandle for up to "
                  "10s as fallback");
        const unsigned long kSpinSliceMs = 50;
        unsigned long elapsed = 0;
        while (elapsed < 10'000) {
            Sleep(kSpinSliceMs);
            elapsed += kSpinSliceMs;
            if (GetModuleHandleW(L"WHGame.dll") != nullptr) return true;
        }
        return false;
    }

    DWORD r = WaitForSingleObject(g_whgameLoadedEvent, timeoutMs);
    if (r == WAIT_OBJECT_0) return true;
    if (r == WAIT_TIMEOUT) {
        log::WarnF("ldr_notify: timed out after %lu ms waiting for "
                   "WHGame.dll to load", timeoutMs);
        return false;
    }
    log::WarnF("ldr_notify: WaitForSingleObject returned %lu (gle=%lu)",
               (unsigned long)r, GetLastError());
    return false;
}

}  // namespace kcdx::ldr_notify
