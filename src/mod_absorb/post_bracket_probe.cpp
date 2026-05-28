#include "post_bracket_probe.h"

// === ARCHIVED PROBE B (2026-05-28): the engine dispatched on the C_ModManager vtable VA itself, not on kcdx's heap obj — the bracket built a modMgr the engine never read through this path.
// === Root cause: ctor bracket returned the heap obj instead of outResult; the install helper's `mov rax, [rdx]` then loaded the vtable VA from the heap block's +0x00 and installed it as the modMgr pointer.
// === See: docs/known-issues/post-step-4 AV at WHGame+0x2440C85.md §Resolution.
// === Revive by flipping #if 0 -> #if 1.
#if 0

#include <windows.h>

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <cstring>

#include "MinHook.h"

#include "select_detour.h"   // GetEnabledListData() — the kcdx-owned
                              // process-lifetime vector the bracket repoints
                              // C_ModManager+0x30 at.
#include "../log.h"

// === DIAGNOSTIC (PROBE B): observe what the engine is actually dispatching
// on at the post-bracket "lookup-by-name in enabled list" call site (frame 4
// of the WHGame+0x2440C85 AV stack). Outcome-map:
//   - rcx == kcdx_obj AND triple is intact  → corruption is BETWEEN frame-4
//     entry and the AV (next probe: hook FUN_1DBBE20 / FUN_1DBC230 too)
//   - rcx == kcdx_obj BUT triple is bad      → something post-bracket OVERWROTE
//     modMgr+0x30; bisect the writer
//   - rcx != kcdx_obj                        → second modMgr the engine reads;
//     find its ctor / sentinel and bracket that too
//   - rcx == kcdx_obj AND triple intact AND
//     hook never fires before the AV          → frame-4 isn't the AV path;
//     hook FUN_1DBBE20 directly

namespace kcdx::mod_absorb {

namespace {

constexpr const char* kCat = "MOD_ABSORB";

// Frame-4 function RVA (cdb-confirmed; see PROBE A static analysis at
// `_research/init-cycle-recon/probe_a_static_singleton.py`). The function's
// ABI: ptr (ptr this /*rcx*/, ptr arg /*rdx*/) — single this-arg from frame-5
// call site `mov rcx, [r13]; call FUN_19C6268`. Tail-call back to the original
// after logging.
constexpr uintptr_t kFrame4Rva = 0x019C6268;

using Frame4Fn_t = void* (__fastcall*)(void* rcx, void* rdx);

std::atomic<bool>        g_installed{false};
std::atomic<bool>        g_fired{false};
std::atomic<Frame4Fn_t>  g_original{nullptr};

uintptr_t                g_whgameBase = 0;

// SEH-guarded qword read. Returns true on success, writes the qword to *out;
// returns false if dereferencing addr raised an AV. The bracket uses the
// same idiom; this duplicates rather than depending on a shared helper to
// keep the probe stand-alone (revert is one file delete).
bool SafeReadQword(const void* addr, uint64_t* out) {
    __try {
        std::memcpy(out, addr, sizeof(uint64_t));
        return true;
    } __except (EXCEPTION_EXECUTE_HANDLER) {
        *out = 0;
        return false;
    }
}

// Log a single I_Mod* slot's first qword (its vtable) and a fingerprint of
// the next few bytes. SEH-guarded — a bad slot logs FAULTED instead of taking
// out the whole probe.
void LogSlot(const char* label, void* slot_value) {
    uint64_t at_zero  = 0;
    uint64_t at_eight = 0;
    bool ok0 = SafeReadQword(slot_value, &at_zero);
    bool ok8 = SafeReadQword(static_cast<uint8_t*>(slot_value) + 8, &at_eight);
    LOG_INFO_KV(kCat, "probe_b_slot",
        kcdx::log::KV::BareStr("which", label),
        kcdx::log::KV("slot_value", reinterpret_cast<uintptr_t>(slot_value)),
        kcdx::log::KV("deref_at_0",  at_zero),
        kcdx::log::KV("deref_at_8",  at_eight),
        kcdx::log::KV::BareStr("status",
            (ok0 && ok8) ? "ok" : (ok0 ? "ok_at_0_FAULT_at_8" : "FAULT_at_0")));
}

void* __fastcall HookedFrame4(void* rcx, void* rdx) {
    // One-shot. Frame-4 is on the main-loop CSystem dispatch and will fire
    // many times across a boot; we want a single, clean dump from the FIRST
    // entry post-bracket (the one preceding the AV).
    bool expected = false;
    if (g_fired.compare_exchange_strong(expected, true,
                                        std::memory_order_acq_rel)) {

        uintptr_t modmgr_va = reinterpret_cast<uintptr_t>(rcx);

        // Capture kcdx's side: the obj/list the bracket built. The vector
        // returned here is the process-lifetime g_enabledList.
        const auto& enabled = GetEnabledListData();
        const void* kcdx_list_data = enabled.data();
        const uint64_t kcdx_list_size = enabled.size();

        LOG_INFO_KV(kCat, "probe_b_enter",
            kcdx::log::KV("rcx_arg",                       modmgr_va),
            kcdx::log::KV("rdx_arg",                       reinterpret_cast<uintptr_t>(rdx)),
            kcdx::log::KV("kcdx_g_enabled_list_data",      reinterpret_cast<uintptr_t>(kcdx_list_data)),
            kcdx::log::KV("kcdx_g_enabled_list_size",      kcdx_list_size),
            kcdx::log::KV::BareStr("detail",
                "PROBE B first entry to frame-4 function at RVA 0x019C6268 "
                "post-bracket — comparing the engine's modMgr `this` and its "
                "enabled-list triple against the kcdx-built list. If "
                "rcx_arg matches the obj logged by ctor_bracket_complete AND "
                "the triple at rcx+0x30/+0x38/+0x40 matches kcdx_g_enabled_list "
                "begin/end/cap, the bracket installed cleanly; the AV's "
                "garbage rcx came from later corruption or a different path. "
                "If rcx_arg DIFFERS, the engine maintains a second modMgr "
                "the bracket did not own"));

        // Walk the gEnv-style singleton chain that frame-4 dispatches
        // through: `mov rcx, [rip + global @ RVA 0x0492B8A8]; mov rax, [rcx];
        // call [rax + 0xB8]; mov rcx, rax`. Settles the post-step-4 open
        // question — what is the global writer/getter, and does its
        // virtual+0xB8 thunk return the kcdx obj or something else.
        //   - *global   = the singleton instance pointer.
        //   - *(that)+0 = its vtable.
        //   - *(vt+0xB8) = the function pointer the call dispatches to.
        // All three reads SEH-guarded (the global may still be null very
        // early, the instance/vtable may be a partial init).
        {
            constexpr uintptr_t kSingletonGlobalRva = 0x0492B8A8;
            const uintptr_t globalVa = g_whgameBase + kSingletonGlobalRva;
            uint64_t singleton_ptr = 0;
            bool ok_global = SafeReadQword(
                reinterpret_cast<const void*>(globalVa), &singleton_ptr);
            uint64_t singleton_vtable = 0;
            bool ok_vt = false;
            if (ok_global && singleton_ptr) {
                ok_vt = SafeReadQword(
                    reinterpret_cast<const void*>(singleton_ptr),
                    &singleton_vtable);
            }
            uint64_t getter_fn = 0;
            bool ok_getter = false;
            if (ok_vt && singleton_vtable) {
                ok_getter = SafeReadQword(
                    reinterpret_cast<const void*>(singleton_vtable + 0xB8),
                    &getter_fn);
            }
            LOG_INFO_KV(kCat, "probe_b_singleton",
                kcdx::log::KV("global_va",        globalVa),
                kcdx::log::KV("singleton_ptr",    singleton_ptr),
                kcdx::log::KV("singleton_vtable", singleton_vtable),
                kcdx::log::KV("getter_fn_at_B8", getter_fn),
                kcdx::log::KV::BareStr("global_status",   ok_global ? "ok" : "FAULTED"),
                kcdx::log::KV::BareStr("vtable_status",   ok_vt     ? "ok" : "FAULTED"),
                kcdx::log::KV::BareStr("getter_status",   ok_getter ? "ok" : "FAULTED"),
                kcdx::log::KV::BareStr("detail",
                    "static walk of *0x0492B8A8 + its vtable[0] + "
                    "[vtable+0xB8] — closes the post-step-4 open question "
                    "about who writes the singleton and what the virtual "
                    "+0xB8 getter resolves to. Compare getter_fn against "
                    "kcdx's HookedCtor outResult or the C_ModManager vtable "
                    "VA to determine whether the install path was correct"));
        }

        // Walk modMgr+0x00..+0x68 with SEH guards on every deref. A bad rcx
        // (the AV case) yields FAULTED lines but does not take the probe out.
        if (rcx) {
            uint8_t* base = static_cast<uint8_t*>(rcx);
            struct OffLabel { size_t off; const char* name; };
            const OffLabel slots[] = {
                {0x00, "vtable"},
                {0x08, "sys"},
                {0x10, "modsDir_data_ptr"},
                {0x18, "scanned_begin"},
                {0x20, "scanned_end"},
                {0x28, "scanned_cap"},
                {0x30, "enabled_begin"},
                {0x38, "enabled_end"},
                {0x40, "enabled_cap"},
                {0x48, "unused_48"},
                {0x50, "unused_50"},
                {0x58, "unused_58"},
                {0x60, "init_flag_qword"},
            };
            for (const auto& s : slots) {
                uint64_t v = 0;
                bool ok = SafeReadQword(base + s.off, &v);
                LOG_INFO_KV(kCat, "probe_b_modmgr_field",
                    kcdx::log::KV::BareStr("field", s.name),
                    kcdx::log::KV("offset",        (uint64_t)s.off),
                    kcdx::log::KV("value",         v),
                    kcdx::log::KV::BareStr("status", ok ? "ok" : "FAULTED"));
            }

            // If +0x30/+0x38 look sane, sample the first 3 enabled slots'
            // contents — the AV says one of these slots was code-bytes.
            uint64_t enbeg = 0, enend = 0;
            (void)SafeReadQword(base + 0x30, &enbeg);
            (void)SafeReadQword(base + 0x38, &enend);
            if (enbeg && enend && enend > enbeg) {
                const size_t stride   = sizeof(void*);
                const size_t n_slots  = (enend - enbeg) / stride;
                const size_t n_sample = n_slots < 3 ? n_slots : 3;
                for (size_t i = 0; i < n_sample; ++i) {
                    void* slot_addr = reinterpret_cast<void*>(enbeg + i * stride);
                    uint64_t slot_value = 0;
                    bool ok = SafeReadQword(slot_addr, &slot_value);
                    if (!ok) {
                        LOG_INFO_KV(kCat, "probe_b_enabled_slot",
                            kcdx::log::KV("idx", (uint64_t)i),
                            kcdx::log::KV::BareStr("status", "FAULTED"));
                        continue;
                    }
                    char label[32];
                    std::snprintf(label, sizeof(label), "enabled[%zu]", i);
                    LogSlot(label, reinterpret_cast<void*>(slot_value));
                }
            }
        }

        // Sample the first 3 entries of g_enabledList for direct comparison
        // (these should match what enabled[0..2] read above, IF the modMgr
        // belongs to kcdx).
        const size_t n_sample = enabled.size() < 3 ? enabled.size() : 3;
        for (size_t i = 0; i < n_sample; ++i) {
            char label[40];
            std::snprintf(label, sizeof(label), "g_enabledList[%zu]", i);
            LogSlot(label, enabled[i]);
        }
    }

    // Tail to the original (no behaviour change — pure observation).
    Frame4Fn_t orig = g_original.load(std::memory_order_acquire);
    if (orig) {
        return orig(rcx, rdx);
    }
    // Defensive — if the trampoline wasn't captured, returning null is the
    // safest no-op (the caller's null-check at frame-4 logs and bails).
    return nullptr;
}

}  // namespace

bool InstallPostBracketProbe() {
    bool expected = false;
    if (!g_installed.compare_exchange_strong(expected, true,
                                             std::memory_order_acq_rel)) {
        return g_original.load(std::memory_order_acquire) != nullptr;
    }

    HMODULE whgame = GetModuleHandleW(L"WHGame.dll");
    if (!whgame) {
        LOG_ERROR_KV(kCat, "probe_b_install_failed",
            kcdx::log::KV::BareStr("reason",
                "WHGame.dll is not mapped at InstallPostBracketProbe time — "
                "the probe is INACTIVE this boot; the AV is unobserved"));
        return false;
    }
    g_whgameBase = reinterpret_cast<uintptr_t>(whgame);

    void* target = reinterpret_cast<void*>(g_whgameBase + kFrame4Rva);

    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        LOG_ERROR_KV(kCat, "probe_b_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_Initialize failed — probe INACTIVE"),
            kcdx::log::KV("mh_status", (long long)si));
        return false;
    }

    void* orig = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedFrame4),
                                &orig);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "probe_b_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_CreateHook on frame-4 target failed — probe INACTIVE"),
            kcdx::log::KV("target",    reinterpret_cast<uintptr_t>(target)),
            kcdx::log::KV("mh_status", (long long)s));
        return false;
    }
    g_original.store(reinterpret_cast<Frame4Fn_t>(orig),
                     std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        LOG_ERROR_KV(kCat, "probe_b_install_failed",
            kcdx::log::KV::BareStr("reason",
                "MH_EnableHook on frame-4 target failed — probe INACTIVE"),
            kcdx::log::KV("target",    reinterpret_cast<uintptr_t>(target)),
            kcdx::log::KV("mh_status", (long long)s));
        return false;
    }

    LOG_INFO_KV(kCat, "probe_b_installed",
        kcdx::log::KV("target",          reinterpret_cast<uintptr_t>(target)),
        kcdx::log::KV("rva",             (uint64_t)kFrame4Rva),
        kcdx::log::KV::BareStr("detail",
            "PROBE B armed — when the game's CSystem::Init reaches the "
            "frame-4 lookup-by-name call, the detour will log the modMgr "
            "this/triple/enabled-slots ONCE and tail to the original. "
            "Diagnostic only; reverts before the next probe"));
    return true;
}

}  // namespace kcdx::mod_absorb

#endif  // ARCHIVED PROBE B
