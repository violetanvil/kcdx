// === early_hook — author-parameterized early-install primitive ===
// See early_hook.h. The generalized engine half of the before_game-hook timing:
// install a MinHook detour by (module + export + signature + detour), immediate
// if the module is mapped or via an LDR notification the instant it maps. The
// loader-lock safety + MinHook idempotence here were proven live for the
// MiniDmpSender ctor target (a log-only consumer; see the bottom of this file).

#include "early_hook.h"

#include <windows.h>
#include <winternl.h>  // NTSTATUS, NTAPI

#include <atomic>
#include <cstdint>

#include "MinHook.h"

#include "dev.h"
#include "log.h"
#include "modification_inventory.h"  // RegisterModification

namespace kcdx::early_hook {

namespace {

// Per-request install latch. Keyed by the request's address (each consumer
// passes a process-lifetime InstallRequest, so its address is a stable
// identity). A small fixed-capacity table — early installs are few (a handful of
// before_game consumers), so a linear scan under a tiny lock is ample and
// allocation-free. Idempotence: a request that has already installed returns
// true without re-installing.
constexpr size_t kMaxArmed = 16;

struct Slot {
    const InstallRequest* req = nullptr;
    std::atomic<bool>     installed{false};
};

Slot          g_slots[kMaxArmed];
std::atomic<size_t> g_slotCount{0};
SRWLOCK       g_slotLock = SRWLOCK_INIT;  // guards g_slots[] membership only

// Find the slot for `req`, or claim a fresh one. Returns nullptr if the table
// is full (logged by the caller). Membership writes are guarded; the per-slot
// `installed` flag is an atomic the install path CASes without the lock.
Slot* SlotFor(const InstallRequest& req) {
    AcquireSRWLockExclusive(&g_slotLock);
    size_t count = g_slotCount.load(std::memory_order_relaxed);
    for (size_t i = 0; i < count; ++i) {
        if (g_slots[i].req == &req) {
            ReleaseSRWLockExclusive(&g_slotLock);
            return &g_slots[i];
        }
    }
    if (count >= kMaxArmed) {
        ReleaseSRWLockExclusive(&g_slotLock);
        return nullptr;
    }
    g_slots[count].req = &req;
    g_slotCount.store(count + 1, std::memory_order_release);
    Slot* s = &g_slots[count];
    ReleaseSRWLockExclusive(&g_slotLock);
    return s;
}

// Case-insensitive compare of a UNICODE_STRING base name against an ASCII-ish
// wide module name. Module names are filesystem-style (ASCII in practice).
bool BaseNameEquals(PUNICODE_STRING bn, const wchar_t* moduleName) {
    if (!bn || bn->Length == 0 || !moduleName) return false;
    size_t wlen = bn->Length / sizeof(WCHAR);
    size_t mlen = 0;
    while (moduleName[mlen]) ++mlen;
    if (wlen != mlen) return false;
    for (size_t i = 0; i < wlen; ++i) {
        wchar_t a = bn->Buffer[i];
        wchar_t b = moduleName[i];
        if (a >= L'A' && a <= L'Z') a = (wchar_t)(a - L'A' + L'a');
        if (b >= L'A' && b <= L'Z') b = (wchar_t)(b - L'A' + L'a');
        if (a != b) return false;
    }
    return true;
}

// ---------------------------------------------------------------------------
// LDR notification — single shared callback for every armed request.
//
// Local declarations of the LDR notification structs / typedefs (the public
// Windows SDK headers for our target version don't expose them). Mirrors
// src/ldr_notify.cpp's copies — that module owns the before_game BYTE-PATCH
// apply pass; this owns the before_game DETOUR install. They share the OS
// notification shape but not the mechanism (patch::ApplyResolvedPatch vs
// MH_CreateHook), so each keeps its own minimal local decls rather than
// coupling two concerns through a shared header.
// ---------------------------------------------------------------------------

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

std::atomic<PVOID> g_ldrCookie{nullptr};   // non-null once registered
std::atomic<bool>  g_ldrArmed{false};      // registration latch

VOID CALLBACK LdrNotifyCallback(ULONG reason,
                                PLDR_DLL_NOTIFICATION_DATA_LOCAL data,
                                PVOID /*ctx*/) {
    if (reason != kLdrLoaded) return;
    if (!data || !data->Loaded.BaseDllName) return;

    // A module just mapped (pre-its-own-DllMain). Install any armed request
    // whose module matches and that hasn't installed yet. Read membership
    // count with acquire to pair with SlotFor's release store; the slots
    // themselves are append-only (never removed), so a stable snapshot of the
    // count is a safe iteration bound.
    size_t count = g_slotCount.load(std::memory_order_acquire);
    for (size_t i = 0; i < count; ++i) {
        const InstallRequest* req = g_slots[i].req;
        if (!req) continue;
        if (g_slots[i].installed.load(std::memory_order_acquire)) continue;
        if (!BaseNameEquals(data->Loaded.BaseDllName, req->module)) continue;

        bool ok = Install(*req);
        if (ok) {
            log::InfoF("early_hook: installed '%s' via LDR callback "
                       "(module just mapped, pre-its-DllMain)",
                       req->exportName ? req->exportName : "(unnamed)");
        } else {
            log::WarnF("early_hook: LDR callback fired for armed request '%s' "
                       "but Install() returned false",
                       req->exportName ? req->exportName : "(unnamed)");
        }
    }
}

bool RegisterLdrCallbackOnce() {
    bool expected = false;
    if (!g_ldrArmed.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return g_ldrCookie.load(std::memory_order_acquire) != nullptr;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        log::Warn("early_hook: ntdll.dll handle null; cannot register LDR notify");
        g_ldrArmed.store(false, std::memory_order_release);
        return false;
    }
    auto pRegister = reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!pRegister) {
        log::Warn("early_hook: LdrRegisterDllNotification not found");
        g_ldrArmed.store(false, std::memory_order_release);
        return false;
    }

    PVOID cookie = nullptr;
    NTSTATUS s = pRegister(0, LdrNotifyCallback, nullptr, &cookie);
    if (s != 0) {
        log::WarnF("early_hook: LdrRegisterDllNotification returned "
                   "NTSTATUS 0x%08lx", (unsigned long)s);
        g_ldrArmed.store(false, std::memory_order_release);
        return false;
    }
    g_ldrCookie.store(cookie, std::memory_order_release);
    log::Info("early_hook: LdrRegisterDllNotification armed for deferred "
              "early-hook installs");
    return true;
}

}  // namespace

bool Install(const InstallRequest& req) {
    if (!req.module || !req.exportName || !req.detour || !req.trampoline) {
        log::Error("early_hook: Install called with an incomplete request "
                   "(module/export/detour/trampoline all required)");
        return false;
    }

    Slot* slot = SlotFor(req);
    if (!slot) {
        log::Error("early_hook: armed-request table full; cannot install");
        return false;
    }

    bool expected = false;
    if (!slot->installed.compare_exchange_strong(expected, true,
                                                 std::memory_order_acq_rel)) {
        return true;  // already installed — idempotent
    }

    HMODULE mod = GetModuleHandleW(req.module);
    if (!mod) {
        // Not yet mapped — roll back the latch so the LDR callback can retry.
        slot->installed.store(false, std::memory_order_release);
        return false;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(mod, req.exportName));
    if (!target) {
        log::WarnF("early_hook: GetProcAddress(%s) returned null in module",
                   req.exportName);
        slot->installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook may not be initialized yet: the LDR-callback path runs under the
    // loader lock during kcdx.dll DllMain, far before the worker thread's
    // MH_Initialize. Initialize idempotently — ALREADY_INITIALIZED is the no-op
    // case. PROVEN loader-lock-safe live for the MiniDmpSender ctor target.
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        log::WarnF("early_hook: MH_Initialize failed: %d", (int)si);
        slot->installed.store(false, std::memory_order_release);
        return false;
    }

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target, req.detour, &origPtr);
    if (s != MH_OK) {
        log::WarnF("early_hook: MH_CreateHook(%s) failed: %d",
                   req.exportName, (int)s);
        slot->installed.store(false, std::memory_order_release);
        return false;
    }
    *req.trampoline = origPtr;

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("early_hook: MH_EnableHook(%s) failed: %d",
                   req.exportName, (int)s);
        // Leave the latch set: the hook is created (trampoline valid) but not
        // enabled; re-enabling is not retried here. A future call sees the
        // latch and no-ops rather than double-creating.
        return false;
    }

    log::InfoF("early_hook: installed detour on %s at %p (module base %p, "
               "target_rva=0x%llx, sig='%s')",
               req.exportName, target, mod,
               (unsigned long long)((uintptr_t)target - (uintptr_t)mod),
               req.signature ? req.signature : req.exportName);
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(target),
        kcdx::modification_inventory::Category::Probe,
        req.inventoryTag ? req.inventoryTag : req.exportName);
    return true;
}

bool Arm(const InstallRequest& req) {
    if (!req.module || !req.exportName || !req.detour || !req.trampoline) {
        log::Error("early_hook: Arm called with an incomplete request");
        return false;
    }

    // Claim the slot up front so the LDR callback can find this request if the
    // module maps between here and registration.
    if (!SlotFor(req)) {
        log::Error("early_hook: armed-request table full; cannot arm");
        return false;
    }

    // If the module is already mapped (the common case — the game's static
    // import chain pulls many DLLs in before kcdx.dll's DllMain runs), install
    // immediately.
    if (GetModuleHandleW(req.module) != nullptr) {
        bool ok = Install(req);
        if (ok) {
            log::InfoF("early_hook: '%s' module already mapped at arm time — "
                       "installed immediately", req.exportName);
        }
        // Immediate install is sufficient; no LDR registration needed when the
        // module is already mapped.
        return ok;
    }

    // Module not mapped yet — register the shared LDR callback to catch it.
    return RegisterLdrCallbackOnce();
}

}  // namespace kcdx::early_hook

// ===========================================================================
// First consumer — the BugSplat colon-filename ctor hook (log-only today).
//
// Wired through the generalized primitive above. PRESERVES the prior probe's
// behavior exactly: dev-gated, log-only (calls the original ctor unchanged),
// installed at the SAME DllMain/LDR timing (immediate if BugSplat64.dll is
// already mapped, else via the LDR notification, pre-its-own-DllMain). The
// later before_game-hook work changes the detour body to rewrite szApp; this
// step only relocates + re-expresses the proven install, not the behavior.
//
// Target: BugSplat64.dll!MiniDmpSender::MiniDmpSender, mangled export
//   ??0MiniDmpSender@@QEAA@PEB_W000K@Z (export ordinal 3).
// ABI (verified live): void* __fastcall(this, const wchar_t* szDatabase,
//   const wchar_t* szApp, const wchar_t* szVersion, const wchar_t* szUser,
//   uint32_t flags). The ctor returns void in source; the ABI returns rcx by
//   convention, hence the void* return + pass-through of `self`.
// ===========================================================================

namespace kcdx::early_hook::bugsplat {

namespace {

using BugSplatCtor_t = void* (__fastcall*)(void*,           // this
                                           const wchar_t*,  // szDatabase
                                           const wchar_t*,  // szApp
                                           const wchar_t*,  // szVersion
                                           const wchar_t*,  // szUser
                                           uint32_t);        // flags

void* g_origCtor = nullptr;  // trampoline; written by Install before any fire

// Bounded snapshot of a wide-string for logging. "(null)" on nullptr,
// "(empty)" on zero-length, truncated UTF-8 otherwise.
void SnapshotWide(const wchar_t* w, char* out, size_t outCap) {
    if (outCap == 0) return;
    if (!w) {
        const char* s = "(null)";
        size_t n = 0;
        while (s[n] && n + 1 < outCap) { out[n] = s[n]; ++n; }
        out[n] = 0;
        return;
    }
    if (!w[0]) {
        const char* s = "(empty)";
        size_t n = 0;
        while (s[n] && n + 1 < outCap) { out[n] = s[n]; ++n; }
        out[n] = 0;
        return;
    }
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, out, (int)outCap,
                                nullptr, nullptr);
    if (n <= 0) { out[0] = '?'; if (outCap > 1) out[1] = 0; }
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

    auto orig = reinterpret_cast<BugSplatCtor_t>(g_origCtor);
    if (!orig) {
        log::Error("early_hook bugsplat: orig ctor pointer null at dispatch");
        return self;  // best-effort no-op
    }
    return orig(self, szDatabase, szApp, szVersion, szUser, flags);
}

// Process-lifetime request — the LDR callback reads it long after Arm returns.
InstallRequest g_req = {
    L"BugSplat64.dll",
    "??0MiniDmpSender@@QEAA@PEB_W000K@Z",
    reinterpret_cast<void*>(&HookedCtor),
    &g_origCtor,
    "void* __fastcall(this,wstr,wstr,wstr,wstr,u32)",
    "bugsplat_ctor",
};

}  // namespace

bool Arm() {
    if (!kcdx::dev::IsEnabled()) return false;  // dev-gated, as before
    return kcdx::early_hook::Arm(g_req);
}

}  // namespace kcdx::early_hook::bugsplat
