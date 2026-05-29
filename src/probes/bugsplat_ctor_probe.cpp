// === BugSplat ctor-hook install machinery (KEEP, not removed) ===
// see bugsplat_ctor_probe.h. This is the proven before_game-hook install
// machinery; a later relocation moves it into the permanent engine home
// and generalizes it.

#include "bugsplat_ctor_probe.h"

#include <windows.h>
#include <winternl.h>  // NTSTATUS, NTAPI

#include <atomic>
#include <cstdint>

#include "MinHook.h"

#include <asmjit/asmjit.h>

#include "../dev.h"
#include "../log.h"
#include "../modification_inventory.h"  // RegisterModification (probe category)
#include "../rom_borrowed/runtime_func_t.h"  // PROBE Z (loader-lock asmjit)

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
        log::Error("bugsplat ctor hook: orig ctor pointer null at dispatch");
        return self;  // best-effort no-op; the ctor returns void in
                      // source but the ABI returns rcx by convention
    }
    return orig(self, szDatabase, szApp, szVersion, szUser, flags);
}

// === ARCHIVED PROBE Z (2026-05-29): VERIFIED — asmjit codegen + branch_pool
// VirtualAlloc + runtime_func_t dtor all complete under loader lock at the
// bugsplat install site (jit_ptr=0x7FFF91990000 + dtor in 1ms, baseline
// matrix unchanged at 113/149). Root cause: engine direct-MH sites bypass
// hook_chain → MinHook returns MH_ERROR_ALREADY_CREATED on plugin install
// at same target. See: docs/known-issues/cap-59-fires picked a one-shot
// VM-init target that already ran by plugin load.md §Reframe 2026-05-29b
// + §Resolution (post-PROBE-α). Revive by flipping #if 0 → #if 1 if a
// future loader-lock asmjit/alloc regression is suspected.
#if 0
// === DIAGNOSTIC (PROBE Z): does runtime_func_t::make_jit_func (asmjit codegen
//     + branch_pool VirtualAlloc) complete successfully when called from this
//     install site under the Windows loader lock? Pre-VM site reached via BOTH
//     kcdx.dll DllMain (direct call when BugSplat64 already mapped) AND the
//     LDR-notification callback — both run under loader lock. Outcome decides
//     the migration shape for the engine-direct MinHook sites (lua_pcall,
//     ctor_bracket, bugsplat_ctor): pure-A.3 migrate-to-AddC requires this
//     to succeed; a deadlock/fault/null-return means bugsplat stays direct-MH
//     with adopt-on-already-created. Full outcome→meaning map in
//     docs/known-issues/cap-59-fires picked a one-shot VM-init target that
//     already ran by plugin load.md §"Active diagnostic instrumentation".
//     One-shot: runs once per boot via the existing g_installed latch in
//     Install(), and the local kProbeZRan flag below short-circuits any
//     duplicate call paths. Does NOT install the JIT detour (the real ctor
//     hook still uses MH_CreateHook); only exercises codegen + alloc.
bool kcdx_probe_z_pre(const kcdx::rom::runtime_func_t::parameters_t* /*params*/,
                      const uint8_t /*parameters_count*/,
                      kcdx::rom::runtime_func_t::return_value_t* /*return_value*/,
                      const uintptr_t /*target_func_ptr*/) {
    return true;
}

std::atomic<bool> kProbeZRan{false};

// SEH-only helper: no C++ objects in scope so __try/__except is legal here.
// Caller pre-builds the FuncSignature so this function holds only POD locals.
static uintptr_t SehCallMakeJit(kcdx::rom::runtime_func_t* rf,
                                const asmjit::FuncSignature* sig,
                                void* targetForNearVa,
                                bool* outFaulted) {
    uintptr_t jit = 0;
    __try {
        jit = rf->make_jit_func(*sig, asmjit::Arch::kX64,
                                &kcdx_probe_z_pre, nullptr,
                                reinterpret_cast<uintptr_t>(targetForNearVa));
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *outFaulted = true;
    }
    return jit;
}

void RunProbeZ(void* targetForNearVa) {
    bool expected = false;
    if (!kProbeZRan.compare_exchange_strong(expected, true,
                                            std::memory_order_acq_rel)) {
        return;
    }

    LOG_DEBUG_KV("PROBE_Z", "entered",
        log::KV("target", targetForNearVa),
        log::KV::BareStr("note",
            "loader-lock asmjit smoke test at bugsplat install site"));

    LOG_DEBUG_KV("PROBE_Z", "start_make_jit_func",
        log::KV("nearVa", targetForNearVa));

    // Heap-allocate runtime_func_t + FuncSignature so their dtors live outside
    // the SEH frame. Dtor of runtime_func_t disables-without-pOriginal per
    // runtime_func_t.h:77 — safe even if make_jit_func returns 0 or faulted.
    auto* rf = new kcdx::rom::runtime_func_t();
    auto* probeSig = new asmjit::FuncSignature(asmjit::CallConvId::kCDecl,
                                               asmjit::FuncSignature::kNoVarArgs,
                                               asmjit::TypeId::kVoid);
    bool faulted = false;
    uintptr_t jit = SehCallMakeJit(rf, probeSig, targetForNearVa, &faulted);
    delete probeSig;

    if (faulted) {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_faulted",
            log::KV::BareStr("verdict",
                "SEH fault during codegen/alloc under loader lock — bugsplat must stay direct-MH"));
    } else if (jit == 0) {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_returned_zero",
            log::KV::BareStr("verdict",
                "codegen or branch_pool allocation failed under loader lock — bugsplat must stay direct-MH or pre-init branch_pool earlier"));
    } else {
        LOG_DEBUG_KV("PROBE_Z", "make_jit_func_ret",
            log::KV("jit_ptr", (void*)jit),
            log::KV::BareStr("verdict",
                "asmjit codegen + branch_pool VirtualAlloc both completed under loader lock — pre-VM sites can migrate to hook_chain::AddC"));
    }

    // Dtor runs here. If it hangs under loader lock the next probe line
    // (dtor_ok) won't appear and the boot will freeze.
    delete rf;

    LOG_DEBUG_KV("PROBE_Z", "runtime_func_dtor_ok",
        log::KV::BareStr("note", "rf dtor completed under loader lock"));
}
#endif  // === END ARCHIVED PROBE Z

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
        log::Warn("bugsplat ctor hook: BugSplat64.dll not loaded yet; cannot install ctor hook");
        // Roll back the latch so a later attempt (e.g. LDR callback)
        // can retry.
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    void* target = reinterpret_cast<void*>(GetProcAddress(bsm, kMangledCtor));
    if (!target) {
        log::WarnF("bugsplat ctor hook: GetProcAddress(%s) returned null", kMangledCtor);
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
        log::WarnF("bugsplat ctor hook: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // PROBE Z call site (archived; see archive header at the function
    // definition above). Revive in lockstep with the #if 0 block by
    // flipping both to #if 1.
#if 0
    RunProbeZ(target);
#endif

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("bugsplat ctor hook: MH_CreateHook failed: %d", (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig_ctor.store(reinterpret_cast<BugSplatCtor_t>(origPtr),
                      std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("bugsplat ctor hook: MH_EnableHook failed: %d", (int)s);
        return false;
    }

    log::InfoF("bugsplat ctor hook: MiniDmpSender ctor hook installed at %p "
               "(BugSplat64.dll base %p, target_rva=0x%llx)",
               target, bsm,
               (unsigned long long)((uintptr_t)target - (uintptr_t)bsm));
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(target),
        kcdx::modification_inventory::Category::Probe, "bugsplat_ctor");
    return true;
}

// === LDR-notification install path ============================
//
// Local declarations of the LDR notification structs / typedefs
// (mirrors src/ldr_notify.cpp; not extracted to a header yet — when a
// later relocation moves this into the permanent engine home these merge
// with ldr_notify's copies).

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
        log::Info("bugsplat ctor hook: installed via LDR callback "
                  "(BugSplat64.dll just mapped, pre-its-DllMain)");
    } else {
        log::Warn("bugsplat ctor hook: LDR callback fired for BugSplat64.dll "
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
            log::Info("bugsplat ctor hook: BugSplat64.dll already mapped at "
                      "kcdx.asi DllMain — ctor hook installed immediately");
        }
        return ok;
    }

    HMODULE ntdll = GetModuleHandleW(L"ntdll.dll");
    if (!ntdll) {
        log::Warn("bugsplat ctor hook: ntdll.dll handle null; can't register LDR notify");
        return false;
    }
    auto pRegister = reinterpret_cast<PFN_LdrRegisterDllNotification>(
        GetProcAddress(ntdll, "LdrRegisterDllNotification"));
    if (!pRegister) {
        log::Warn("bugsplat ctor hook: LdrRegisterDllNotification not found");
        return false;
    }

    NTSTATUS s = pRegister(0, LdrNotifyCallback, nullptr, &g_ldrCookie);
    if (s != 0) {
        log::WarnF("bugsplat ctor hook: LdrRegisterDllNotification returned NTSTATUS 0x%08lx",
                   (unsigned long)s);
        return false;
    }
    log::Info("bugsplat ctor hook: LdrRegisterDllNotification armed for BugSplat64.dll mapping");
    return true;
}

}  // namespace kcdx::probes::bugsplat_ctor_probe
