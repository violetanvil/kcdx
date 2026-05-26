// === PROBE S + PROBE T (answered 2026-05-26 — KEEP, not removed) ===
// see bugsplat_ctor_probe.h. This is the proven before_game-hook install
// machinery; Phase 11 relocates it into the permanent engine home and
// generalizes it (docs/outstanding-work/before-game-hooks.md §5).

#include "bugsplat_ctor_probe.h"

#include <windows.h>
#include <winternl.h>  // NTSTATUS, NTAPI

#include <atomic>
#include <cstdint>

#include "MinHook.h"

#include "../dev.h"
#include "../log.h"
#include "../modification_inventory.h"  // RegisterModification (probe category)

namespace kcdx::probes::bugsplat_ctor_probe {

namespace {

// Mangled name of the 5-arg ctor variant we want, per dumpbin /exports:
//   ??0MiniDmpSender@@QEAA@PEB_W000K@Z
// Decoded: void __cdecl MiniDmpSender::MiniDmpSender(
//   wchar_t const* szDatabase,
//   wchar_t const* szApp,
//   wchar_t const* szVersion,
//   wchar_t const* szUser,
//   unsigned long  flags
// )
// Win64 fastcall: RCX=this, RDX=szDatabase, R8=szApp, R9=szVersion,
//                 [rsp+0x28]=szUser, [rsp+0x30]=flags.
//
// We type the function pointer to match. `this` is the constructed
// MiniDmpSender* — we don't deref it; just pass through.
using BugSplatCtor_t = void* (__fastcall*)(void*,             // this
                                           const wchar_t*,    // szDatabase
                                           const wchar_t*,    // szApp
                                           const wchar_t*,    // szVersion
                                           const wchar_t*,    // szUser
                                           uint32_t);         // flags

constexpr const char* kMangledCtor =
    "??0MiniDmpSender@@QEAA@PEB_W000K@Z";

std::atomic<BugSplatCtor_t> g_orig_ctor{nullptr};
std::atomic<bool>           g_installed{false};

// Bounded snapshot of a wide-string for logging. Returns "(null)" on
// nullptr, "(empty)" on zero-length, truncated UTF-8 on long inputs.
void SnapshotWide(const wchar_t* w, char* out, size_t outCap) {
    if (!w) {
        if (outCap > 0) {
            const char* s = "(null)";
            size_t n = 0;
            while (s[n] && n + 1 < outCap) { out[n] = s[n]; ++n; }
            out[n] = 0;
        }
        return;
    }
    if (!w[0]) {
        if (outCap > 0) {
            const char* s = "(empty)";
            size_t n = 0;
            while (s[n] && n + 1 < outCap) { out[n] = s[n]; ++n; }
            out[n] = 0;
        }
        return;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1,
                                out, (int)outCap, nullptr, nullptr);
    if (n <= 0 && outCap > 0) out[0] = '?', out[1] = 0;
}

void* __fastcall HookedCtor(void*          self,
                            const wchar_t* szDatabase,
                            const wchar_t* szApp,
                            const wchar_t* szVersion,
                            const wchar_t* szUser,
                            uint32_t       flags) {
    char db[256]  = {0};
    char app[256] = {0};
    char ver[128] = {0};
    char usr[128] = {0};
    SnapshotWide(szDatabase, db,  sizeof(db));
    SnapshotWide(szApp,      app, sizeof(app));
    SnapshotWide(szVersion,  ver, sizeof(ver));
    SnapshotWide(szUser,     usr, sizeof(usr));

    LOG_DEBUG_KV("BUGSPLAT_CTOR", "fire",
                 log::KV("this",       self),
                 log::KV("szDatabase", db),
                 log::KV("szApp",      app),
                 log::KV("szVersion",  ver),
                 log::KV("szUser",     usr),
                 log::KV("flags",      (uint64_t)flags));

    BugSplatCtor_t orig = g_orig_ctor.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("PROBE S: orig ctor pointer null at dispatch");
        return self;  // best-effort no-op; the ctor returns void in
                      // source but the ABI returns rcx by convention
    }
    return orig(self, szDatabase, szApp, szVersion, szUser, flags);
}

}  // namespace

bool Install() {
    if (!kcdx::dev::IsEnabled()) return false;

    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;
    }

    HMODULE bsm = GetModuleHandleW(L"BugSplat64.dll");
    if (!bsm) {
        log::Warn("PROBE S/T: BugSplat64.dll not loaded yet; cannot install ctor hook");
        // Roll back the latch so a later attempt (e.g. LDR callback)
        // can retry.
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(bsm, kMangledCtor));
    if (!target) {
        log::WarnF("PROBE S/T: GetProcAddress(%s) returned null", kMangledCtor);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook must be initialized. The worker-thread path
    // (hooks::Install) already calls MH_Initialize before invoking
    // us. The LDR-callback path (ArmLdrInstall) runs much earlier —
    // under loader lock during kcdx.dll DllMain — so MinHook may not
    // be initialized yet. Call MH_Initialize idempotently; the
    // ALREADY_INITIALIZED status is the no-op case.
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        log::WarnF("PROBE S/T: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("PROBE S/T: MH_CreateHook failed: %d", (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig_ctor.store(reinterpret_cast<BugSplatCtor_t>(origPtr),
                      std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("PROBE S/T: MH_EnableHook failed: %d", (int)s);
        return false;
    }

    log::InfoF("PROBE S/T: MiniDmpSender ctor hook installed at %p "
               "(BugSplat64.dll base %p, target_rva=0x%llx)",
               target, bsm,
               (unsigned long long)((uintptr_t)target - (uintptr_t)bsm));
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(target),
        kcdx::modification_inventory::Category::Probe, "bugsplat_ctor");
    return true;
}

// === PROBE T: LDR-notification path ============================
//
// Local declarations of the LDR notification structs / typedefs
// (mirrors src/ldr_notify.cpp; not extracted to a header yet — when
// Phase 11 relocates this into the permanent engine home these merge
// with ldr_notify's copies, see
// docs/outstanding-work/before-game-hooks.md §5/§8).

namespace {

constexpr ULONG kLdrLoaded = 1;

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
    ULONG, PLDR_DLL_NOTIFICATION_DATA_LOCAL, PVOID);

typedef NTSTATUS (NTAPI *PFN_LdrRegisterDllNotification)(
    ULONG, PLDR_DLL_NOTIFICATION_FUNCTION_LOCAL, PVOID, PVOID*);

PVOID g_ldrCookie = nullptr;

bool BaseNameEqualsBugSplat(PUNICODE_STRING bn) {
    if (!bn || bn->Length == 0) return false;
    size_t wlen = bn->Length / sizeof(WCHAR);
    static const wchar_t kTarget[] = L"BugSplat64.dll";
    constexpr size_t kTargetLen = (sizeof(kTarget) / sizeof(wchar_t)) - 1;
    if (wlen != kTargetLen) return false;
    for (size_t i = 0; i < wlen; ++i) {
        wchar_t a = bn->Buffer[i];
        wchar_t b = kTarget[i];
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

VOID CALLBACK LdrNotifyCallback(ULONG reason,
                                PLDR_DLL_NOTIFICATION_DATA_LOCAL data,
                                PVOID /*ctx*/) {
    if (reason != kLdrLoaded) return;
    if (!data || !data->Loaded.BaseDllName) return;
    if (!BaseNameEqualsBugSplat(data->Loaded.BaseDllName)) return;

    // BugSplat64.dll has been mapped but its DllMain hasn't run yet.
    // Install the ctor hook before WHGame.dll's init code can call it.
    bool ok = Install();
    if (ok) {
        log::Info("PROBE T: ctor hook installed via LDR callback "
                  "(BugSplat64.dll just mapped, pre-its-DllMain)");
    } else {
        log::Warn("PROBE T: LDR callback fired for BugSplat64.dll "
                  "but Install() returned false");
    }
}

}  // namespace

bool ArmLdrInstall() {
    if (!kcdx::dev::IsEnabled()) return false;

    // If BugSplat64.dll is already mapped (it usually is, pulled in by
    // the game's normal startup walking the static-import chain before
    // kcdx.dll loads), install immediately. Otherwise register
    // an LDR notification callback to catch the load.
    if (GetModuleHandleW(L"BugSplat64.dll") != nullptr) {
        bool ok = Install();
        if (ok) {
            log::Info("PROBE T: BugSplat64.dll already mapped at "
                      "kcdx.asi DllMain — ctor hook installed immediately");
        }
        return ok;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        log::Warn("PROBE T: ntdll.dll handle null; can't register LDR notify");
        return false;
    }
    auto pRegister = reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!pRegister) {
        log::Warn("PROBE T: LdrRegisterDllNotification not found");
        return false;
    }

    NTSTATUS s = pRegister(0, LdrNotifyCallback, nullptr, &g_ldrCookie);
    if (s != 0) {
        log::WarnF("PROBE T: LdrRegisterDllNotification returned NTSTATUS 0x%08lx",
                   (unsigned long)s);
        return false;
    }
    log::Info("PROBE T: LdrRegisterDllNotification armed for BugSplat64.dll mapping");
    return true;
}

}  // namespace kcdx::probes::bugsplat_ctor_probe
