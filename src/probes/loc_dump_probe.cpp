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

// === Step 2a — read-only manager-struct LAYOUT PROBE ================
//
// TWO one-shot latches for the struct-field dump — one per fire site. The
// ctor-time dump sees the manager layout but manager+0x18 (current language)
// is NULL by construction (the ctor zeroes it last); the getter-time dump
// fires only after a language has loaded, so manager+0x18 should be populated
// and its language-table layout dumpable. We want BOTH observations, so each
// site latches independently. See LayoutDump() for the full framing +
// outcome→meaning map.
std::atomic<bool>           g_layout_dumped_ctor{false};
std::atomic<bool>           g_layout_dumped_getter{false};

// How many qwords (8 bytes each) to dump per struct. 32 qwords = 0x100 bytes —
// the cap from the step spec. The manager is far larger (the ctor writes
// through param_1[0x18] = +0xC0 and beyond), but 0x100 covers every landmark
// the ctor pins ([3]/[6]/[9]/[10]/[0xb]) with margin.
constexpr int kDumpQwords = 32;

// === Step 2a layout-probe helpers ===================================
//
// Fault-guarded copy of `count` qwords from `src` into `out`. The manager
// `this` is a known-valid object (reading its own bytes never faults), but a
// pointer-INTO-heap reached FROM the manager (e.g. the language table at
// manager+0x18) can be stale/garbage — dereferencing it raw is exactly the
// step-1-class crash this probe exists to avoid. SEH-guard the copy so a bad
// pointer is OBSERVED (returns false → logged "unreadable") instead of
// crashing. Returns true iff all `count` qwords were read.
bool ReadQwordsNoFault(const void* src, uint64_t* out, int count) {
    __try {
        const volatile uint64_t* p = reinterpret_cast<const volatile uint64_t*>(src);
        for (int i = 0; i < count; ++i) out[i] = p[i];
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        return false;
    }
}

// Heuristic: do three consecutive already-read qwords (begin/end/capacity)
// look like a libstdc++/MSVC std::vector<T*> with 8-byte stride? Requires
// begin<=end<=capacity, begin non-null, and (end-begin) being a clean multiple
// of 8 (pointer entries). Pure arithmetic on values ALREADY in `buf` — it does
// NOT dereference begin/end/cap, so it never faults. `*outCount` ← element
// count on a hit.
bool LooksLikeVector(uint64_t begin, uint64_t end, uint64_t cap,
                     uint64_t* outCount) {
    if (begin == 0) return false;
    if (!(begin <= end && end <= cap)) return false;
    uint64_t span = end - begin;
    if ((span & 0x7) != 0) return false;        // not an 8-byte stride
    // A plausible non-degenerate vector: cap strictly past begin. (An empty
    // begin==end==cap vector is a valid C++ state but indistinguishable from
    // three equal junk values here, so we don't flag it — step 2b confirms
    // population live.)
    if (cap == begin) return false;
    *outCount = span / 8;
    return true;
}

// Dump the first kDumpQwords qwords of an already-read buffer as
// "<label> mgr_field" / "lang_field" lines (offset + hex value), and flag any
// vector-shaped triple. `what` distinguishes the manager dump ("mgr") from the
// language-table dump ("lang") in the action tag. Observe-only.
void DumpBufferFields(const char* what, const uint64_t* buf) {
    for (int i = 0; i < kDumpQwords; ++i) {
        // value rendered via the void* KV ctor → HEX (0x...). The qword is a
        // raw field word; hex is how a layout reader needs to SEE it.
        LOG_DEBUG_KV("LOC_DUMP", "field",
                     log::KV::BareStr("struct", what),
                     log::KV("offset",  (uint64_t)(i * 8)),
                     log::KV("value",   reinterpret_cast<void*>(buf[i])));
    }
    // Scan every offset for a vector-shaped triple (begin/end/cap at i,i+1,i+2).
    int vecHits = 0;
    for (int i = 0; i + 2 < kDumpQwords; ++i) {
        uint64_t count = 0;
        if (LooksLikeVector(buf[i], buf[i + 1], buf[i + 2], &count)) {
            ++vecHits;
            LOG_DEBUG_KV("LOC_DUMP", "vec_candidate",
                         log::KV::BareStr("struct", what),
                         log::KV("offset",   (uint64_t)(i * 8)),
                         log::KV("field_idx", (uint64_t)i),
                         log::KV("begin",    reinterpret_cast<void*>(buf[i])),
                         log::KV("end",      reinterpret_cast<void*>(buf[i + 1])),
                         log::KV("cap",      reinterpret_cast<void*>(buf[i + 2])),
                         log::KV("count",    count));
        }
    }
    // AP14: "ran, found nothing" is a state — say it, don't go silent.
    if (vecHits == 0) {
        LOG_DEBUG_KV("LOC_DUMP", "vec_scan_empty",
                     log::KV::BareStr("struct", what),
                     log::KV("note",
                             "no vector-shaped (begin<=end<=cap, 8-stride) "
                             "triple in first 0x100 bytes"));
    }
}

// One-shot LAYOUT PROBE (step 2a). Given the captured manager `this`, dump
// enough of the manager's (and the current-language sub-object's) field layout
// to CONFIRM the path manager → language-table → key↔int-ID vector BEFORE step
// 2b walks it for real. OBSERVE-ONLY: reads + logs raw field values; walks
// nothing, mutates nothing.
//
// OPEN QUESTION (what this probe answers):
//   WHERE is the per-language string-table whose vector<entry*> ([9]/[10]/[0xb]
//   in AddLocalizedString's param_2) gives key→int-ID? The ctor zeroes those
//   same indices on the MANAGER, but AddLocalizedString operates on a
//   per-LANGUAGE param_2 — the manager→table link is unconfirmed. The likely
//   link is manager+0x18 (this[3], "current language"); the ctor sets it to 0
//   as its LAST act, so it is NULL at ctor time and populated only once a
//   language loads.
//
// OUTCOME→MEANING (also in this probe's report):
//   - A clean begin/end/cap "vec_candidate" triple on the MANAGER dump (around
//     +0x48 = field[9]) → the manager itself carries the key→id vector; step 2b
//     walks the manager at that offset.
//   - manager+0x18 ("lang_table_ptr") logged NULL → the language table is not
//     loaded at this fire; we must re-dump on a LATER fire (a getter call, when
//     a language is set). Reported so step 2b knows WHEN the table populates.
//   - manager+0x18 non-null AND its "lang" dump shows a vec_candidate (around
//     its +0x48) → the language-table layout is confirmed directly; step 2b
//     walks *(manager+0x18) at that offset. This is the path the RE predicts.
//   - Nothing resembles a vector on either → layout differs from the
//     AddLocalizedString sub-object assumption; the raw dump feeds a re-RE.
//
// `when` tags which fire we dumped at ("ctor" vs "getter") so the report can
// tell whether the language table was populated yet. `latch` is the per-site
// one-shot guard (ctor vs getter) so each site dumps exactly once.
void LayoutDump(void* manager, const char* when, std::atomic<bool>* latch) {
    bool expected = false;
    if (!latch->compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {
        return;  // already dumped at this site this session (one-shot)
    }
    if (!manager) {
        log::Warn("LOC_DUMP: layout probe skipped — captured manager is null");
        return;
    }

    LOG_DEBUG_KV("LOC_DUMP", "layout_begin",
                 log::KV("manager", manager),
                 log::KV::BareStr("when", when));

    // --- Manager struct dump. The manager `this` is a valid object; reading
    //     its own bytes does not fault. Guard anyway for uniformity. ---
    uint64_t mgr[kDumpQwords] = {0};
    if (!ReadQwordsNoFault(manager, mgr, kDumpQwords)) {
        log::Error("LOC_DUMP: layout probe FAILED — manager this unreadable "
                   "(fault reading its own bytes; capture is bad)");
        return;
    }
    DumpBufferFields("mgr", mgr);

    // --- Known landmarks from the ctor decompile, logged explicitly so the
    //     reader can orient (these indices are READ from the buffer above, not
    //     re-dereferenced). ---
    //   manager+0x18 = this[3]  — current-language ptr ("No language set"
    //                             guard; ctor sets it 0 as its last act).
    //   manager+0x30 = this[6]  — sentinel-node ptr (ctor: node->{+0,+8,+0x10}
    //                             =node, *(u16*)(node+0x18)=0x101).
    //   manager+0x58 = this[0xb]— field plVar1 the ctor zeroes (vector-cap
    //                             slot in the AddLocalizedString param_2 model).
    uint64_t langTablePtr = mgr[3];   // +0x18
    LOG_DEBUG_KV("LOC_DUMP", "landmarks",
                 log::KV("lang_table_ptr_0x18", reinterpret_cast<void*>(langTablePtr)),
                 log::KV("sentinel_node_0x30",  reinterpret_cast<void*>(mgr[6])),
                 log::KV("field_0xb_0x58",      reinterpret_cast<void*>(mgr[0xb])));

    // --- The pointed-to current-language sub-object. If null, the table has
    //     not loaded — SAY SO (AP14), and step 2b must dump on a later fire. If
    //     non-null, dump its first 0x100 bytes the SAME way, fault-guarded (a
    //     stale pointer here is observed, not a crash). ---
    if (langTablePtr == 0) {
        LOG_DEBUG_KV("LOC_DUMP", "lang_table_null",
                     log::KV::BareStr("when", when),
                     log::KV("note",
                             "manager+0x18 (current language) is null at this "
                             "fire — language table not loaded yet; re-dump on "
                             "a later getter fire to see its layout"));
        LOG_DEBUG_KV("LOC_DUMP", "layout_end",
                     log::KV("manager", manager));
        return;
    }

    uint64_t lang[kDumpQwords] = {0};
    if (!ReadQwordsNoFault(reinterpret_cast<void*>(langTablePtr), lang,
                           kDumpQwords)) {
        // Warn, not Error: an unreadable reached-pointer is an EXPECTED probe
        // outcome (manager+0x18 may hold a not-yet-valid table ptr at this fire),
        // not a crash-risk product fail-state — severity matches consequence
        // (fail-state-logging.md §severity). The manager-this-unreadable case
        // above stays Error (that IS a bad capture).
        log::WarnF("LOC_DUMP: layout probe — language-table ptr %p "
                   "(manager+0x18) UNREADABLE (faulted on deref); not a valid "
                   "table object at this fire",
                   reinterpret_cast<void*>(langTablePtr));
        LOG_DEBUG_KV("LOC_DUMP", "layout_end",
                     log::KV("manager", manager));
        return;
    }
    LOG_DEBUG_KV("LOC_DUMP", "lang_table",
                 log::KV("ptr", reinterpret_cast<void*>(langTablePtr)),
                 log::KV::BareStr("when", when));
    DumpBufferFields("lang", lang);

    LOG_DEBUG_KV("LOC_DUMP", "layout_end",
                 log::KV("manager", manager));
}

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
    char* result = orig(self, id);

    // Step 2a layout probe (one-shot, getter site). The getter fires only AFTER
    // a language is loaded (it resolves a string for the current language), so
    // here manager+0x18 (current language) should be populated — the dump that
    // shows the language-table layout directly. `self` is the manager `this`
    // (slot-1 getter is a CLocalizedStringsManager method). Observe-only;
    // dumped after orig ran so the read sees the post-call state.
    LayoutDump(self, "getter", &g_layout_dumped_getter);

    return result;
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
    kcdx::modification_inventory::RegisterModification(
        reinterpret_cast<uintptr_t>(getterTarget),
        kcdx::modification_inventory::Category::Probe, "loc_dump:getter");
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

    // Step 2a layout probe (one-shot, ctor site). The ctor has fully run, so
    // the manager's fields are initialized — but manager+0x18 (current
    // language) is NULL by construction (the ctor's last act is param_1[3]=0),
    // so this dump shows the MANAGER layout with no language table yet. The
    // getter-site dump (above) catches the populated language table later.
    // Observe-only.
    LayoutDump(self, "ctor", &g_layout_dumped_ctor);

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
