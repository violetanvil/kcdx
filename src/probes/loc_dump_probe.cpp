// === LOC-DUMP PROBE (step 1 — minimal live probe) ===================
//
// See loc_dump_probe.h for the full framing. This probe answers two gating
// unknowns for the localization runtime-dump feature, observe-only:
//   (1) hooking the CLocalizedStringsManager ctor captures the manager `this`;
//   (2) hooking the by-INT-ID getter (vtable slot 1) fires with a readable
//       (caller-return-address, id) per call.
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

#include "MinHook.h"

#include "../dev.h"
#include "../log.h"
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

// The by-INT-ID getter is vtable slot 1 (offset 0x8). We do NOT hardcode its
// RVA — we read its address off the captured instance's LIVE vtable at runtime:
// `(*(void***)this)[1]`. This is the robust path (no second RVA to maintain, no
// ASLR base arithmetic for the getter) and it is the slot the RE pins
// (LOC-MANAGER-FINDINGS.md: "slot 1 char* FUN_1804d99e0(this, uint id)").
constexpr size_t kGetterVtableSlot = 1;

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

// by-ID getter, slot 1. Decompiled signature `char* (this, uint id)`. RCX=this,
// EDX=id. THIS PROBE IS THE VERIFICATION of this ABI: a readable `id` in the
// live log confirms the shape.
using LocByIdGetter_t = char* (__fastcall*)(void* self, uint32_t id);

// === Probe state =====================================================

std::atomic<LocCtor_t>      g_orig_ctor{nullptr};
std::atomic<LocByIdGetter_t> g_orig_getter{nullptr};

// Captured manager `this` from the ctor hook (unknown #1's deliverable).
std::atomic<void*>          g_manager{nullptr};

std::atomic<bool>           g_installed{false};        // ctor-hook install latch
std::atomic<bool>           g_getter_installing{false};// getter-install one-shot latch

// One-shot self-report latches so each row reports PASS exactly once from the
// FIRST observation (hook-fire-self-report convention) — never poll a count.
std::atomic<bool>           g_reported_capture{false};
std::atomic<bool>           g_reported_getter{false};

constexpr const char* kRowCapture = "cap-43-loc-ctor-capture";
constexpr const char* kRowGetter  = "cap-43-loc-byid-getter";

// === The by-ID getter detour ========================================
//
// Observe-only: log (caller-return-address, id), then call the original
// unmodified and return its result. NEVER mutate args or return.
char* __fastcall HookedGetter(void* self, uint32_t id) {
    void* caller = _ReturnAddress();

    LOG_DEBUG_KV("LOC_DUMP", "byid_getter_fire",
                 log::KV("caller_ra", caller),
                 log::KV("this",      self),
                 log::KV("id",        (uint64_t)id));

    // First fire → the slot-1 ABI is exercised with a readable id; report PASS
    // once (one-shot guarded), per the hook-fire-self-report convention.
    bool expected = false;
    if (g_reported_getter.compare_exchange_strong(expected, true,
                                                  std::memory_order_acq_rel)) {
        kcdx::test::ReportResult(
            kRowGetter, true,
            "by-ID getter (loc-manager vtable slot 1) fired; caller-return-"
            "address + id read via the char*(this,uint id) ABI on first call");
    }

    LocByIdGetter_t orig = g_orig_getter.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("LOC_DUMP: orig getter pointer null at dispatch");
        return nullptr;  // best-effort; should never happen post-install
    }
    return orig(self, id);
}

// Install the slot-1 getter hook ONCE, lazily, off the captured instance's live
// vtable. Called from inside the ctor detour the first time a manager `this` is
// captured. The getter target = `(*(void***)self)[1]`.
void InstallGetterHookOnce(void* self) {
    bool expected = false;
    if (!g_getter_installing.compare_exchange_strong(expected, true,
                                                     std::memory_order_acq_rel)) {
        return;  // another thread / a prior ctor call already (is) installing
    }

    if (!self) {
        log::Warn("LOC_DUMP: getter install skipped — captured this is null");
        g_getter_installing.store(false, std::memory_order_release);
        return;
    }

    void** vtable = *reinterpret_cast<void***>(self);
    if (!vtable) {
        log::Warn("LOC_DUMP: getter install skipped — instance vtable null");
        g_getter_installing.store(false, std::memory_order_release);
        return;
    }
    void* getterTarget = vtable[kGetterVtableSlot];
    if (!getterTarget) {
        log::Warn("LOC_DUMP: getter install skipped — vtable[1] null");
        g_getter_installing.store(false, std::memory_order_release);
        return;
    }

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(getterTarget,
                                reinterpret_cast<void*>(&HookedGetter),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_CreateHook(getter @ %p) failed: %d",
                   getterTarget, (int)s);
        return;  // leave the latch set: do not thrash retries on a hard failure
    }
    g_orig_getter.store(reinterpret_cast<LocByIdGetter_t>(origPtr),
                        std::memory_order_release);

    s = MH_EnableHook(getterTarget);
    if (s != MH_OK) {
        log::WarnF("LOC_DUMP: MH_EnableHook(getter @ %p) failed: %d",
                   getterTarget, (int)s);
        return;
    }

    log::InfoF("LOC_DUMP: by-ID getter hook installed at vtable[1] %p "
               "(resolved off captured manager %p)", getterTarget, self);
}

// === The ctor detour =================================================
//
// Capture `this` (arg 1) into an atomic, log it, install the getter hook off
// the live vtable on first capture, then call the original ctor and return its
// result. Observe-only — the ctor runs unmodified.
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

    // The getter hook must be installed AFTER the original ctor runs: the
    // detour fires at function ENTRY, before the ctor body executes its
    // `*this = vtable` store, so the live vtable `(*(void***)self)[1]` is not
    // yet valid here. We install it below, after orig(self) returns.

    LocCtor_t orig = g_orig_ctor.load(std::memory_order_acquire);
    if (!orig) {
        log::Error("LOC_DUMP: orig ctor pointer null at dispatch");
        return self;  // best-effort no-op; ABI returns rcx by convention
    }
    void* ret = orig(self, sysctx);  // pass BOTH args through untouched

    // Now the ctor body has run `*this = vtable`; the live vtable is readable.
    InstallGetterHookOnce(self);

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
    return true;
}

}  // namespace kcdx::probes::loc_dump_probe
