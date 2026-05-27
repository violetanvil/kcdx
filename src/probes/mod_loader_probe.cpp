// === MOD-LOADER PROBE (Phase 8.5 absorb — PROBE U.6) =================
//
// See mod_loader_probe.h for the full framing. Log-only detour on the engine's
// mod-loader SELECT orchestrator (wh::C_ModManager FUN_180da104c) resolving two
// probe-first gates in one launch: U.6.1 timing (does it fire from the worker-
// thread install point?) + U.6.2 the I_Mod record layout (dump the first record).
//
// Mirrors loc_dump_probe's MinHook discipline. The orchestrator RVA is a
// PROBE-LOCAL LABELED CONSTANT (not a seed id yet) — the loc_dump_probe
// precedent: a freshly-RE'd RVA for a throwaway diagnostic uses a labeled
// constant; the seed-id promotion (AP1) lands when the absorb feature graduates
// out of the probe stage. The RVA is from FINDINGS.md §"MOD-LOADER ORCHESTRATOR"
// (binary-verified, commit 99b0457).

#include "mod_loader_probe.h"

#include <windows.h>

#include <atomic>
#include <cstdint>

#include "MinHook.h"

#include "../dev.h"
#include "../log.h"

namespace kcdx::probes::mod_loader_probe {

namespace {

// SELECT orchestrator: wh::C_ModManager scan/select (FUN_180da104c). fastcall
// void(C_ModManager* this) — this-only (abi_walker-confirmed, FINDINGS U.6).
// RVA from the orchestrator RE (binary-verified).
constexpr uintptr_t kSelectRva = 0xDA104C;

// The enabled-mod list at C_ModManager+0x30 is a std::vector-style range:
//   +0x30 = begin ptr, +0x38 = end ptr; count = (end - begin) / 8.
// Elements are 8-byte I_Mod POINTERS (the engine computes count as (end-begin)>>3,
// FINDINGS ORCH-A FUN_180da1294). Each pointed-to I_Mod record is 0x70 bytes.
constexpr size_t kListBeginOff = 0x30;
constexpr size_t kListEndOff   = 0x38;
constexpr size_t kIModRecordSize = 0x70;

using SelectFn_t = void (__fastcall*)(void* self);

std::atomic<SelectFn_t> g_orig{nullptr};
std::atomic<bool>       g_installed{false};
std::atomic<bool>       g_fired{false};  // one-shot dump latch

// Dump the first 0x70-byte I_Mod record as hex rows (16 bytes/row) so the
// layout can be reverse-engineered from the log. Read-only.
void DumpRecord(const uint8_t* rec) {
    for (size_t row = 0; row < kIModRecordSize; row += 16) {
        char hex[16 * 3 + 1] = {0};
        size_t pos = 0;
        for (size_t i = 0; i < 16 && (row + i) < kIModRecordSize; ++i) {
            static const char* H = "0123456789ABCDEF";
            uint8_t b = rec[row + i];
            hex[pos++] = H[b >> 4];
            hex[pos++] = H[b & 0xF];
            hex[pos++] = ' ';
        }
        LOG_DEBUG_KV("MODLOADER_PROBE", "imod_bytes",
                     log::KV("off",  (uint64_t)row),
                     log::KV("hex",  hex));
    }
}

void __fastcall HookedSelect(void* self) {
    // U.6.1 TIMING: the detour fired -> a worker-thread install IS early enough
    // to take over the SELECT phase. (If this never logs but the native
    // "[Mod] ... mods enabled" line appears in kcd.log, the native select ran
    // before our install -> needs before_game timing.)
    LOG_DEBUG_KV("MODLOADER_PROBE", "select_fire",
                 log::KV("this", self),
                 log::KV("note", "C_ModManager SELECT orchestrator (FUN_180da104c) "
                                 "detour fired -> worker-thread install is in time"));

    // Run the ORIGINAL first so the enabled list is populated, THEN read it.
    SelectFn_t orig = g_orig.load(std::memory_order_acquire);
    if (orig) {
        orig(self);
    } else {
        log::Error("MODLOADER_PROBE: orig SELECT pointer null at dispatch");
        return;
    }

    // U.6.2 I_MOD LAYOUT: one-shot dump of the enabled list + the first record.
    bool expected = false;
    if (!g_fired.compare_exchange_strong(expected, true,
                                         std::memory_order_acq_rel)) {
        return;  // already dumped
    }
    if (!self) return;

    auto base = reinterpret_cast<const uint8_t*>(self);
    void* beginPtr = *reinterpret_cast<void* const*>(base + kListBeginOff);
    void* endPtr   = *reinterpret_cast<void* const*>(base + kListEndOff);
    uint64_t count = 0;
    if (beginPtr && endPtr && endPtr >= beginPtr) {
        count = (reinterpret_cast<uintptr_t>(endPtr) -
                 reinterpret_cast<uintptr_t>(beginPtr)) / 8;
    }
    LOG_DEBUG_KV("MODLOADER_PROBE", "enabled_list",
                 log::KV("this+0x30_begin", beginPtr),
                 log::KV("this+0x38_end",   endPtr),
                 log::KV("count",           count),
                 log::KV("note", "list is a vector of 8-byte I_Mod pointers; "
                                 "count=(end-begin)/8; each record is 0x70 bytes"));

    if (count > 0 && beginPtr) {
        // First element is an I_Mod* — deref to the 0x70-byte record + dump it.
        void* firstImod = *reinterpret_cast<void* const*>(beginPtr);
        LOG_DEBUG_KV("MODLOADER_PROBE", "imod_first",
                     log::KV("imod_ptr", firstImod));
        if (firstImod) {
            DumpRecord(reinterpret_cast<const uint8_t*>(firstImod));
        }
    }
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
        log::Warn("MODLOADER_PROBE: WHGame.dll not mapped yet; cannot install SELECT detour");
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    // MinHook idempotent init (the worker-thread caller already initialized it).
    MH_STATUS si = MH_Initialize();
    if (si != MH_OK && si != MH_ERROR_ALREADY_INITIALIZED) {
        log::WarnF("MODLOADER_PROBE: MH_Initialize failed: %d", (int)si);
        g_installed.store(false, std::memory_order_release);
        return false;
    }

    void* target = reinterpret_cast<void*>(
        reinterpret_cast<uintptr_t>(whgame) + kSelectRva);

    void* origPtr = nullptr;
    MH_STATUS s = MH_CreateHook(target,
                                reinterpret_cast<void*>(&HookedSelect),
                                &origPtr);
    if (s != MH_OK) {
        log::WarnF("MODLOADER_PROBE: MH_CreateHook(SELECT @ %p) failed: %d",
                   target, (int)s);
        g_installed.store(false, std::memory_order_release);
        return false;
    }
    g_orig.store(reinterpret_cast<SelectFn_t>(origPtr), std::memory_order_release);

    s = MH_EnableHook(target);
    if (s != MH_OK) {
        log::WarnF("MODLOADER_PROBE: MH_EnableHook(SELECT @ %p) failed: %d",
                   target, (int)s);
        return false;
    }

    log::InfoF("MODLOADER_PROBE: C_ModManager SELECT detour installed at %p "
               "(WHGame.dll base %p, rva=0x%llx)",
               target, reinterpret_cast<void*>(whgame),
               (unsigned long long)kSelectRva);
    return true;
}

}  // namespace kcdx::probes::mod_loader_probe
