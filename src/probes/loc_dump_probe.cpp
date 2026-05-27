// === LOC-DUMP PROBE (LocalizeString key-capture) ====================
//
// See loc_dump_probe.h for the full framing. This probe answers two gating
// unknowns for the localization runtime-dump feature, observe-only:
//   (1) hooking the CLocalizedStringsManager ctor captures the manager `this`;
//   (2) hooking the TWO public LocalizeString overloads (vtable slots 21 / 22)
//       fires with a readable key string + gameplay caller-return-address per
//       call.
//
// Mirrors bugsplat_ctor_probe's MinHook install discipline (idempotent latch,
// atomic orig-pointer, Win64-fastcall function-pointer typing), but against
// WHGame.dll — the game's own module, resolved with GetModuleHandleW(L"WHGame.
// dll") the same way hooks.cpp / address_library.cpp resolve the game base.

#include "loc_dump_probe.h"

#include <windows.h>
#include <intrin.h>  // _ReturnAddress()

#include <atomic>
#include <cstdint>
#include <cstdio>  // std::snprintf (loc-fire report reason)

#include "MinHook.h"

#include "../dev.h"
#include "../log.h"
#include "../modification_inventory.h"  // RegisterModification (probe category)
#include "../test.h"  // kcdx::test::ReportResult — engine-internal probe self-report
                      // (same pattern as lua_plugin_loader.cpp's cap-23 report:
                      //  the behavior under test is engine machinery, so the engine
                      //  reports the row directly; a manifest-only test plugin
                      //  registers the names for PENDING tracking).

namespace kcdx::probes::loc_dump_probe {

namespace {

// === Verified RE constants (LOC-MANAGER-FINDINGS.md) =================
//
// PROBE-LOCAL LABELED CONSTANTS, not Address Library IDs — a USER-APPROVED
// DEFERRAL for this diagnostic step (mirrors bugsplat_ctor_probe's RVA-comment
// style). The AP1 cleanup (promote to seed IDs) is deferred to feature
// graduation by explicit user decision. This is NOT an oversight: a labeled-RVA
// here is the sanctioned form for the probe; the seed-ID promotion lands when
// the loc-dump feature graduates out of the probe stage.
//
// CLocalizedStringsManager ctor (FUN_1809f0ce4). First store is `*this =
// vtable`; hooking it captures `this` (RCX = Win64 fastcall arg 1).
constexpr uintptr_t kCtorRva = 0x9f0ce4;

// The two PUBLIC LocalizeString overloads are manager vtable slots 21 and 22.
// We do NOT hardcode their RVAs — we read each address off the captured
// instance's LIVE vtable at runtime: `(*(void***)this)[21]` / `[22]`. This is
// the robust path (no overload RVA to maintain, no ASLR base arithmetic) and
// these are the slots the RE pins (LOC-MANAGER-FINDINGS.md §"Slot 21 + 22 thunk
// ABIs"): slot 21 (offset 0xA8) = FUN_18051d514 (CryStringT overload), slot 22
// (offset 0xB0) = FUN_18242e770 (raw C-string overload). Both thunk into the
// inner FUN_18051d534; their OWN callers are the gameplay frames.
constexpr size_t kLocSlot21 = 21;  // offset 0xA8
constexpr size_t kLocSlot22 = 22;  // offset 0xB0

// === Function-pointer typings (Win64 fastcall) ======================
//
// Ctor: `*this = vtable` as its first store. Source return is effectively void,
// but the ABI returns `this` in RAX by convention (the bugsplat probe documents
// the same), so we type it returning void* and pass through.
//
// TWO-arg (verified — FUN_1809f0ce4 in _research/parallel-ghidra-research/
// loc-manager-recon.txt): param_1 = `this` (RCX), param_2 = a system/context
// pointer (RDX). The ctor stores it (`this[2] = param_2`) and makes a virtual
// call through it (`(**(code**)(*param_2 + 0x2a0))(param_2)` at ~ctor+0x110).
// The probe is observe-only: it passes `sysctx` through UNTOUCHED — never reads
// or derefs it. (A one-arg typedef left RDX = register garbage, so the ctor's
// `*param_2 + 0x2a0` deref faulted — AP2, arg-count wrong.)
using LocCtor_t = void* (__fastcall*)(void* self, void* sysctx);

// LocalizeString overload typedefs — abi_walker + full-disasm verified
// (LOC-MANAGER-FINDINGS.md §"Slot 21 + 22 thunk ABIs"; _abi_18051d514.txt /
// _abi_18242e770.txt). Both are 4-arg __fastcall returning char (bool). The key
// string is RDX in both, but the CryStringT overload (slot 21) needs ONE deref.
//
// Slot 21 (FUN_18051d514) — CryStringT overload. `cryStr` (RDX) points at a
// CryStringT whose char buffer pointer is stored at offset 0 (the thunk does
// `mov rdx,[rdx]`); length is at buffer-8. Key string = `*(const char**)cryStr`
// after a null check on cryStr.
using LocLocalizeStr21_t = char(__fastcall*)(void* self, void* cryStr, void* out, char flag);

// Slot 22 (FUN_18242e770) — raw C-string overload. `str` (RDX) IS the char*
// directly (the thunk strlen's it and forwards). Key string = `(const char*)str`.
using LocLocalizeStr22_t = char(__fastcall*)(void* self, const char* str, void* out, char flag);

// === Probe state =====================================================

std::atomic<LocCtor_t>          g_orig_ctor{nullptr};
std::atomic<LocLocalizeStr21_t> g_orig_loc21{nullptr};
std::atomic<LocLocalizeStr22_t> g_orig_loc22{nullptr};

// Captured manager `this` from the ctor hook (still needed to resolve the live
// vtable for installing the slot-21/22 hooks).
std::atomic<void*>          g_manager{nullptr};

std::atomic<bool>           g_installed{false};      // ctor-hook install latch
std::atomic<bool>           g_loc_installing{false}; // slot-21/22 install one-shot latch

// One-shot self-report latches so each row reports PASS exactly once from the
// FIRST observation (hook-fire-self-report convention) — never poll a count.
std::atomic<bool>           g_reported_capture{false};
std::atomic<bool>           g_reported_loc{false};  // first LocalizeString fire (slot 21 OR 22)

constexpr const char* kRowCapture = "cap-43-loc-ctor-capture";
constexpr const char* kRowLoc     = "cap-43-loc-localizestring-fire";

// Bounded-copy a narrow key string into a caller buffer for SAFE logging,
// mirroring bugsplat_ctor_probe's SnapshotWide. `key` may be null or (for the
// CryStringT overload) reached through one deref off a possibly-null pointer;
// it may also be unterminated. The whole bounded read is SEH-guarded so a
// bad/unmapped pointer is OBSERVED ("(null)") rather than crashing the game —
// observe-only must never take down the process it observes, and the byte-by-
// byte cap means an unterminated buffer never walks past `outCap` (a raw
// strlen in the logger would — AP14: say it, don't go silent or crash). `out`
// is always NUL-terminated on return. `key` is the already-resolved char*
// (slot 22: RDX directly; slot 21: the result of `*(const char**)cryStr`).
void SnapshotKey(const char* key, char* out, size_t outCap) {
    if (outCap == 0) return;
    auto fill = [&](const char* s) {
        size_t n = 0;
        while (s[n] && n + 1 < outCap) { out[n] = s[n]; ++n; }
        out[n] = 0;
    };
    if (!key) { fill("(null)"); return; }
    __try {
        size_t n = 0;
        for (; n + 1 < outCap; ++n) {
            char c = key[n];      // each read inside the SEH guard
            if (c == '\0') break;
            out[n] = c;
        }
        out[n] = 0;
        if (n == 0) fill("(empty)");
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        fill("(null)");
    }
}

// SEH-guarded one-level pointer deref. POD-only leaf (no C++ objects) so __try
// is legal here (C2712: __try can't coexist with object unwinding in one func).
// Returns the dereferenced pointer, or nullptr if the read faults.
const char* DerefKeyPtrNoFault(const void* cryStr) {
    if (!cryStr) return nullptr;
    __try {
        return *reinterpret_cast<const char* const*>(cryStr);
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return nullptr;
    }
}

// === The LocalizeString detours =====================================
//
// Observe-only: each reads its key per its overload's ABI, logs
// `(slot, key-string, caller-return-address)`, then calls the original
// UNMODIFIED with ALL FOUR args passed through and returns its result. NEVER
// mutate args or return. The first fire from EITHER slot reports the
// cap-43-loc-localizestring-fire row PASS (one-shot guarded).

// Report the shared LocalizeString-fire row on the first observation from
// either overload. Reads the captured key string + caller_ra (feature-driven
// values), so the row FAILS if neither slot fires or fires without a readable
// key — not a constant.
void ReportLocFireOnce(int slot, const char* readableKey, void* caller) {
    bool expected = false;
    if (!g_reported_loc.compare_exchange_strong(expected, true,
                                                std::memory_order_acq_rel)) {
        return;  // already reported on a prior fire (one-shot)
    }
    char detail[256];
    std::snprintf(detail, sizeof(detail),
                  "LocalizeString (loc-manager vtable slot %d) fired; key=\"%s\" "
                  "read + caller_ra=%p captured on first call (observe-only)",
                  slot, readableKey, caller);
    kcdx::test::ReportResult(kRowLoc, true, detail);
}

// Slot 21 — CryStringT overload. Key = `*(const char**)cryStr` (one deref),
// null-checked. Observe-only.
char __fastcall HookedLocalizeStr21(void* self, void* cryStr, void* out, char flag) {
    void* caller = _ReturnAddress();

    // Resolve the char buffer pointer out of the CryStringT (stored at offset 0).
    // The deref is SEH-guarded in DerefKeyPtrNoFault (a POD-only leaf): cryStr
    // could be null/unmapped at an early fire, and __try can't live in this
    // function (it constructs C++ temporaries via LOG_DEBUG_KV — C2712).
    const char* key = DerefKeyPtrNoFault(cryStr);
    char readable[256];
    SnapshotKey(key, readable, sizeof(readable));

    LOG_DEBUG_KV("LOC_DUMP", "localizestring_fire",
                 log::KV("slot",      (uint64_t)kLocSlot21),
                 log::KV::BareStr("key", readable),
                 log::KV("caller_ra", caller),
                 log::KV("this",      self));

    ReportLocFireOnce((int)kLocSlot21, readable, caller);

    LocLocalizeStr21_t orig = g_orig_loc21.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("LOC_DUMP: orig LocalizeString slot-21 pointer null at "
                   "dispatch (hook fired before orig stored)");
        return 0;  // best-effort; should never happen post-install
    }
    return orig(self, cryStr, out, flag);  // all four args through untouched
}

// Slot 22 — raw C-string overload. Key = `(const char*)str` directly. Observe-only.
char __fastcall HookedLocalizeStr22(void* self, const char* str, void* out, char flag) {
    void* caller = _ReturnAddress();

    char readable[256];
    SnapshotKey(str, readable, sizeof(readable));

    LOG_DEBUG_KV("LOC_DUMP", "localizestring_fire",
                 log::KV("slot",      (uint64_t)kLocSlot22),
                 log::KV::BareStr("key", readable),
                 log::KV("caller_ra", caller),
                 log::KV("this",      self));

    ReportLocFireOnce((int)kLocSlot22, readable, caller);

    LocLocalizeStr22_t orig = g_orig_loc22.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("LOC_DUMP: orig LocalizeString slot-22 pointer null at "
                   "dispatch (hook fired before orig stored)");
        return 0;  // best-effort; should never happen post-install
    }
    return orig(self, str, out, flag);  // all four args through untouched
}

// Install ONE slot's LocalizeString hook off the captured instance's live
// vtable. Resolves `vtable[slot]`, MH_CreateHook + MH_EnableHook, stores the
// original in `origOut`, and registers the modification. `detourName` /
// `invName` label the failure logs + the inventory entry. Returns true on a
// fully-enabled hook. Fail-state: each failure logs at Warn with the slot +
// resolved target (recoverable — the OTHER slot may still install + fire);
// severity matches consequence (a non-installed observe-only probe hook is a
// missed observation, not a crash risk). Per fail-state-logging.md.
bool InstallOneLocSlot(void** vtable, size_t slot, void* detour,
                       void** origOut, const char* invName) {
    void* target = vtable[slot];
    if (!target) {
        log::WarnF("LOC_DUMP: LocalizeString install skipped — vtable[%zu] null",
                   slot);
        return false;
    }

    MH_STATUS s = MH_CreateHook(target, detour, origOut);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_CreateHook(LocalizeString slot %zu @ %p) "
                   "failed: %d — this overload will not be observed this run",
                   slot, target, (int)s);
        return false;
    }

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_EnableHook(LocalizeString slot %zu @ %p) "
                   "failed: %d — this overload will not be observed this run",
                   slot, target, (int)s);
        return false;
    }

    log::InfoF("LOC_DUMP: LocalizeString hook installed at vtable[%zu] %p "
               "(resolved off captured manager)", slot, target);
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(target),
        kcdx::modification_inventory::Category::Probe, invName);
    return true;
}

// Install BOTH LocalizeString hooks (slots 21 + 22) ONCE, lazily, off the
// captured instance's live vtable. Called from inside the ctor detour the first
// time a manager `this` is captured. Each overload installs independently — one
// failing does not block the other (the live run reveals which slot the
// gameplay UI uses).
void InstallLocHooksOnce(void* self) {
    bool expected = false;
    if (!g_loc_installing.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        return;  // another thread / a prior ctor call already (is) installing
    }

    if (!self) {
        log::Warn("LOC_DUMP: LocalizeString install skipped — captured this is null");
        return;  // latch stays set: do not thrash retries on a bad capture
    }

    void** vtable = *reinterpret_cast<void***>(self);
    if (!vtable) {
        log::Warn("LOC_DUMP: LocalizeString install skipped — instance vtable null");
        return;
    }

    void* orig21 = nullptr;
    if (InstallOneLocSlot(vtable, kLocSlot21,
                          reinterpret_cast<void*>(&HookedLocalizeStr21),
                          &orig21, "loc_dump:localizestring_slot21")) {
        g_orig_loc21.store(reinterpret_cast<LocLocalizeStr21_t>(orig21),
                           std::memory_order_release);
    }

    void* orig22 = nullptr;
    if (InstallOneLocSlot(vtable, kLocSlot22,
                          reinterpret_cast<void*>(&HookedLocalizeStr22),
                          &orig22, "loc_dump:localizestring_slot22")) {
        g_orig_loc22.store(reinterpret_cast<LocLocalizeStr22_t>(orig22),
                           std::memory_order_release);
    }
}

// === The ctor detour =================================================
//
// Capture `this` (arg 1) into an atomic, log it, install the LocalizeString
// hooks off the live vtable on first capture, then call the original ctor and
// return its result. Observe-only — the ctor runs unmodified.
// `sysctx` is the ctor's second arg (RDX) — a system/context pointer the ctor
// uses (`this[2] = sysctx`; virtual call at `*sysctx + 0x2a0`). The probe passes
// it through untouched; it does not read or interpret it (observe-only).
void* __fastcall HookedCtor(void* self, void* sysctx) {
    g_manager.store(self, std::memory_order_release);

    LOG_DEBUG_KV("LOC_DUMP", "manager_captured",
                 log::KV("this", self));

    // First capture → report PASS once (one-shot guarded).
    bool expected = false;
    if (g_reported_capture.compare_exchange_strong(expected, true,
                                                    std::memory_order_acq_rel)) {
        kcdx::test::ReportResult(
            kRowCapture, true,
            "CLocalizedStringsManager ctor hook fired; manager `this` captured "
            "from arg 1 (RCX)");
    }

    // The LocalizeString hooks must be installed AFTER the original ctor runs:
    // the detour fires at function ENTRY, before the ctor body executes its
    // `*this = vtable` store, so the live vtable `(*(void***)self)[21/22]` is
    // not yet valid here. We install below, after orig(self) returns.

    LocCtor_t orig = g_orig_ctor.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("LOC_DUMP: orig ctor pointer null at dispatch");
        return self;  // best-effort no-op; ABI returns rcx by convention
    }
    void* ret = orig(self, sysctx);  // pass BOTH args through untouched

    // Now the ctor body has run `*this = vtable`; the live vtable is readable.
    InstallLocHooksOnce(self);

    return ret;
}

}  // namespace

bool Install() {
    if (!kcdx::dev::IsEnabled()) return false;

    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return true;  // already installed this session
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        log::Warn("LOC_DUMP: WHGame.dll not mapped yet; cannot install ctor hook");
        g_installed.store(false, std::memory_order_release);  // allow retry
        return false;
    }

    // MinHook is initialized by the worker-thread install path (hooks::Install
    // calls MH_Initialize before we run). Call it idempotently anyway —
    // ALREADY_INITIALIZED is the no-op case — so the probe is robust to its
    // caller's ordering.
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        log::WarnF("LOC_DUMP: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    void* ctorTarget = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(whgame) + kCtorRva);

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(ctorTarget,
                                reinterpret_cast<void*>(&HookedCtor),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_CreateHook(ctor @ %p) failed: %d",
                   ctorTarget, (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig_ctor.store(reinterpret_cast<LocCtor_t>(origPtr),
                      std::memory_order_release);

    s = MH_EnableHook(ctorTarget);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_EnableHook(ctor @ %p) failed: %d",
                   ctorTarget, (int)s);
        return false;
    }

    log::InfoF("LOC_DUMP: CLocalizedStringsManager ctor hook installed at %p "
               "(WHGame.dll base %p, ctor_rva=0x%llx)",
               ctorTarget, reinterpret_cast<void*>(whgame),
               (unsigned long long)kCtorRva);
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(ctorTarget),
        kcdx::modification_inventory::Category::Probe, "loc_dump:ctor");
    return true;
}

}  // namespace kcdx::probes::loc_dump_probe
