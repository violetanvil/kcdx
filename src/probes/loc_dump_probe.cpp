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

#include <windows.h>    // RtlCaptureStackBackTrace (declared in winnt.h, kernel32-exported)
#include <intrin.h>     // _ReturnAddress()

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>  // std::snprintf (loc-fire report reason + stack-frame buffer)
#include <cstring>  // std::memcmp / std::strncpy (seen-key set)
#include <mutex>   // dev-only guard around the seen-key set

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

// === Verified RE constants ==========================================
//
// PROBE-LOCAL LABELED CONSTANTS, not Address Library IDs — a USER-APPROVED
// DEFERRAL for this diagnostic step (mirrors bugsplat_ctor_probe's RVA-comment
// style). Promoting these to resolved seed IDs (so the address is never a
// hardcoded RVA) is deferred to feature graduation by explicit user decision.
// This is NOT an oversight: a labeled-RVA here is the sanctioned form for the
// probe; the seed-ID promotion lands when the loc-dump feature graduates out of
// the probe stage.
//
// CLocalizedStringsManager ctor (FUN_1809f0ce4). First store is `*this =
// vtable`; hooking it captures `this` (RCX = Win64 fastcall arg 1).
constexpr uintptr_t kCtorRva = 0x9f0ce4;

// The two PUBLIC LocalizeString overloads are manager vtable slots 21 and 22.
// We do NOT hardcode their RVAs — we read each address off the captured
// instance's LIVE vtable at runtime: `(*(void***)this)[21]` / `[22]`. This is
// the robust path (no overload RVA to maintain, no ASLR base arithmetic) and
// these are the slots the RE pins (verified against the binary): slot 21
// (offset 0xA8) = FUN_18051d514 (CryStringT overload), slot 22
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
// TWO-arg (verified against the binary by Ghidra analysis — FUN_1809f0ce4):
// param_1 = `this` (RCX), param_2 = a system/context pointer (RDX). The ctor
// stores it (`this[2] = param_2`) and makes a virtual call through it
// (`(**(code**)(*param_2 + 0x2a0))(param_2)` at ~ctor+0x110). The probe is
// observe-only: it passes `sysctx` through UNTOUCHED — never reads or derefs
// it. (A one-arg typedef left RDX = register garbage, so the ctor's
// `*param_2 + 0x2a0` deref faulted — the arg count was wrong; the signature is
// verified against the binary, not inferred from prologue shape.)
using LocCtor_t = void* (__fastcall*)(void* self, void* sysctx);

// LocalizeString overload typedefs — verified against the binary by capstone
// body-wide stack-arg analysis + full disassembly. Both are 4-arg __fastcall
// returning char (bool). The key string is RDX in both, but the CryStringT
// overload (slot 21) needs ONE deref.
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
// strlen in the logger would — fail loud, don't go silent or crash). `out`
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

// === Decision-1 stack-shape probe (per-@-key call-stack capture) =====
//
// GOAL: the prior live run showed caller_ra (the IMMEDIATE return address)
// collapses to a SINGLE value — RVA 0x51d4a8, a universal text-FORMATTING
// chokepoint — across all 11,589 fires. That immediate edge does NOT serve
// find{text=}: it points
// at the formatter, not "the inventory screen" / "the buff system". This probe
// captures the frames ABOVE that chokepoint to answer the gating question:
//
//   Do frames[1..7] (above the chokepoint at frames[0]~=0x51d4a8) DIFFER across
//   different @-keys?
//     → YES (per-key variation): a real gameplay frame is reachable in the live
//       stack; find{text=} CAN get the gameplay function via a capture-time
//       stack-walk.
//     → NO (identical / near-identical across all keys): everything funnels
//       through the same dispatcher stack above the chokepoint too; runtime
//       caller capture cannot yield the gameplay function, and a different
//       (non-runtime) mechanism is needed.
//
// Observe-only, dev-mode, same discipline as the existing hooks. This probe
// DUMPs frames only — it builds NO caller-selection logic (the human reads
// whether the frames vary). It is the approved Decision-1 stack-shape probe,
// run BEFORE choosing the caller-capture mechanism (results-driven).

// WHGame.dll module range, resolved ONCE so each captured frame can be logged
// as a base-relative RVA (comparable across runs / ASLR). Frames outside this
// range left the game module and are logged raw-tagged (themselves informative
// — the stack left WHGame.dll), never silently dropped.
struct WhgameRange {
    uintptr_t base = 0;
    uintptr_t end  = 0;  // base + image size (exclusive)
};

WhgameRange ResolveWhgameRangeOnce() {
    static WhgameRange r = [] {
        WhgameRange out;
        HMODULE m = GetModuleHandleW(L"WHGame.dll");
        if (!m) return out;  // base stays 0 → every frame logged raw-tagged below
        out.base = reinterpret_cast<uintptr_t>(m);
        // Image size from the PE optional header (SizeOfImage).
        auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(m);
        auto* nt  = reinterpret_cast<const IMAGE_NT_HEADERS*>(
            out.base + dos->e_lfanew);
        out.end = out.base + nt->OptionalHeader.SizeOfImage;
        return out;
    }();
    return r;
}

// Volume bounds (dev-only): log the stack for the FIRST N DISTINCT @-keys only,
// once per key, and hard-cap total stack-logs so a key churn can't flood the
// log. The prior run saw 914 distinct @-keys / 5,447 @-key fires — we only need
// a SAMPLE to read whether the frames vary.
constexpr size_t kMaxDistinctKeys = 200;  // distinct @-keys to sample
constexpr size_t kMaxStackLogs    = 300;  // absolute log-line cap (belt+braces)
constexpr size_t kKeyBufLen       = 64;   // per-stored-key length (truncated)

std::mutex g_seenKeysMutex;  // dev-only; the hot path is the formatter chokepoint
                             // but the seen-set must not race across UI threads.
std::array<std::array<char, kKeyBufLen>, kMaxDistinctKeys> g_seenKeys{};
size_t g_seenKeyCount = 0;                  // # distinct @-keys recorded
std::atomic<size_t> g_stackLogCount{0};     // total stack lines emitted

// Returns true the FIRST time this @-key is seen (and records it); false if
// already logged or if the distinct-key / total-log cap is hit. Bounded under
// g_seenKeysMutex. `key` is the already-snapshotted, NUL-terminated readable
// string (always starts with '@' by the caller's filter).
bool ShouldLogStackForKey(const char* key) {
    if (g_stackLogCount.load(std::memory_order_relaxed) >= kMaxStackLogs) {
        return false;  // absolute cap reached — stop emitting
    }
    std::lock_guard<std::mutex> lk(g_seenKeysMutex);
    for (size_t i = 0; i < g_seenKeyCount; ++i) {
        if (std::strncmp(g_seenKeys[i].data(), key, kKeyBufLen) == 0) {
            return false;  // already logged this key's stack
        }
    }
    if (g_seenKeyCount >= kMaxDistinctKeys) {
        return false;  // distinct-key sample full
    }
    std::strncpy(g_seenKeys[g_seenKeyCount].data(), key, kKeyBufLen - 1);
    g_seenKeys[g_seenKeyCount][kKeyBufLen - 1] = '\0';
    ++g_seenKeyCount;
    return true;
}

// Capture ~8 frames above the detour and log them as WHGame.dll-relative RVAs
// (or raw-tagged if outside the module) under LOC_DUMP. Called only for @-key
// fires that pass ShouldLogStackForKey. `slot` is 21/22; `key` is the readable
// snapshotted key (starts with '@'). frames[0] is the immediate caller (~the
// 0x51d4a8 chokepoint); frames[1..] are what this probe exists to compare.
//
// Fail-state: RtlCaptureStackBackTrace returning 0 frames is logged as a
// distinct "captured=0" line — "ran and found nothing" is NOT a silent blank.
void LogStackForKey(int slot, const char* key) {
    void* frames[8] = {};
    USHORT n = RtlCaptureStackBackTrace(/*FramesToSkip*/ 1,
                                        /*FramesToCapture*/ 8, frames, nullptr);
    WhgameRange r = ResolveWhgameRangeOnce();

    if (n == 0) {
        // Ran, captured nothing — say so loudly rather than emit a blank list.
        LOG_DEBUG_KV("LOC_DUMP", "localizestring_stack",
                     log::KV("slot", (uint64_t)slot),
                     log::KV::BareStr("key", key),
                     log::KV::BareStr("frames", "[captured=0]"));
        g_stackLogCount.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    // Build the frames=[...] list: WHGame.dll-relative RVA (0xNNN) when inside
    // the module, raw pointer tagged (raw:0xNNN) when outside it.
    char list[256];
    size_t pos = 0;
    list[pos++] = '[';
    for (USHORT i = 0; i < n && pos < sizeof(list) - 24; ++i) {
        uintptr_t f = reinterpret_cast<uintptr_t>(frames[i]);
        int w;
        if (r.base && f >= r.base && f < r.end) {
            w = std::snprintf(list + pos, sizeof(list) - pos,
                              "%s0x%llx", (i ? "," : ""),
                              (unsigned long long)(f - r.base));
        } else {
            // Outside WHGame.dll (or base unresolved) — informative, not dropped.
            w = std::snprintf(list + pos, sizeof(list) - pos,
                              "%sraw:0x%llx", (i ? "," : ""),
                              (unsigned long long)f);
        }
        if (w <= 0) break;
        pos += (size_t)w;
    }
    if (pos < sizeof(list) - 1) list[pos++] = ']';
    list[pos] = '\0';

    LOG_DEBUG_KV("LOC_DUMP", "localizestring_stack",
                 log::KV("slot", (uint64_t)slot),
                 log::KV::BareStr("key", key),
                 log::KV::BareStr("frames", list));
    g_stackLogCount.fetch_add(1, std::memory_order_relaxed);
}

// True if the readable key is a REAL @-key (starts with '@'), filtering out the
// formatted-value noise ("88", "0%", "(empty)", "(null)") the slot-21 formatter
// also routes. Only @-key fires get a stack dump (volume control + relevance:
// @-keys are exactly what find{text=} targets).
bool IsAtKey(const char* readable) {
    return readable && readable[0] == '@';
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

    // Decision-1 stack-shape probe: for a REAL @-key (not the formatted-value
    // noise), DUMP the call stack above the chokepoint, bounded to the first N
    // distinct keys. frames[0] ~= the 0x51d4a8 formatter chokepoint; frames[1..]
    // are what we read for per-key variation. See LogStackForKey.
    if (IsAtKey(readable) && ShouldLogStackForKey(readable)) {
        LogStackForKey((int)kLocSlot21, readable);
    }

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

    // Decision-1 stack-shape probe (same as slot 21): @-key fires only, bounded.
    if (IsAtKey(readable) && ShouldLogStackForKey(readable)) {
        LogStackForKey((int)kLocSlot22, readable);
    }

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
// missed observation, not a crash risk).
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
